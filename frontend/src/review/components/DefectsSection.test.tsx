import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchLatestComponentInventory } from "../../api/componentInventoryApi";
import { data } from "../testFixtures";
import { DefectsSection } from "./DefectsSection";

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...actual, fetchLatestComponentInventory: vi.fn() };
});

const mockedFetchInventory = vi.mocked(fetchLatestComponentInventory);

describe("DefectsSection", () => {
  beforeEach(() => {
    mockedFetchInventory.mockReset();
  });

  it("adds a manual defect from an actual mapped component and allows an empty scale", async () => {
    mockedFetchInventory.mockResolvedValue({
      id: "revision-1",
      bridge_id: "bridge-1",
      revision_number: 1,
      status: "confirmed",
      baseline_revision_id: null,
      confirmed_at: null,
      entries: [{
        id: "entry-1",
        bridge_component_id: "component-1",
        component_number: "1-1#梁",
        site_name: "主梁",
        site_component_type: "主梁",
        span_or_location: null,
        is_active: true,
        deactivated_at: null,
        deactivation_reason: null,
        sort_order: 1,
        remarks: null,
        is_referenced: false,
        mappings: [{
          id: "mapping-1",
          standard_package_id: "package-1",
          standard_bridge_type_id: "bridge-type-1",
          standard_component_category_id: "h21.component.beam",
          structure_part: "superstructure",
          mapping_source: "template",
          confirmation_status: "confirmed",
          is_active: true,
        }],
      }],
    });
    const dispatch = vi.fn();
    const draft = data();
    draft.defects = [];

    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId={null} onSelect={vi.fn()} dispatch={dispatch} allowStructureChanges />);
    fireEvent.click(screen.getByRole("button", { name: "新增病害" }));
    await waitFor(() => expect(screen.getByLabelText("实际构件")).toHaveValue("entry-1"));
    fireEvent.change(screen.getByLabelText("新增病害位置"), { target: { value: "第1跨梁底" } });
    fireEvent.change(screen.getByLabelText("新增病害类型"), { target: { value: "裂缝" } });
    fireEvent.change(screen.getByLabelText("新增病害描述"), { target: { value: "梁底纵向裂缝" } });
    fireEvent.click(screen.getByRole("button", { name: "添加病害" }));

    expect(dispatch).toHaveBeenCalledWith({
      type: "add_defect",
      input: {
        componentName: "主梁",
        componentNumber: "1-1#梁",
        bridgeComponentId: "component-1",
        standardComponentCategoryId: "h21.component.beam",
        resolvedStructurePart: "上部结构",
        defectLocation: "第1跨梁底",
        defectType: "裂缝",
        defectDescription: "梁底纵向裂缝",
        defectScale: null,
      },
    });
  });
  it("disables editable controls but keeps photo viewing available in a read-only review", () => {
    const draft = data();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId="defect_0001" selectedPhotoCandidateId="photo_0001" onSelect={vi.fn()} dispatch={vi.fn()} disabled />);

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
        bridgeId="bridge-1"
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
