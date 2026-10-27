#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bridge_report/standards/Jtg5230Evaluator.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace bridge_report::tests::jtg5230 {

inline constexpr std::string_view kCastInPlaceSlab = "jtg5230.evaluation_type.beam.cast_in_place_slab";
inline constexpr std::string_view kOpenSpandrelSlabArch = "jtg5230.evaluation_type.arch.open_spandrel_slab";

// part("super.slab") → "jtg5230.part.super.slab"
inline std::string part(std::string_view suffix) {
    return "jtg5230.part." + std::string(suffix);
}

inline std::filesystem::path package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/technical-condition/jtg-t-5230-2026/1.0.0";
}

inline standards::StandardPackage load_package() {
    standards::StandardPackageLoader loader;
    auto loaded = loader.load(package_root());
    if (!loaded.ok()) {
        throw std::runtime_error("unable to load JTG/T 5230 package");
    }
    return std::move(*loaded.package);
}

inline std::unique_ptr<standards::Jtg5230Evaluator> evaluator() {
    auto created = standards::Jtg5230Evaluator::create(load_package());
    if (created.evaluator == nullptr) {
        throw std::runtime_error("unable to create JTG/T 5230 evaluator");
    }
    return std::move(created.evaluator);
}

inline standards::LayeredUnitInput unit(
    std::string unit_id,
    std::string_view evaluation_type_id,
    std::string weight_set_id,
    std::vector<std::string> span_ids) {
    standards::LayeredUnitInput input;
    input.unit_id = std::move(unit_id);
    input.evaluation_type_id = std::string(evaluation_type_id);
    input.weight_set_id = std::move(weight_set_id);
    input.span_ids = std::move(span_ids);
    return input;
}

inline standards::LayeredComponentInput component(
    std::string component_id,
    std::string_view part_suffix,
    std::vector<std::string> span_ids) {
    standards::LayeredComponentInput input;
    input.component_id = std::move(component_id);
    input.part_id = part(part_suffix);
    input.span_ids = std::move(span_ids);
    return input;
}

// 不带 EAI 时 ECI 恰为 eci：ESDI 取 min(eci, 3)，余下的记到 EFI。
inline standards::LayeredComponentInput component_with_eci(
    std::string component_id,
    std::string_view part_suffix,
    std::vector<std::string> span_ids,
    int eci) {
    auto input = component(std::move(component_id), part_suffix, std::move(span_ids));
    input.esdi = (std::min)(eci, 3);
    input.efi = (std::max)(eci - 3, 0);
    return input;
}

}  // namespace bridge_report::tests::jtg5230
