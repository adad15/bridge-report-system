#include "bridge_report/standards/AssessmentModels.hpp"

namespace bridge_report::standards {

std::string to_string(StructurePart part) {
    switch (part) {
        case StructurePart::superstructure:
            return "superstructure";
        case StructurePart::substructure:
            return "substructure";
        case StructurePart::deck_system:
            return "deck_system";
    }
    return "superstructure";
}

std::optional<StructurePart> parse_structure_part(std::string_view value) {
    if (value == "superstructure") {
        return StructurePart::superstructure;
    }
    if (value == "substructure") {
        return StructurePart::substructure;
    }
    if (value == "deck_system") {
        return StructurePart::deck_system;
    }
    return std::nullopt;
}

}  // namespace bridge_report::standards
