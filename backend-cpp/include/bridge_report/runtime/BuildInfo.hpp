#pragma once
#include <json/json.h>
namespace bridge_report::runtime {
// Frozen at process startup; health requests never read the current source tree.
const Json::Value& service_identity();
}
