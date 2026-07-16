import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { parseWordImport } from "../api/reviewApi";
import { uploadWordImport } from "../api/workspaceApi";
import { ImportWordDialog } from "./ImportWordDialog";

vi.mock("../api/reviewApi", () => ({ parseWordImport: vi.fn() }));
vi.mock("../api/workspaceApi", () => ({
  uploadWordImport: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

describe("ImportWordDialog", () => {
  beforeEach(() => vi.resetAllMocks());

  it("uploads first, then parses with the fixed annual context", async () => {
    vi.mocked(uploadWordImport).mockResolvedValue({ id: "import-1" } as never);
    vi.mocked(parseWordImport).mockResolvedValue({ parsed: true } as never);
    const onCompleted = vi.fn();
    render(<ImportWordDialog bridgeName="绕阳河二号桥" inspectionYearId="year-1" inspectionYear={2026} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={onCompleted} />);
    await userEvent.upload(screen.getByLabelText("Word 文件"), new File(["docx"], "报告.docx"));
    await userEvent.type(screen.getByLabelText("检查日期"), "2026-05-18");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-001");
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    expect(uploadWordImport).toHaveBeenCalledWith(expect.any(String), "year-1", expect.any(File), "软件导出Word");
    expect(parseWordImport).toHaveBeenCalledWith(expect.any(String), "import-1", expect.objectContaining({
      rule_profile: "辽宁国省干线",
      import_mode: "已有桥年度导入",
      inspection_date: "2026-05-18",
      report_number: "BG-001",
    }));
    expect(vi.mocked(uploadWordImport).mock.invocationCallOrder[0]).toBeLessThan(vi.mocked(parseWordImport).mock.invocationCallOrder[0]);
    expect(onCompleted).toHaveBeenCalledWith("import-1");
  });

  it("rejects a non-docx file without uploading", async () => {
    render(<ImportWordDialog bridgeName="测试桥" inspectionYearId="year-1" inspectionYear={2026} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);
    await userEvent.upload(screen.getByLabelText("Word 文件"), new File(["x"], "报告.doc", { type: "application/msword" }), { applyAccept: false });
    await userEvent.type(screen.getByLabelText("检查日期"), "2026-05-18");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-001");
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    expect(screen.getByRole("alert")).toHaveTextContent(".docx");
    expect(uploadWordImport).not.toHaveBeenCalled();
  });

  it("shows a field-specific error instead of letting native validation silently block submission", async () => {
    render(<ImportWordDialog bridgeName="测试桥" inspectionYearId="year-1" inspectionYear={2026} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);
    await userEvent.upload(screen.getByLabelText("Word 文件"), new File(["docx"], "报告.docx"));
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    expect(screen.getByRole("alert")).toHaveTextContent("请选择检查日期");
    expect(screen.getByLabelText("检查日期")).toHaveFocus();
    expect(uploadWordImport).not.toHaveBeenCalled();
  });
});
