#pragma once

#include <json/json.h>

#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

Json::Value standard_package_summary_json(const db::StandardPackageRecord& package);
Json::Value standard_catalog_json(
    const db::StandardPackageRecord& record,
    const StandardPackage& package);

}  // namespace bridge_report::standards
