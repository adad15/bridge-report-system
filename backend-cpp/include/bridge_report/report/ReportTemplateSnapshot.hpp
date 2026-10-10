#pragma once
#include <string>
#include <drogon/orm/DbClient.h>
#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::report {
// Freeze template bytes and metadata before enqueue; throws without deleting tracked artifacts.
void prepare_report_template_snapshot(const drogon::orm::DbClientPtr& client,
    const config::AppConfig& config, const std::string& job_id);
}
