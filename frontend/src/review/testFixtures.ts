import type { BridgeAnnualInspectionData, BridgeAnnualInspectionDataV2 } from "../contracts/annualInspection";
import { projectVersionTwoForLegacyReview } from "../contracts/annualInspection";

export function data(): BridgeAnnualInspectionData {
  const wire: BridgeAnnualInspectionDataV2 = {
    contract: { name: "BridgeAnnualInspectionData", version: "2.0", generated_at: "2026-07-12", producer: "test", parser_name: "test", parser_version: "2" },
    import_context: { source_type: "软件导出Word", file_role: "当前年度检测资料", archived_file_system_number: "GDWJ-1", import_record_system_number: "DRJL-1" },
    bridge_check: { selected_bridge_system_number: "QL-1", match_status: "匹配", warnings: [] },
    inspection: { inspection_year: 2026, inspection_date: "2026-07-12", report_number: "R-1", project_name: "测试", data_role: "当前年度" },
    defects: [{ candidate_id: "defect_0001", source_structure_part: "上部结构", component_name: "主梁", component_number: "2-1#梁", bridge_component_id: null, standard_component_category_id: null, resolved_structure_part: null, defect_type: "裂缝", defect_location: "第二跨", defect_scale: 2, defect_description: "梁底裂缝", quantity_text: "1处", measurement_text: "L=0.8m", measurements: [], photo_numbers: ["2.1-1"], group_review_status: "待确认", confirmed_missing_photo_numbers: [], severity: null, remark: null, source_ref: { source_type: "word" }, confidence: 0.9, review_status: "已确认", review_note: null, warnings: [] }],
    photos: [{ candidate_id: "photo_0001", photo_number: "2.1-1", linked_defect_candidate_id: "defect_0001", extracted_file: { temporary_file_name: "photo.jpg", original_caption: "主梁裂缝", archive_relative_path: "photos/photo.jpg" }, match_status: "已确认", source_ref: {}, confidence: 0.9, review_status: "已确认", warnings: [] }],
    comparison_candidates: [], report_text_candidates: [], warnings: [], errors: [],
  };
  return projectVersionTwoForLegacyReview(wire);
}
