#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/review/ConfirmPlan.hpp"
#include "bridge_report/review/DraftValidation.hpp"
#include "bridge_report/review/ReviewModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::db {

/**
 * @brief confirm_annual_facts 的写入计数：各事实表本次事务实际插入的行数。
 */
struct ConfirmWrittenCounts {
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
    int assessment_component_results{0};
    int assessment_part_results{0};
    int assessment_control_results{0};
    int assessment_rule_traces{0};
};

/**
 * @brief confirm_annual_facts 的结果：成功时携带目标年度信息与写入计数；
 * 失败时 error_code 为下列之一，事务已回滚，未产生任何写入：
 *   - "import_record_wrong_status"：重新加锁读取到的导入记录状态不是"待校对"。
 *   - "revision_confirmation_required"：桥梁+年度已存在"已确认+当前版本"记录，
 *     且调用方未显式传入 confirm_revision=true。
 *   - "db_write_failed"：事务执行期间抛出数据库异常（如 check 约束冲突），error_message
 *     携带异常摘要，供路由层原样透出。
 */
struct ConfirmOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    std::string inspection_year_id;
    int version_number{0};
    std::string assessment_run_id;
    ConfirmWrittenCounts written;
    Json::Value preflight_details;
};

struct PhotoContentRef {
    std::string archived_file_id;
    std::string storage_relative_path;
    std::string content_type;
};

/**
 * @brief save_review_draft() 的输入：请求体加上写入时需要的操作者身份与编辑锁。
 *
 * draft 是**未加工**的请求体（须已通过 validate_review_draft 的契约校验）。
 * 规范化、证据校验、构件与评定树关联校验都在仓储事务内完成，因为它们依赖的
 * 存量草稿、年度锁定版本与规范组合必须与写入取自同一个事务快照。
 */
struct SaveReviewDraftInput {
    std::string import_record_id;
    Json::Value draft;
    std::string actor_username;
    bool actor_is_admin{false};
    std::optional<EditLockCredentials> edit_lock;
};

/**
 * @brief save_review_draft() 的结果。失败时事务已回滚，草稿与年度版本均未改变。
 *
 * error_code 取值与路由映射：
 *   - "import_record_not_found"：加锁读取时记录已不存在 -> 404。
 *   - "import_record_not_editable"：状态已不是"待校对" -> 409。
 *   - "edit_lock_invalid"：编辑锁在事务内已失效 -> 409。
 *   - "contract_version_outdated"：存量草稿仍是旧版合同 -> 409。
 *   - "component_inventory_revision_changed"：整份草稿一致地引用了旧台账版本，
 *     或年度版本被并发锁到别的版本 -> 409，**整体提示一次，不逐条**。
 *   - "component_inventory_unavailable"：有绑定病害，但本年度解析不出可用的已确认
 *     台账版本（年度锁在草稿版本/别的桥，或桥上没有已确认台账）-> 409。
 *   - "rating_tree_required" / "rating_tree_unavailable"：年度未锁定评定树或树不可用 -> 409。
 *   - "forbidden"：full 重开态下非管理员保存 -> 403。
 *   - 校验类失败：error_code 与 validation.code 相同（defect_component_assignment_invalid /
 *     defect_rating_tree_assignment_invalid / imported_evidence_modified /
 *     reopen_scope_violation），逐项问题在 validation.issues 里 -> 400。
 *   - "db_write_failed"：事务内抛出数据库异常 -> 500。
 *   - "database_commit_failed"：提交回调失败 -> 500。
 */
struct SaveReviewDraftOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    review::DraftValidationResult validation;
};

/**
 * @brief 桥梁 -> 年度 -> 导入记录导航只读查询，以及唯一的年度事实入库写入口。
 *
 * 注意：内部使用 execSqlSync，会阻塞调用方所在线程；
 * 本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
 */
class ReviewRepository {
public:
    explicit ReviewRepository(
        drogon::orm::DbClientPtr db_client,
        std::shared_ptr<const standards::StandardRegistry> standard_registry = nullptr);

    std::vector<review::BridgeSummary> list_bridges();
    std::vector<review::InspectionYearSummary> list_inspection_years(const std::string& bridge_id);
    std::vector<review::ImportRecordSummary> list_import_records(const std::string& bridge_id);

    /**
     * @brief 联查 import_records + bridges + inspection_years（左联，年度可空），
     * 供 GET /api/import-records/{import_record_id}/review 使用。
     */
    std::optional<review::ImportRecordDetail> get_import_record_detail(const std::string& import_record_id);
    std::optional<PhotoContentRef> get_photo_content_ref(
        const std::string& import_record_id,
        const std::string& photo_candidate_id
    );

