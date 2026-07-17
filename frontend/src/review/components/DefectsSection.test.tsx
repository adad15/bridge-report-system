import { render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { data } from "../testFixtures";
import { DefectsSection } from "./DefectsSection";

describe("DefectsSection", () => {
  it("disables editable controls but keeps photo viewing available in a read-only review", () => {
    const draft = data();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" selectedCandidateId="defect_0001" selectedPhotoCandidateId="photo_0001" onSelect={vi.fn()} dispatch={vi.fn()} disabled />);

    // 编辑类控件（字段输入、下拉、照片操作按钮）全部禁用。
    const editableControls = [...screen.getAllByRole("textbox"), ...screen.getAllByRole("combobox")];
    expect(editableControls.length).toBeGreaterThan(0);
    editableControls.forEach((control) => expect(control).toBeDisabled());
    expect(screen.getByRole("button", { name: "关联到病害" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "确认本组" })).toBeDisabled();

    // 查看类控件不禁用：查看是只读动作，已确认后仍要能看照片。
    expect(screen.getByRole("button", { name: /收起照片|查看照片/ })).toBeEnabled();
    screen.getAllByRole("button", { name: /^查看(待处理)?照片 / }).forEach((thumbnail) => {
      expect(thumbnail).toBeEnabled();
    });
  });

  it("locks defects outside the reopen scope while keeping warning defects editable", () => {
    const draft = data();
    const [first] = draft.defects;
    // 第一条病害带警告（可编辑），克隆出第二条无警告（应锁定）。
    draft.defects = [
      { ...first, warnings: [{ code: "w", message: "警告", severity: "warning" }] },
      { ...first, candidate_id: "defect_0002", warnings: [] },
    ];

    render(
      <DefectsSection
        draft={draft}
        importRecordId="record-1"
        baseUrl="http://backend"
        selectedCandidateId={null}
        onSelect={vi.fn()}
        dispatch={vi.fn()}
        isDefectEditable={(defect) => defect.warnings.length > 0}
      />
    );

    const locationInputs = screen.getAllByRole("textbox", { name: "位置" });
    expect(locationInputs).toHaveLength(2);
    expect(locationInputs[0]).toBeEnabled();
    expect(locationInputs[1]).toBeDisabled();
    expect(screen.getByText("病害 1")).toBeInTheDocument();
    expect(screen.getByText("病害 2")).toBeInTheDocument();
  });
});
