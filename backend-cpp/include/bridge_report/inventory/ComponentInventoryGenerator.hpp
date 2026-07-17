#pragma once

#include <string>
#include <vector>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::inventory {

struct InventoryGenerationResult {
    std::vector<GeneratedInventoryEntry> entries;
    std::string error_code;
    std::string error_message;

    bool ok() const noexcept { return error_code.empty(); }
};

InventoryGenerationResult generate_component_inventory(const GenerateInventoryInput& input);

}  // namespace bridge_report::inventory
