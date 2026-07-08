#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/review/ConfirmPlan.hpp"
#include "bridge_report/review/ReviewModels.hpp"

namespace bridge_report::db {

/**
 * @brief confirm_annual_facts 的写入计数：各事实表本次事务实际插入的行数。
 */
struct ConfirmWrittenCounts {
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
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
    ConfirmWrittenCounts written;
};

/**
 * @brief 桥梁 -> 年度 -> 导入记录导航只读查询，以及唯一的年度事实入库写入口。
 *
 * 注意：内部使用 execSqlSync，会阻塞调用方所在线程；
 * 本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
 */
class ReviewRepository {
public:
    explicit ReviewRepository(drogon::orm::DbClientPtr db_client);

    std::vector<review::BridgeSummary> list_bridges();
    std::vector<review::InspectionYearSummary> list_inspection_years(const std::string& bridge_id);
    std::vector<review::ImportRecordSummary> list_import_records(const std::string& bridge_id);

    /**
     * @brief 联查 import_records + bridges + inspection_years（左联，年度可空），
     * 供 GET /api/import-records/{import_record_id}/review 使用。
     */
    std::optional<review::ImportRecordDetail> get_import_record_detail(const std::string& import_record_id);

    /**
     * @brief 判断桥梁在指定年度是否存在“已确认 + 当前版本”的年度检查记录。
     */
    bool has_current_annual_facts(const std::string& bridge_id, int inspection_year);

    /**
     * @brief 保存校对草稿：覆盖 parsed_result_json，import_status 保持不变。
     *
     * 更新条件带 import_status = '待校对' 谓词，防止处理器加载记录后状态被并发
     * 改为已取消/已确认时草稿仍写入（TOCTOU）。返回 false 表示记录不存在或状态已不可编辑。
     * @param parsed_json_text 完整的 BridgeAnnualInspectionData JSON 文本。
     */
    bool save_review_draft(const std::string& import_record_id, const std::string& parsed_json_text);

    /**
     * @brief 取消导入记录：状态为 已上传/解析中/待校对/解析失败 时更新为 已取消 并返回 true；
     * 状态已是 已确认/已取消 时不更新，返回 false。
     */
    bool cancel_import_record(const std::string& import_record_id);

    /**
     * @brief 唯一的年度事实入库写入口：在单个 drogon::orm::Transaction 内完成状态重校验、
     * 修订判定、年度行终态更新、构件 upsert 与四类事实表插入，全部成功才提交，任一步失败
     * （业务拒绝或数据库异常）都显式回滚，不留部分写入。
     *
     * @param import_record_id 目标导入记录 id（须为合法 uuid，由调用方保证）。
     * @param plan build_confirm_plan 产出的写计划（调用方保证已通过入库前检查）。
     * @param inspection_year 有效检测年度（resolve_effective_inspection_year 的结果）。
     * @param confirm_revision 是否显式确认写入修订版（同桥同年已有当前有效事实时生效）。
     * @param confirmation_note 随导入记录 validation_result_json 落盘的确认备注，可为空串。
     *
     * @note 本方法必须在 db_client_ 为“裸” DbClient（而非另一个 Transaction）时调用——
     * 内部通过 db_client_->newTransaction() 开启自己的事务，事务套事务不受支持。
     */
    ConfirmOutcome confirm_annual_facts(
        const std::string& import_record_id,
        const review::ConfirmPlan& plan,
        int inspection_year,
        bool confirm_revision,
        const std::string& confirmation_note
    );

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // 命名空间 bridge_report::db
