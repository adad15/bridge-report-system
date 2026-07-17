#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

std::string to_string(const StandardFamily family) {
    switch (family) {
        case StandardFamily::technical_condition:
            return "technical_condition";
        case StandardFamily::maintenance:
            return "maintenance";
    }
    return "";
}

std::optional<StandardFamily> parse_standard_family(const std::string_view value) {
    if (value == "technical_condition") {
        return StandardFamily::technical_condition;
    }
    if (value == "maintenance") {
        return StandardFamily::maintenance;
    }
    return std::nullopt;
}

StandardPackageKey StandardPackage::key() const {
    return {manifest.family, manifest.standard_id, manifest.package_version};
}

}  // namespace bridge_report::standards
