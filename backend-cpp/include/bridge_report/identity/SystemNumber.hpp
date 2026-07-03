#pragma once

#include <string>
#include <string_view>

namespace bridge_report::identity {

std::string format_system_number(std::string_view prefix, int sequence_value);
bool is_supported_system_number_prefix(std::string_view prefix);

}  // namespace bridge_report::identity
