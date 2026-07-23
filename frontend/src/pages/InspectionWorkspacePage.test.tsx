import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchInspectionYears } from "../api/navigationApi";
import { clearCachedForTests } from "../api/resourceCache";
import { fetchInspectionWorkspace } from "../api/workspaceApi";
import { InspectionWorkspacePage } from "./InspectionWorkspacePage";

const reloadOverview = vi.fn();
const authState = vi.hoisted(() => ({ role: "admin" }));

vi.mock("../api/navigationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/navigationApi")>();
  return { ...original, fetchInspectionYears: vi.fn() };
});

vi.mock("../api/workspaceApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/workspaceApi")>();
  return { ...original, fetchInspectionWorkspace: vi.fn() };
});

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "tester", display_name: "测试用户", role: authState.role } }),
}));

vi.mock("../workspace/BridgeWorkspaceShell", () => ({
  useBridgeWorkspace: () => ({
    overview: { bridge: { id: "bridge-1", bridge_name: "百股大桥" } },
    reloadOverview,
  }),
}));

vi.mock("../workspace/ImportWordDialog", () => ({
  ImportWordDialog: ({ onChanged }: { onChanged: () => void }) => (
    <section aria-label="导入测试弹窗">
      <button type="button" onClick={onChanged}>模拟上传后刷新</button>
    </section>
  ),
}));

vi.mock("../workspace/DeleteImportRecordDialog", () => ({
  DeleteImportRecordDialog: ({ importRecordId }: { importRecordId: string }) => (
    <section aria-label="删除导入记录测试弹窗">{importRecordId}</section>
  ),
}));

