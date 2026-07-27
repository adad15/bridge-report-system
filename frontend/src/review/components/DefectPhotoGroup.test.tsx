import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { BridgeAnnualInspectionData, DefectCandidate, PhotoCandidate } from "../../contracts/annualInspection";
import { DefectPhotoGroup } from "./DefectPhotoGroup";

function defect(): DefectCandidate {
  return {
    candidate_id: "defect_0001", source_structure_part: "上部结构", component_name: "主梁", component_number: "2-1#梁",
    bridge_component_id: "component-1", standard_component_category_id: "category-1", resolved_structure_part: "上部结构",
    standard_defect_indicator_id: "h21.defect.crack", defect_type: "裂缝", defect_location: "第二跨", defect_description: "梁底裂缝", quantity_text: "1处",
    measurement_text: "L=0.8m", measurements: [], photo_references: [
      { photo_number: "2.1-1", resolution: "matched", photo_candidate_id: "photo_0001", resolved_defect_candidate_id: "defect_0001", review_note: null },
      { photo_number: "2.1-2", resolution: "matched", photo_candidate_id: "photo_0002", resolved_defect_candidate_id: "defect_0001", review_note: null },
    ],
    group_review_status: "待确认", severity: null, remark: null,
    source_ref: {}, confidence: 0.9, review_status: "已确认", review_note: "现场复核", warnings: [],
  };
}

function photo(id: string, number: string): PhotoCandidate {
  return {
    candidate_id: id, photo_number: number, linked_defect_candidate_id: "defect_0001",
    extracted_file: { temporary_file_name: `${id}.jpg`, original_caption: `${number} 主梁裂缝`, archive_relative_path: `photos/${id}.jpg` },
    match_status: "已确认", source_ref: {}, confidence: 0.9, review_status: "已确认", warnings: [],
  };
}

function data(): BridgeAnnualInspectionData {
  return {
    contract: { name: "BridgeAnnualInspectionData", version: "3.0", generated_at: "2026-07-12", producer: "test", parser_name: "test", parser_version: "3" },
    import_context: { source_type: "软件导出Word", file_role: "当前年度检测资料", archived_file_system_number: "GDWJ-1", import_record_system_number: "DRJL-1" },
    bridge_check: { selected_bridge_system_number: "QL-1", match_status: "匹配", warnings: [] },
    inspection: { inspection_year: 2026, inspection_date: "2026-07-12", report_number: "R-1", project_name: "测试", data_role: "当前年度" },
    defects: [defect()], photos: [photo("photo_0001", "2.1-1"), photo("photo_0002", "2.1-2")],
    comparison_candidates: [], report_text_candidates: [], warnings: [], errors: [],
  };
}

describe("DefectPhotoGroup", () => {
  it("shows required disease fields without rendering the internal structure part", () => {
    const draft = data();
    draft.defects[0].component_number = "2-1#板";
    render(
      <table><DefectPhotoGroup draft={draft} defect={draft.defects[0]} importRecordId="record-1" baseUrl="http://backend" expanded onToggle={vi.fn()} dispatch={vi.fn()} /></table>
    );

    for (const label of ["构件类别", "构件编号", "位置", "病害类型", "病害描述", "数量", "尺寸原文", "照片编号", "校对状态", "备注"]) {
      expect(screen.getByLabelText(label)).toBeInTheDocument();
    }
    expect(screen.queryByLabelText("结构部位")).not.toBeInTheDocument();
    expect(screen.getByLabelText("构件编号")).toHaveValue("2-1#板");
    expect(screen.getAllByRole("img", { name: /照片/ }).filter((item) => item.classList.contains("active"))).toHaveLength(1);
  });

  it("dispatches delete_defect from the explicit delete control", () => {
    vi.spyOn(window, "confirm").mockReturnValue(true);
    const draft = data();
    const dispatch = vi.fn();
    render(
      <table><DefectPhotoGroup draft={draft} defect={draft.defects[0]} importRecordId="record-1" baseUrl="http://backend" expanded={false} onToggle={vi.fn()} dispatch={dispatch} allowDelete /></table>
    );

    fireEvent.click(screen.getByRole("button", { name: "删除病害" }));
    expect(dispatch).toHaveBeenCalledWith({ type: "delete_defect", candidateId: "defect_0001" });
  });

  it("switches the large photo without dispatching a business action", () => {
    const draft = data();
    const dispatch = vi.fn();
    render(
      <table><DefectPhotoGroup draft={draft} defect={draft.defects[0]} importRecordId="record-1" baseUrl="http://backend" expanded onToggle={vi.fn()} dispatch={dispatch} /></table>
    );

    fireEvent.click(screen.getByRole("button", { name: "查看照片 2.1-2" }));

    expect(screen.getByRole("img", { name: "照片 2.1-2" })).toHaveClass("active");
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("uses an explicit button to open the photo review", () => {
    const draft = data();
    const onToggle = vi.fn();
    render(
      <table><DefectPhotoGroup draft={draft} defect={draft.defects[0]} importRecordId="record-1" baseUrl="http://backend" expanded={false} onToggle={onToggle} dispatch={vi.fn()} /></table>
    );

    fireEvent.click(screen.getByRole("button", { name: "查看照片（2）" }));
    expect(onToggle).toHaveBeenCalledOnce();
  });
});
