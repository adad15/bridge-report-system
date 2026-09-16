import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { parseWordImport } from "../api/reviewApi";
import { createSourceDbImport, listSourceTasks, uploadWordImport } from "../api/workspaceApi";
import { chooseOption, optionLabels, selectedLabel } from "../test/antd";
import { ImportWordDialog } from "./ImportWordDialog";

vi.mock("../api/reviewApi", () => ({ parseWordImport: vi.fn() }));
vi.mock("../api/workspaceApi", () => ({
  createSourceDbImport: vi.fn(),
  listSourceTasks: vi.fn(),
  uploadWordImport: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

const TASKS = [
  { task_id: "task-2024", name: "百股大桥", check_date: "2024-06-21", defect_count: 279, photo_count: 166 },
  { task_id: "task-2025", name: "百股大桥", check_date: "2025-06-20", defect_count: 314, photo_count: 253 },
];

/** 默认来源已是来源软件离线库；要测 Word 那条路就得先切回去。 */
async function chooseWord() {
  await chooseOption(screen.getByLabelText("数据来源"), "Word 文件");
}

/** 离线库读完、任务列表填进下拉框之后才能往下操作。 */
async function tasksLoaded() {
  await waitFor(() => expect(screen.getByLabelText("离线库路径")).toHaveValue("D:/data/1"));
}

describe("ImportWordDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    // 打开对话框就会去读离线库；不给默认实现，每个用例都要先 mock 一遍。
    vi.mocked(listSourceTasks).mockResolvedValue({ source_db_path: "D:/data/1", tasks: TASKS });
  });

  it("uploads first, then parses with the fixed annual context", async () => {
    vi.mocked(uploadWordImport).mockResolvedValue({ id: "import-1" } as never);
    vi.mocked(parseWordImport).mockResolvedValue({ parsed: true } as never);
    const onCompleted = vi.fn();
    render(<ImportWordDialog bridgeName="绕阳河二号桥" inspectionYearId="year-1" inspectionYear={2026} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={onCompleted} />);
    await chooseWord();
    await userEvent.upload(screen.getByLabelText("Word 文件", { selector: "input" }), new File(["docx"], "报告.docx"));
    await userEvent.type(screen.getByLabelText("检查日期"), "2026-05-18{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-001");
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    await waitFor(() => expect(onCompleted).toHaveBeenCalled());
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
    await chooseWord();
    await userEvent.upload(screen.getByLabelText("Word 文件", { selector: "input" }), new File(["x"], "报告.doc", { type: "application/msword" }), { applyAccept: false });
    await userEvent.type(screen.getByLabelText("检查日期"), "2026-05-18{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-001");
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    expect(screen.getByRole("alert")).toHaveTextContent(".docx");
    expect(uploadWordImport).not.toHaveBeenCalled();
  });

  it("shows a field-specific error instead of letting native validation silently block submission", async () => {
    render(<ImportWordDialog bridgeName="测试桥" inspectionYearId="year-1" inspectionYear={2026} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);
    await chooseWord();
    await userEvent.upload(screen.getByLabelText("Word 文件", { selector: "input" }), new File(["docx"], "报告.docx"));
    await userEvent.click(screen.getByRole("button", { name: "上传并解析" }));
    expect(screen.getByRole("alert")).toHaveTextContent("请选择检查日期");
    expect(screen.getByLabelText("检查日期")).toHaveFocus();
    expect(uploadWordImport).not.toHaveBeenCalled();
  });

  it("registers a source database import without uploading anything", async () => {
    vi.mocked(createSourceDbImport).mockResolvedValue({ id: "import-9" } as never);
    vi.mocked(parseWordImport).mockResolvedValue({ parsed: true } as never);
    const onCompleted = vi.fn();
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={onCompleted} />);
    await tasksLoaded();
    await chooseOption(screen.getByLabelText("检测任务"), /2025-06-20/);
    await userEvent.type(screen.getByLabelText("检查日期"), "2024-06-21{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-2024");
    await userEvent.click(screen.getByRole("button", { name: "开始导入" }));
    await waitFor(() => expect(onCompleted).toHaveBeenCalled());

    // 路径由后端给出，用户没填过一个字符。
    // 名字用选中的任务，而不是离线库那个叫 "1" 的文件名——列表里才认得出来。
    expect(createSourceDbImport).toHaveBeenCalledWith(
      expect.any(String), "year-1", "D:/data/1", "task-2025", "百股大桥 2025-06-20");
    expect(uploadWordImport).not.toHaveBeenCalled();
    // 源库那条路没有 Word 规则档；多送一个字段只会让人以为它还在起作用。
    expect(parseWordImport).toHaveBeenCalledWith(expect.any(String), "import-9",
      expect.not.objectContaining({ rule_profile: expect.anything() }));
    expect(onCompleted).toHaveBeenCalledWith("import-9");
  });

  it("refreshes the workspace after a failed parse so the auto-deleted record disappears", async () => {
    vi.mocked(createSourceDbImport).mockResolvedValue({ id: "import-failed" } as never);
    vi.mocked(parseWordImport).mockRejectedValue(new Error("契约校验失败"));
    const onChanged = vi.fn();
    const onCompleted = vi.fn();
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={onChanged} onCompleted={onCompleted} />);
    await tasksLoaded();
    await chooseOption(screen.getByLabelText("检测任务"), /2025-06-20/);
    await userEvent.type(screen.getByLabelText("检查日期"), "2024-06-21{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-2024");
    await userEvent.click(screen.getByRole("button", { name: "开始导入" }));

    expect(await screen.findByRole("alert")).toHaveTextContent("契约校验失败");
    expect(onChanged).toHaveBeenCalledTimes(1);
    expect(onCompleted).not.toHaveBeenCalled();
  });

  it("lists the tasks so nobody has to know the vendor's task id", async () => {
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);

    // taskId 是厂商库里的 UUID；界面必须把它翻译成人能认的桥名、日期与条数。
    await tasksLoaded();
    const option = (await optionLabels(screen.getByLabelText("检测任务"))).find((label) => label.includes("2025-06-20"));
    expect(option).toContain("百股大桥");
    expect(option).toContain("314 条病害");
    expect(option).toContain("253 张照片");
    // 路径也不用人填。
    expect(screen.getByLabelText("离线库路径")).toHaveValue("D:/data/1");
  });

  it("picks the only task automatically when the database holds just one", async () => {
    vi.mocked(listSourceTasks).mockResolvedValue({ source_db_path: "D:/data/1", tasks: [TASKS[0]] });
    vi.mocked(createSourceDbImport).mockResolvedValue({ id: "import-1" } as never);
    vi.mocked(parseWordImport).mockResolvedValue({ parsed: true } as never);
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);

    await tasksLoaded();
    expect(selectedLabel(screen.getByLabelText("检测任务"))).toMatch(/2024-06-21/);
    await userEvent.type(screen.getByLabelText("检查日期"), "2024-06-21{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-2024");
    await userEvent.click(screen.getByRole("button", { name: "开始导入" }));

    await waitFor(() => expect(createSourceDbImport).toHaveBeenCalledWith(
      expect.any(String), "year-1", "D:/data/1", "task-2024", "百股大桥 2024-06-21"));
  });

  it("says what went wrong when the offline database cannot be read", async () => {
    vi.mocked(listSourceTasks).mockRejectedValue(new Error("找不到来源软件的离线库"));
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);

    expect(await screen.findByText(/找不到来源软件的离线库/)).toBeInTheDocument();
    expect(screen.getByText("未读到检测任务")).toBeInTheDocument();
  });

  it("refuses to register an import until a task is chosen", async () => {
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);
    await tasksLoaded();
    await userEvent.type(screen.getByLabelText("检查日期"), "2024-06-21{Enter}");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-2024");
    await userEvent.click(screen.getByRole("button", { name: "开始导入" }));

    expect(screen.getByRole("alert")).toHaveTextContent("检测任务");
    expect(createSourceDbImport).not.toHaveBeenCalled();
  });

  it("spells out that the bridge must be opened in the desktop app first", async () => {
    // 数据只在那一步才落盘；不写清楚，用户会对着空库反复重试。
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);

    expect(screen.getByText(/桌面程序里打开该桥/)).toBeInTheDocument();
  });

  it("hides the source database fields when re-parsing an existing record", async () => {
    render(<ImportWordDialog bridgeName="百股大桥" inspectionYearId="year-1" inspectionYear={2024} retryImport={{ id: "import-3", import_name: "旧资料" } as never} onClose={vi.fn()} onChanged={vi.fn()} onCompleted={vi.fn()} />);

    expect(screen.queryByLabelText("数据来源")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("离线库路径")).not.toBeInTheDocument();
    // 重新解析用的是登记时存下的引用，不该再去读离线库。
    expect(listSourceTasks).not.toHaveBeenCalled();
  });
});