describe("InspectionWorkspacePage", () => {
  beforeEach(() => {
    clearCachedForTests();  // 缓存是模块作用域的，不清会让用例顺序影响结果
    vi.resetAllMocks();
    authState.role = "admin";
    vi.mocked(fetchInspectionYears).mockResolvedValue([{
      id: "year-1", system_number: "NDJC-000001", inspection_year: 2024,
      status: "待校对", version_number: 1, is_current: true,
    }]);
    vi.mocked(fetchInspectionWorkspace).mockResolvedValue({
      bridge: { id: "bridge-1", system_number: "QL-000001", bridge_name: "百股大桥", route_name: "大养线", status: "在用" },
      inspection_year: { id: "year-1", system_number: "NDJC-000001", inspection_year: 2024, status: "待校对", version_number: 1, is_current: true, overall_score: null, overall_grade: null, created_at: null, updated_at: null },
      standard_profile: {
        id: "profile-1", revision_number: 1, status: "生效",
        technical_condition: { id: "technical-1", family: "technical_condition", standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准", official_edition: "2011", package_version: "1.0.0", is_enabled: true, sync_status: "正常" },
        maintenance: { id: "maintenance-1", family: "maintenance", standard_code: "JTG 5120—2021", standard_name: "公路桥涵养护规范", official_edition: "2021", package_version: "1.0.0", is_enabled: true, sync_status: "正常" },
      },
      imports: [],
      pending: { import_count: 0, unbound_observation_count: 0, total_count: 0 },
    });
  });

  it("shows the locked standard identities and package versions", async () => {
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes><Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} /></Routes>
      </MemoryRouter>
    );
    expect(await screen.findByText(/JTG\/T H21—2011.*规则包 1.0.0/)).toBeInTheDocument();
    expect(screen.getByText(/JTG 5120—2021.*规则包 1.0.0/)).toBeInTheDocument();
  });

  it("keeps the import dialog mounted while refreshing the annual workspace", async () => {
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes>
          <Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );

    await userEvent.click(await screen.findByRole("button", { name: "导入资料" }));
    expect(screen.getByRole("region", { name: "导入测试弹窗" })).toBeInTheDocument();

    vi.mocked(fetchInspectionWorkspace).mockImplementation(() => new Promise(() => undefined));
    await userEvent.click(screen.getByRole("button", { name: "模拟上传后刷新" }));

    expect(screen.getByRole("region", { name: "导入测试弹窗" })).toBeInTheDocument();
    expect(reloadOverview).not.toHaveBeenCalled();
  });

  it("shows the specific Python parsing error stored on a failed import", async () => {
    vi.mocked(fetchInspectionWorkspace).mockResolvedValue({
      bridge: { id: "bridge-1", system_number: "QL-000001", bridge_name: "百股大桥", route_name: "大养线", status: "在用" },
      inspection_year: { id: "year-1", system_number: "NDJC-000001", inspection_year: 2024, status: "待校对", version_number: 1, is_current: true, overall_score: null, overall_grade: null, created_at: null, updated_at: null },
      standard_profile: null,
      imports: [{
        id: "import-1", system_number: "DRJL-000001", import_name: "百股大桥报告.docx",
        source_type: "软件导出Word", import_status: "解析失败", importer_name: null,
        created_at: null, updated_at: null,
        error_message: "未识别到表4.1-2总体技术状况评定表。",
        temporary_source_status: "解析失败", temporary_source_expires_at: "2026-07-17T12:00:00+08:00",
        statistics: { defect_count: 0, photo_count: 0, rating_item_count: 0, pending_count: 0, confirmed_count: 0, modified_count: 0, ignored_count: 0, object_warning_count: 0 },
        edit_lock: null, available_action: "parse",
      }],
      pending: { import_count: 1, unbound_observation_count: 0, total_count: 1 },
    });

    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes>
          <Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );

    expect(await screen.findByText("解析失败：未识别到表4.1-2总体技术状况评定表。")).toBeInTheDocument();
  });

  it("opens the import deletion dialog from an import card for an administrator", async () => {
    vi.mocked(fetchInspectionWorkspace).mockResolvedValue({
      bridge: { id: "bridge-1", system_number: "QL-000001", bridge_name: "百股大桥", route_name: "大养线", status: "在用" },
      inspection_year: { id: "year-1", system_number: "NDJC-000001", inspection_year: 2024, status: "待校对", version_number: 1, is_current: true, overall_score: null, overall_grade: null, created_at: null, updated_at: null },
      standard_profile: null,
      imports: [{
        id: "import-1", system_number: "DRJL-000001", import_name: "百股大桥报告.docx",
        source_type: "软件导出Word", import_status: "待校对", importer_name: "liaoning-word-importer",
        created_at: null, updated_at: null, error_message: null,
        temporary_source_status: "已删除", temporary_source_expires_at: null,
        statistics: { defect_count: 25, photo_count: 31, rating_item_count: 15, pending_count: 6, confirmed_count: 88, modified_count: 0, ignored_count: 6, object_warning_count: 0 },
        edit_lock: null, available_action: "continue_review",
      }],
      pending: { import_count: 1, unbound_observation_count: 0, total_count: 1 },
    });
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes>
          <Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );

    await userEvent.click(await screen.findByRole("button", { name: "删除导入记录" }));
    expect(screen.getByRole("region", { name: "删除导入记录测试弹窗" })).toHaveTextContent("import-1");
  });

  it("does not show import deletion to a normal user", async () => {
    authState.role = "normal";
    vi.mocked(fetchInspectionWorkspace).mockResolvedValue({
      bridge: { id: "bridge-1", system_number: "QL-000001", bridge_name: "百股大桥", route_name: "大养线", status: "在用" },
      inspection_year: { id: "year-1", system_number: "NDJC-000001", inspection_year: 2024, status: "待校对", version_number: 1, is_current: true, overall_score: null, overall_grade: null, created_at: null, updated_at: null },
      standard_profile: null,
      imports: [{
        id: "import-1", system_number: "DRJL-000001", import_name: "百股大桥报告.docx",
        source_type: "软件导出Word", import_status: "待校对", importer_name: null,
        created_at: null, updated_at: null, error_message: null,
        temporary_source_status: null, temporary_source_expires_at: null,
        statistics: { defect_count: 1, photo_count: 0, rating_item_count: 0, pending_count: 1, confirmed_count: 0, modified_count: 0, ignored_count: 0, object_warning_count: 0 },
        edit_lock: null, available_action: "continue_review",
      }],
      pending: { import_count: 1, unbound_observation_count: 0, total_count: 1 },
    });
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes>
          <Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );

    expect(await screen.findByText("百股大桥报告.docx")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "删除导入记录" })).not.toBeInTheDocument();
  });
});
