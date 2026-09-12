import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  fetchReportEquipment,
  fetchReportPersonnel,
  setReportPersonnelEnabled,
  type ReportEquipment,
  type ReportPersonnel,
} from "../api/reportApi";
import { useAuth } from "../auth/AuthContext";
import { ReportDirectoryPage } from "./ReportDirectoryPage";

class ResizeObserverMock implements ResizeObserver {
  observe() {}
  unobserve() {}
  disconnect() {}
}

Object.defineProperty(globalThis, "ResizeObserver", {
  writable: true,
  value: ResizeObserverMock,
});

vi.mock("../auth/AuthContext", () => ({ useAuth: vi.fn() }));

vi.mock("../api/reportApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/reportApi")>();
  return {
    ...original,
    fetchReportPersonnel: vi.fn(),
    fetchReportEquipment: vi.fn(),
    setReportPersonnelEnabled: vi.fn(),
  };
});

const person = (overrides: Partial<ReportPersonnel> = {}): ReportPersonnel => ({
  id: "p1",
  full_name: "张三",
  organization: "某某设计院",
  job_title: null,
  professional_title: "教授级高工",
  qualification_certificate_no: "JC-001",
  phone: null,
  email: null,
  remarks: null,
  is_enabled: true,
  assignment_count: 0,
  updated_at: "2026-09-11 10:00:00+08",
  ...overrides,
});

const equipment = (overrides: Partial<ReportEquipment> = {}): ReportEquipment => ({
  id: "e1",
  equipment_name: "裂缝观测仪",
  model_spec: "ZBL-F130",
  asset_number: "SB-01",
  measurement_range: "0-6mm",
  accuracy: "0.01mm",
  calibration_certificate_no: "JD-1",
  calibration_valid_until: "2027-01-31",
  remarks: null,
  is_enabled: true,
  assignment_count: 0,
  updated_at: "2026-09-11 10:00:00+08",
  ...overrides,
});

describe("ReportDirectoryPage", () => {
  beforeEach(() => {
    vi.mocked(useAuth).mockReturnValue({
      user: { username: "admin", display_name: "管理员", role: "admin" },
    } as never);
    vi.mocked(fetchReportPersonnel).mockResolvedValue([person()]);
    vi.mocked(fetchReportEquipment).mockResolvedValue([equipment()]);
  });

  // 设计 §15.3：被年度配置引用的人不许硬删，删掉会让历史报告配置指向一个不存在的人。
  it("disables delete for a person a year still references", async () => {
    vi.mocked(fetchReportPersonnel).mockResolvedValue([person({ assignment_count: 2 })]);
    render(<ReportDirectoryPage />);

    const row = (await screen.findByText("张三")).closest("tr") as HTMLElement;
    expect(within(row).getByRole("button", { name: "删除" })).toBeDisabled();
    expect(within(row).getByText("被引用 2")).toBeInTheDocument();
  });

  // 停用项仍然列出来，只是标出来——历史配置要看得见。
  it("keeps a disabled person visible and offers to re-enable them", async () => {
    vi.mocked(fetchReportPersonnel).mockResolvedValue([person({ is_enabled: false })]);
    vi.mocked(setReportPersonnelEnabled).mockResolvedValue(person());
    render(<ReportDirectoryPage />);

    const row = (await screen.findByText("张三")).closest("tr") as HTMLElement;
    expect(within(row).getByText("停用")).toBeInTheDocument();
    await userEvent.click(within(row).getByRole("button", { name: "启用" }));
    expect(setReportPersonnelEnabled).toHaveBeenCalledWith("p1", true);
  });

  it("hides every write action from a non-administrator", async () => {
    vi.mocked(useAuth).mockReturnValue({
      user: { username: "u", display_name: "普通用户", role: "normal" },
    } as never);
    render(<ReportDirectoryPage />);

    await screen.findByText("张三");
    expect(screen.queryByRole("button", { name: /新增人员/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "编辑" })).not.toBeInTheDocument();
  });

  // 检定过期不阻断保存，但必须显眼——报告里会如实印出这台设备（设计 §21.3）。
  it("marks equipment whose calibration has expired", async () => {
    vi.mocked(fetchReportEquipment).mockResolvedValue([
      equipment({ calibration_valid_until: "2020-01-31" }),
    ]);
    render(<ReportDirectoryPage />);

    await userEvent.click(screen.getByRole("tab", { name: "检测设备" }));
    const row = (await screen.findByText("裂缝观测仪")).closest("tr") as HTMLElement;
    expect(within(row).getByText("已过期")).toBeInTheDocument();
  });
});
