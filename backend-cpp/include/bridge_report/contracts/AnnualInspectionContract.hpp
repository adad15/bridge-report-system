#pragma once

#include <string>
#include <vector>

namespace Json {
class Value;
}  // 命名空间 Json

namespace bridge_report::contracts {

// Task 11—17 的显式过渡门：仅让既有 1.2 流程继续运行；2.0 始终按最终模式
// 拒绝 ratings、defect_deduction 和旧字段。Task 18 删除该门及旧流程。
inline constexpr bool kLegacyAnnualInspection12ValidationEnabled = true;

enum class AnnualInspectionValidationMode {
    FinalVersion2,
    Legacy12Transition,
};

// 契约校验问题使用 JSON 路径定位，方便前端或日志直接指出出错字段。
struct ContractValidationIssue {
    std::string path;
    std::string message;
};

// C++ 侧只做入库前的轻量防线；完整字段定义仍以模块 03 JSON Schema 为准。
class ContractValidationResult {
public:
    void add_issue(std::string path, std::string message);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::vector<ContractValidationIssue>& issues() const noexcept;
    [[nodiscard]] std::string summary() const;

private:
    std::vector<ContractValidationIssue> issues_;
};

// 校验年度检测候选 JSON 是否满足 C++ 入库流程依赖的关键结构约束。
[[nodiscard]] ContractValidationResult validate_bridge_annual_inspection_data(
    const Json::Value& root,
    AnnualInspectionValidationMode mode = AnnualInspectionValidationMode::FinalVersion2);

}  // 命名空间 bridge_report::contracts
