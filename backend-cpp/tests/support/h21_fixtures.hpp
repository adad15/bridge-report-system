#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include "bridge_report/standards/H21Evaluator.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace bridge_report::tests::h21 {

inline std::filesystem::path package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/technical-condition/jtg-t-h21-2011/1.0.1";
}

inline standards::StandardPackage load_package() {
    standards::StandardPackageLoader loader;
    auto loaded = loader.load(package_root());
    if (!loaded.ok()) {
        throw std::runtime_error("unable to load H21 package");
    }
    return std::move(*loaded.package);
}

inline standards::H21Evaluator evaluator() {
    return standards::H21Evaluator(load_package());
}

inline standards::BridgeAssessmentInput complete_input(
    const standards::StandardPackage& package,
    const std::string& bridge_type_id) {
    standards::BridgeAssessmentInput input;
    input.bridge_type_id = bridge_type_id;

    const standards::StandardDefinition* profile = nullptr;
    for (const auto& [id, definition] : package.definitions) {
        if (id.starts_with("h21.weight_profile.") &&
            definition.payload["bridge_type_id"].asString() == bridge_type_id) {
            profile = &definition;
            break;
        }
    }
    if (profile == nullptr) {
        throw std::runtime_error("missing H21 weight profile");
    }

    for (const auto& reference : profile->references) {
        if (!reference.starts_with("h21.weight_set.") ||
            reference == "h21.weight_set.structure.default") {
            continue;
        }
        const auto& weights = package.definitions.at(reference).payload["weights"];
        for (const auto& weight : weights) {
            const auto type_id = weight["component_id"].asString();
            input.components.push_back({type_id + ".instance.1", type_id, {}});
        }
    }
    return input;
}

inline standards::BridgeAssessmentInput complete_beam_input(
    const standards::StandardPackage& package) {
    return complete_input(package, "h21.bridge_type.beam");
}

inline standards::ComponentAssessmentInput& component(
    standards::BridgeAssessmentInput& input,
    const std::string& component_type_id) {
    for (auto& item : input.components) {
        if (item.component_type_id == component_type_id) {
            return item;
        }
    }
    throw std::runtime_error("component type not present in test inventory");
}

}  // namespace bridge_report::tests::h21
