#include "bridge_report/standards/StandardRegistry.hpp"

#include <utility>

#include "bridge_report/standards/H21Evaluator.hpp"

namespace bridge_report::standards {

StandardRegistry::StandardRegistry() {
    register_algorithm(
        "jtg-h21-2011",
        [](const StandardPackage& package) {
            return std::make_unique<H21Evaluator>(package);
        });
}

bool StandardRegistry::register_algorithm(
    std::string algorithm_id,
    StandardAlgorithmFactory factory) {
    if (algorithm_id.empty() || !factory) {
        return false;
    }
    return algorithm_factories_.emplace(std::move(algorithm_id), std::move(factory)).second;
}

StandardRegistrationResult StandardRegistry::register_package(StandardPackage package) {
    const auto key = package.key();
    const auto existing = packages_.find(key);
    if (existing != packages_.end()) {
        if (existing->second.manifest.content_checksum != package.manifest.content_checksum) {
            return {
                false,
                false,
                StandardIssue{
                    "package_identity_checksum_conflict",
                    "同一规范身份和包版本不能对应不同内容摘要。",
                },
            };
        }
        return {true, false, std::nullopt};
    }

    packages_.emplace(key, std::move(package));
    return {true, true, std::nullopt};
}

const StandardPackage* StandardRegistry::find(const StandardPackageKey& key) const {
    const auto found = packages_.find(key);
    return found == packages_.end() ? nullptr : &found->second;
}

std::unique_ptr<StandardAlgorithmAdapter> StandardRegistry::create_algorithm(
    const StandardPackageKey& key) const {
    const auto* package = find(key);
    if (package == nullptr) {
        return nullptr;
    }
    const auto factory = algorithm_factories_.find(package->manifest.algorithm_id);
    if (factory == algorithm_factories_.end()) {
        return nullptr;
    }
    return factory->second(*package);
}

std::size_t StandardRegistry::package_count() const noexcept {
    return packages_.size();
}

}  // namespace bridge_report::standards
