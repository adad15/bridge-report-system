#include "bridge_report/db/BridgeAdministrationRepository.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

std::string trim(std::string value) {
    const auto nonspace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
    return value;
}

std::optional<std::string> normalized_optional(const std::optional<std::string>& value) {
    if (!value.has_value()) return std::nullopt;
    auto result = trim(*value);
    return result.empty() ? std::nullopt : std::optional<std::string>(std::move(result));
}

std::optional<std::string> nullable(const drogon::orm::Field& field) {
    return field.isNull() ? std::nullopt : std::optional<std::string>(field.as<std::string>());
}

BridgeAdministrationSummary summary(const drogon::orm::Row& row) {
    return {
        row["id"].as<std::string>(), row["system_number"].as<std::string>(),
        row["bridge_name"].as<std::string>(), nullable(row["route_number"]),
        nullable(row["route_name"]), nullable(row["administrative_region"]),
        nullable(row["station_mark"]), row["status"].as<std::string>()
    };
}

}  // namespace

BridgeAdministrationRepository::BridgeAdministrationRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

CreateBridgeOutcome BridgeAdministrationRepository::create_bridge(const CreateBridgeRequest& request) {
    const auto name = trim(request.bridge_name);
    const auto route_number = normalized_optional(request.route_number);
    const auto route_name = normalized_optional(request.route_name);
    const auto region = normalized_optional(request.administrative_region);
    const auto station = normalized_optional(request.station_mark);
    if (name.empty() || (request.status != "在用" && request.status != "停用" && request.status != "拆除")) {
        return {CreateBridgeStatus::Invalid, std::nullopt};
    }

    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        tx->execSqlSync(
            "select pg_advisory_xact_lock(hashtextextended(lower(btrim($1))||chr(31)||"
            "lower(btrim($2))||chr(31)||lower(btrim($3)),0))",
            name, route_number.value_or(""), station.value_or("")
        );
        const auto existing = tx->execSqlSync(
            "select id::text as id,system_number,bridge_name,route_number,route_name,"
            "administrative_region,station_mark,status from bridges "
            "where lower(btrim(bridge_name))=lower(btrim($1)) "
            "and lower(btrim(coalesce(route_number,'')))=lower(btrim($2)) "
            "and lower(btrim(coalesce(station_mark,'')))=lower(btrim($3)) limit 1",
            name, route_number.value_or(""), station.value_or("")
        );
        if (!existing.empty()) {
            tx->rollback();
            return {CreateBridgeStatus::Duplicate, summary(existing[0])};
        }
        const auto inserted = tx->execSqlSync(
            "insert into bridges(bridge_name,route_number,route_name,administrative_region,station_mark,status) "
            "values($1,nullif($2,''),nullif($3,''),nullif($4,''),nullif($5,''),$6) "
            "returning id::text as id,system_number,bridge_name,route_number,route_name,"
            "administrative_region,station_mark,status",
            name, route_number.value_or(""), route_name.value_or(""), region.value_or(""),
            station.value_or(""), request.status
        );
        const auto created = summary(inserted[0]);
        tx.reset();
        if (!latch->wait()) return {CreateBridgeStatus::Failed, std::nullopt};
        return {CreateBridgeStatus::Created, created};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {CreateBridgeStatus::Failed, std::nullopt};
    }
}

}  // namespace bridge_report::db
