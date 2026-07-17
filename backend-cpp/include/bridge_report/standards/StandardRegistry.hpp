#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

class StandardAlgorithmAdapter {
public:
    virtual ~StandardAlgorithmAdapter() = default;
    virtual std::string algorithm_id() const = 0;
};

using StandardAlgorithmFactory =
    std::function<std::unique_ptr<StandardAlgorithmAdapter>(const StandardPackage&)>;

struct StandardRegistrationResult {
    bool accepted{false};
    bool inserted{false};
    std::optional<StandardIssue> issue;
};

class StandardRegistry {
public:
    bool register_algorithm(std::string algorithm_id, StandardAlgorithmFactory factory);
    StandardRegistrationResult register_package(StandardPackage package);

    const StandardPackage* find(const StandardPackageKey& key) const;
    std::unique_ptr<StandardAlgorithmAdapter> create_algorithm(const StandardPackageKey& key) const;
    std::size_t package_count() const noexcept;

private:
    std::map<StandardPackageKey, StandardPackage> packages_;
    std::map<std::string, StandardAlgorithmFactory> algorithm_factories_;
};

}  // namespace bridge_report::standards