    /**
     * @brief 判断桥梁在指定年度是否存在“已确认 + 当前版本”的年度检查记录。
     */
    bool has_current_annual_facts(const std::string& bridge_id, int inspection_year);

    /**
     * @brief 保存校对草稿的生产入口：在单个事务内完成解析、校验、年度版本锁定与写入。
     *
     * 事务内按 import_records -> inspection_years 的顺序加行锁（与绑定、Word 导入和
     * 确认事务一致），并**重新读取**存量草稿、年度状态与锁定台账版本、规范组合与已发布
     * 评定树——只把写入搬进事务、却沿用事务外读到的数据，旧快照照样能覆盖新数据。
     *
     * 台账版本按"年度锁定优先、否则该桥最新已确认"解析，与绑定写入病害时同一条规则；
     * 草稿含构件绑定且年度尚未锁定版本时，本次解析出的版本会一并锁进年度。
     *
     * @note 必须在 db_client_ 为"裸" DbClient（而非另一个 Transaction）时调用。
     */
    SaveReviewDraftOutcome save_review_draft(const SaveReviewDraftInput& input);

    /**
     * @brief 低层草稿写入：只覆盖 parsed_result_json，**不做任何校验，也不锁定年度版本**。
     *
     * 更新条件带 import_status = '待校对' 谓词，防止处理器加载记录后状态被并发
     * 改为已取消/已确认时草稿仍写入（TOCTOU）。返回 false 表示记录不存在或状态已不可编辑。
     *
     * 生产路径一律走上面的 save_review_draft(SaveReviewDraftInput)；这一条留给测试
     * 夹具播种既有草稿，它可以在事务客户端上调用，而结构化版本必须自己开事务。
     *
     * @param parsed_json_text 完整的 BridgeAnnualInspectionData JSON 文本。
     */
    bool save_review_draft(
        const std::string& import_record_id,
        const std::string& parsed_json_text,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt,
        const std::string& defect_change_audit_json = ""
    );

    /**
     * @brief 取消导入记录：状态为 已上传/解析中/待校对/解析失败 时更新为 已取消 并返回 true；
     * 状态已是 已确认/已取消 时不更新，返回 false。
     * 重开校对中的记录（reopened_at 非空）也拒绝取消——它背后已有正式事实，
     * 只能「放弃修改」恢复已确认或重新确认修订版。
     */
    bool cancel_import_record(
        const std::string& import_record_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt
    );

    /**
     * @brief 重开校对：已确认记录翻回待校对，记录审计现场并快照当前草稿
     * （供「放弃修改」精确还原）。谓词带 import_status='已确认'，并发安全同 cancel。
     *
     * @param scope 'warnings_only'（仅警告病害可改）或 'full'（管理员全改），由路由层校验。
     * @return false 表示记录不存在或状态已不是已确认。
     */
    bool reopen_import_record(
        const std::string& import_record_id,
        const std::string& scope,
        const std::string& username
    );

    /**
     * @brief 放弃重开修改：草稿还原为重开时的快照，状态翻回已确认，清空重开列。
     * 仅当记录处于"待校对 + 重开态"时生效；正式事实表从未被重开触碰，无需回滚。
     */
    bool restore_reopened_import_record(
        const std::string& import_record_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt
    );

    /**
     * @brief 唯一的年度事实入库写入口：在单个 drogon::orm::Transaction 内完成状态重校验、
     * 锁定并读取最新 parsed_result_json，在事务内完成严格契约校验、预检、写计划构造、
     * 照片归档 ID 解析、修订判定与事实写入。只有 commitCallback 明确成功才返回 success=true。
     *
     * @param import_record_id 目标导入记录 id（须为合法 uuid，由调用方保证）。
     * @param confirm_revision 是否显式确认写入修订版（同桥同年已有当前有效事实时生效）。
     * @param confirmation_note 随导入记录 validation_result_json 落盘的确认备注，可为空串。
     *
     * @note 本方法必须在 db_client_ 为“裸” DbClient（而非另一个 Transaction）时调用——
     * 内部通过 db_client_->newTransaction() 开启自己的事务，事务套事务不受支持。
     */
    ConfirmOutcome confirm_annual_facts(
        const std::string& import_record_id,
        bool confirm_revision,
        const std::string& confirmation_note,
        const std::string& confirmed_by_user_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt
    );

private:
    drogon::orm::DbClientPtr db_client_;
    std::shared_ptr<const standards::StandardRegistry> standard_registry_;
};

}  // 命名空间 bridge_report::db
