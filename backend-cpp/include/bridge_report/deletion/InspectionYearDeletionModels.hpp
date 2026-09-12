#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::deletion {

struct DeletionCounts {
    int inspection_versions{0};
    int import_records{0};
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
    int archived_files_to_delete{0};
    int temporary_source_files_to_delete{0};
    int shared_files_retained{0};
    int defect_threads_affected{0};
    int defect_comparisons{0};
    // 评定运行：assessment_runs.inspection_year_id 是 on delete restrict，不先删它
    // 就删不掉年度。拆成两个计数是因为两者的处置完全不同——"正式+成功"的评定由
    // protect_completed_formal_assessment_run 保护为不可删，有它在就整单拒绝。
    int assessment_runs{0};
    int formal_assessment_runs{0};
    // 报告配置随年度级联删除，必须进入预览计数，否则用户看不到自己会失去什么
    // （报告设计 §17.4）。
    int report_settings{0};
    int report_personnel_assignments{0};
    int report_equipment_assignments{0};
    // 生成任务同样级联，但进行中的任务要整单拒绝：它正拿着模板副本和临时文件，
    // 底下的年度被删掉只会让它以看不懂的方式失败。
    int report_generation_jobs{0};
    int running_report_generation_jobs{0};
    // 其他年度的报告配置把本年度选作历史对比。该外键是 on delete set null，
    // 删除会静默清空它们的选择，必须提前告诉用户有多少份配置受影响。
    int report_comparison_references{0};

    Json::Value to_json() const;
};

struct ActiveDeletionLock {
    std::string import_record_id;
    std::string owner_username;
    std::string owner_display_name;
    std::string acquired_at;
    std::string expires_at;

    Json::Value to_json() const;
};

struct InspectionYearDeletionPlan {
    std::string bridge_id;
    std::string bridge_system_number;
    std::string bridge_name;
    int inspection_year{0};
    std::vector<int> version_numbers;
    DeletionCounts counts;
    std::vector<ActiveDeletionLock> active_edit_locks;

    // 仅供仓储层执行和并发指纹使用，绝不进入 API JSON。
    std::vector<std::string> inspection_year_ids;
    std::vector<std::string> import_record_ids;
    std::vector<std::string> defect_observation_ids;
    std::vector<std::string> condition_rating_ids;
    std::vector<std::string> assessment_run_ids;
    std::vector<std::string> defect_thread_ids;
    std::vector<std::string> defect_comparison_ids;
    std::vector<std::string> archived_file_ids_to_delete;
    std::vector<std::string> archived_file_relative_paths_to_delete;
    std::vector<std::string> temporary_source_file_ids_to_delete;
    std::vector<std::string> temporary_source_relative_paths_to_delete;
    std::vector<std::string> fingerprint_items;

    std::string confirmation_text() const;
    std::string impact_token() const;
    Json::Value to_public_json() const;
};

struct DeletionActorSnapshot {
    std::string user_id;
    std::string username;
    std::string display_name;
};

enum class DeleteInspectionYearStatus {
    Deleted,
    NotFound,
    Locked,
    ImpactChanged,
    /// 该年度存在已完成的正式评定。正式评定是不可变的业务记录，不能被删除顺手抹掉。
    FormalAssessmentPresent,
    /// 该年度有正在运行的报告生成任务（报告设计 §17.4）。等它终态化或被取消再删。
    ReportGenerationJobRunning,
    Failed,
};

struct DeleteInspectionYearOutcome {
    DeleteInspectionYearStatus status{DeleteInspectionYearStatus::Failed};
    std::optional<InspectionYearDeletionPlan> current_plan;
    std::optional<std::string> deletion_audit_id;
    std::optional<std::string> next_inspection_year_id;
};

}  // namespace bridge_report::deletion
