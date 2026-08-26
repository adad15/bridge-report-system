#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/rating_tree/RatingTreeModels.hpp"
#include "bridge_report/standards/AssessmentModels.hpp"
#include "bridge_report/standards/StandardModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/standards/TechnicalConditionStandard.hpp"

namespace bridge_report::assessment {

struct AssessmentPreviewPayload {
    Json::Value draft{Json::objectValue};
    int client_revision{0};
};

struct AssessmentComponentSnapshot {
    std::string component_instance_id;
    std::string component_type_id;
};

struct AssessmentPreviewIssue {
    std::string code;
    std::string message;
    std::string entity_type;
    std::string entity_id;
    std::string field_path;
    std::string rule_id;

    Json::Value to_json() const;
};

struct AssessmentContextSnapshot {
    std::string inspection_year_id;
    std::string standard_package_id;
    std::string standard_profile_id;
    std::string inventory_revision_id;
    std::string rating_tree_version_id;
    std::string rating_tree_content_checksum;
    std::optional<rating_tree::EffectiveRatingTree> rating_tree;
    bool inventory_confirmed{false};
    std::string bridge_type_id;
    std::vector<AssessmentComponentSnapshot> components;
    std::vector<AssessmentPreviewIssue> issues;
};

struct AssessmentPreview {
    int client_revision{0};
    std::string input_checksum;
    Json::Value input_summary{Json::objectValue};
    Json::Value standard_identity{Json::objectValue};
    std::optional<standards::BridgeAssessmentResult> result;
    std::vector<AssessmentPreviewIssue> issues;
    std::optional<std::string> assessment_run_id;

    Json::Value to_json() const;
};

AssessmentPreview calculate_assessment_preview(
    const standards::TechnicalConditionStandard& evaluator,
    const standards::StandardPackage& package,
    const AssessmentContextSnapshot& context,
    const Json::Value& draft,
    int client_revision);

Json::Value assessment_result_to_json(
    const standards::BridgeAssessmentResult& result);

/**
 * @brief 已入库年度评定的只读回执。
 *
 * standard / result / issues 三个字段与试算同形，界面因此能用同一套渲染；其余是运行
 * 元信息，用来说清"这份分数是哪一次正式评定写下的、有没有被后来的修订顶替"。
 */
struct ConfirmedAssessmentReport {
    std::string assessment_run_id;
    int formal_revision_number{0};
    bool is_current{false};
    std::string confirmed_at;
    int inspection_year{0};
    int inspection_year_version{0};
    /** 年度行本身是否仍是当前有效版本；false 表示这一年后来被修订过。 */
    bool inspection_year_is_current{false};
    Json::Value standard_identity{Json::objectValue};
    Json::Value result{Json::nullValue};
    Json::Value issues{Json::arrayValue};

    Json::Value to_json() const;
};

/**
 * @brief 从 assessment_runs 的两个 jsonb 列还原评定内容，不重算任何东西。
 *
 * 入库时 result_summary_json 存的就是 {"result": assessment_result_to_json(...),
 * "issues": [...]}，rule_package_summary_json 存的就是当时的 standard_identity。
 * 这里只做形状还原：拿不到 result 时留 null，让上层显示"没有可读的评定结果"，
 * 而不是编一个空壳分数出来。
 */
ConfirmedAssessmentReport build_confirmed_assessment_report(
    const Json::Value& result_summary,
    const Json::Value& rule_package_summary);

enum class ConfirmedAssessmentStatus {
    Ok,
    ImportRecordNotFound,
    ReportNotFound,
};

struct ConfirmedAssessmentOutcome {
    ConfirmedAssessmentStatus status{ConfirmedAssessmentStatus::ReportNotFound};
    ConfirmedAssessmentReport report;
};

/**
 * @brief 读取某条导入记录写下的正式评定结果。
 *
 * 纯读，不需要编辑锁，也不需要标准包——已入库的结果是既成事实，重算反而可能因为
 * 规则包升级而与当年入库的数字不一致。
 */
ConfirmedAssessmentOutcome fetch_confirmed_assessment(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& import_record_id);

enum class AssessmentServiceStatus {
    Completed,
    Blocked,
    NotFound,
    LockRejected,
};

struct AssessmentServiceOutcome {
    AssessmentServiceStatus status{AssessmentServiceStatus::Blocked};
    AssessmentPreview preview;
};

class AssessmentService {
public:
    AssessmentService(
        drogon::orm::DbClientPtr db_client,
        std::shared_ptr<const standards::StandardRegistry> registry);

    AssessmentServiceOutcome preview(
        const std::string& import_record_id,
        const AssessmentPreviewPayload& payload,
        const db::AuthUser& user,
        const std::string& edit_lock_token) const;

private:
    drogon::orm::DbClientPtr db_client_;
    std::shared_ptr<const standards::StandardRegistry> registry_;
};

}  // namespace bridge_report::assessment
