#include "bridge_report/runtime/BuildInfo.hpp"
#include "BuildMetadata.hpp"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <drogon/utils/Utilities.h>

namespace bridge_report::runtime {
const Json::Value& service_identity() {
    static const Json::Value identity = [] {
        Json::Value value;
        Json::CharReaderBuilder builder;
        std::istringstream stream(kBuildMetadata);
        std::string errors;
        if (!Json::parseFromStream(builder, stream, &value, &errors)) {
            throw std::runtime_error("Invalid embedded build metadata");
        }
        const auto* launch = std::getenv("BRIDGE_REPORT_LAUNCH_ID");
        value["launch_id"] = launch == nullptr ? "" : launch;
        value["instance_id"] = drogon::utils::getUuid();
        return value;
    }();
    return identity;
}
}
