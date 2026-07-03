#pragma once

#include <string>
#include <vector>

namespace Json {
class Value;
}  // namespace Json

namespace bridge_report::contracts {

struct ContractValidationIssue {
    std::string path;
    std::string message;
};

class ContractValidationResult {
public:
    void add_issue(std::string path, std::string message);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::vector<ContractValidationIssue>& issues() const noexcept;
    [[nodiscard]] std::string summary() const;

private:
    std::vector<ContractValidationIssue> issues_;
};

[[nodiscard]] ContractValidationResult validate_bridge_annual_inspection_data(const Json::Value& root);

}  // namespace bridge_report::contracts
