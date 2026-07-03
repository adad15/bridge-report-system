#include "bridge_report/identity/SystemNumber.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace bridge_report::identity {

namespace {

constexpr std::array<std::string_view, 14> kSupportedPrefixes{
    "QL",
    "QLBM",
    "NDJC",
    "GDWJ",
    "DRJL",
    "DRWJ",
    "GJ",
    "GJBM",
    "BHGC",
    "BHCC",
    "BHZP",
    "JSPD",
    "BHXS",
    "BHDB",
};

}  // namespace

std::string format_system_number(std::string_view prefix, int sequence_value) {
    if (sequence_value <= 0) {
        throw std::invalid_argument("System number sequence value must be positive.");
    }

    std::ostringstream output;
    output << prefix << "-" << std::setw(6) << std::setfill('0') << sequence_value;
    return output.str();
}

bool is_supported_system_number_prefix(std::string_view prefix) {
    for (const auto supported : kSupportedPrefixes) {
        if (supported == prefix) {
            return true;
        }
    }
    return false;
}

}  // namespace bridge_report::identity
