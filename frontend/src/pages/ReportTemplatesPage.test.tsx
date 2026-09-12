import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import {
  fetchReportTemplates,
  uploadReportTemplate,
  type ReportTemplate,
} from "../api/reportApi";
import { useAuth } from "../auth/AuthContext";
import { ReportTemplatesPage } from "./ReportTemplatesPage";

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
    fetchReportTemplates: vi.fn(),
    uploadReportTemplate: vi.fn(),
  };
});

const template = (overrides: Partial<ReportTemplate> = {}): ReportTemplate => ({
  id: "t1",
  template_code: "PERIODIC_INSPECTION_V1",
  template_name: "定期检测报告标准模板",
  description: null,
  contract_type: "periodic_inspection_v1",
  file_id: "f1",
  file_checksum: "sha256:abc",
  file_name: "periodic-inspection-v1.docx",
  contract_config: {
    table_number_formats: { "COMPONENT_WEIGHTS": "表4.1-{n}" },
    required_personnel_roles: ["approver", "compiler"],
  },
  validation_status: "valid",
  validation_result: {
    status: "valid",
    issues: [],
    anchors_in_document_order: ["BRIDGE_PROFILE", "DEFECT_TABLES:SUPERSTRUCTURE"],
    placeholders_used: [],
    fields_used: ["TOC"],
  },
  is_enabled: true,
  is_default: true,
  updated_by_display_name: "管理员",
  updated_at: "2026-09-11 10:00:00+08",
  usage_count: 0,
  can_delete: true,
  can_disable: false,
  ...overrides,
});

describe("ReportTemplatesPage", () => {
  beforeEach(() => {
    vi.mocked(useAuth).mockReturnValue({
      user: { username: "admin", display_name: "管理员", role: "admin" },
    } as never);
    vi.mocked(fetchReportTemplates).mockResolvedValue([template()]);
  });

  it("shows upload and management actions only to administrators", async () => {
    const { unmount } = render(<ReportTemplatesPage />);
    expect(await screen.findByRole("button", { name: /上传模板/ })).toBeInTheDocument();
    unmount();

    vi.mocked(useAuth).mockReturnValue({
      user: { username: "u", display_name: "普通用户", role: "normal" },
    } as never);
    render(<ReportTemplatesPage />);
    await screen.findByText("定期检测报告标准模板");
    expect(screen.queryByRole("button", { name: /上传模板/ })).not.toBeInTheDocument();
  });

  // 设计 §17.4：被年度配置引用的模板只能停用。界面直接禁掉按钮，不让用户点下去撞外键。
  it("disables delete for a template that a year still references", async () => {
    vi.mocked(fetchReportTemplates).mockResolvedValue([
      template({ usage_count: 3, can_delete: false }),
    ]);
    render(<ReportTemplatesPage />);

    const row = (await screen.findByText("定期检测报告标准模板")).closest("tr");
    expect(row).not.toBeNull();
    expect(within(row as HTMLElement).getByRole("button", { name: "删除" })).toBeDisabled();
    expect(within(row as HTMLElement).getByText("3 个年度")).toBeInTheDocument();
  });

  // 默认模板不能直接停用——先把默认让给别的模板，否则普通用户会选不到任何模板。
  it("does not offer to disable the default template", async () => {
    render(<ReportTemplatesPage />);

    const row = (await screen.findByText("定期检测报告标准模板")).closest("tr");
    expect(within(row as HTMLElement).getByRole("button", { name: "停用" })).toBeDisabled();
    expect(within(row as HTMLElement).queryByRole("button", { name: "设为默认" })).toBeNull();
  });

  // 校验不通过时只说一句"模板不合契约"，管理员无从下手；明细必须逐条摆出来（§21.1）。
  it("lists every validation issue when an upload is rejected", async () => {
    vi.mocked(uploadReportTemplate).mockRejectedValue(
      new ApiError("report_template_invalid", "模板未通过契约校验。", {
        details: {
          code: "report_template_invalid",
          validation_result: {
            issues: [
              {
                code: "template_anchor_missing",
                message: "模板缺少内容锚点 CONCLUSION。",
                severity: "error",
              },
              {
                code: "template_field_rejected",
                message: "模板使用了被禁止的域 SEQ。",
                severity: "error",
                location: "body#42",
              },
            ],
          },
        },
      }),
    );
    render(<ReportTemplatesPage />);
    await userEvent.click(await screen.findByRole("button", { name: /上传模板/ }));

    await userEvent.type(screen.getByPlaceholderText("PERIODIC_INSPECTION_V2"), "T2");
    await userEvent.type(screen.getByPlaceholderText("定期检测报告标准模板"), "第二版");
    const file = new File(["docx"], "t.docx", {
      type: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    });
    const input = document.querySelector('input[type="file"]') as HTMLInputElement;
    await userEvent.upload(input, file);

    await userEvent.click(screen.getByRole("button", { name: "上传并校验" }));

    expect(await screen.findByText("模板缺少内容锚点 CONCLUSION。")).toBeInTheDocument();
    expect(screen.getByText("模板使用了被禁止的域 SEQ。")).toBeInTheDocument();
    expect(screen.getByText("body#42")).toBeInTheDocument();
  });

  it("shows the anchors in document order in the detail drawer", async () => {
    render(<ReportTemplatesPage />);
    await userEvent.click(await screen.findByRole("button", { name: "锚点" }));

    const drawer = await screen.findByRole("dialog");
    expect(within(drawer).getByText("BRIDGE_PROFILE")).toBeInTheDocument();
    expect(within(drawer).getByText("DEFECT_TABLES")).toBeInTheDocument();
    expect(within(drawer).getByText("上部结构")).toBeInTheDocument();
  });
});
