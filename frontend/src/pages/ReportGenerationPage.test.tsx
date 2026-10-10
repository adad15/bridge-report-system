import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, Route, Routes, useNavigate } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import {
  createReportJob,
  saveReportSettings,
  downloadReportJob,
  fetchComparisonCandidates,
  fetchReportEquipment,
  fetchCurrentReportJob,
  fetchReportJob,
  fetchReportPersonnel,
  fetchReportPreflight,
  fetchReportSettings,
  fetchReportTemplates,
  type InspectionReportSettings,
  type ReportJob,
  type ReportPreflight,
} from "../api/reportApi";
import { ReportGenerationPage } from "./ReportGenerationPage";

class ResizeObserverMock implements ResizeObserver {
  observe() {}
  unobserve() {}
  disconnect() {}
}

Object.defineProperty(globalThis, "ResizeObserver", {
  writable: true,
  value: ResizeObserverMock,
});

vi.mock("../api/reportApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/reportApi")>();
  return {
    ...original,
    fetchReportPreflight: vi.fn(),
    fetchReportSettings: vi.fn(),
    fetchReportTemplates: vi.fn(),
    fetchComparisonCandidates: vi.fn(),
    fetchReportPersonnel: vi.fn(),
    fetchReportEquipment: vi.fn(),
    fetchCurrentReportJob: vi.fn(),
    fetchReportJob: vi.fn(),
    createReportJob: vi.fn(),
    saveReportSettings: vi.fn(),
    downloadReportJob: vi.fn(),
  };
});

const preflight = (overrides: Partial<ReportPreflight> = {}): ReportPreflight => ({
  inspection_year_id: "y1",
  inspection_year: 2026,
  can_generate: true,
  findings: [],
  summary: {
    defect_component_count: 430,
    defect_observation_count: 1197,
    source_defect_count: 1149,
    photo_count: 455,
    overall_grade: "2类",
    structure_parts_with_data: ["SUPERSTRUCTURE", "SUBSTRUCTURE", "DECK"],
    template_covered_parts: ["SUPERSTRUCTURE", "SUBSTRUCTURE", "DECK"],
  },
  ...overrides,
});

const settings = (
  overrides: Partial<InspectionReportSettings> = {},
): InspectionReportSettings => ({
  inspection_year_id: "y1",
  inspection_year: 2026,
  inspection_date: null,
  report_number: null,
  project_name: null,
  inspection_org: null,
  template_id: "t1",
  template_name: "定期检测报告标准模板",
  template_is_usable: true,
  comparison_inspection_id: null,
  comparison_year: null,
  comparison_is_usable: false,
  personnel: [],
  equipment: [],
  configured_by_display_name: "管理员",
  configured_at: "2026-09-11 10:00:00+08",
  blocking_notes: [],
  ...overrides,
});

const job = (overrides: Partial<ReportJob> = {}): ReportJob => ({
  id: "j1",
  inspection_year_id: "y1",
  requested_by_user_id: "u1",
  status: "ready",
  is_running: false,
  progress: {
    page_count: 101,
    image_count: 455,
    file_bytes: 22314386,
    updater: "MicrosoftWordUpdater",
  },
  template_id: "t1",
  template_checksum: null,
  error_code: null,
  error_message: null,
  created_at: "2026-09-11 14:12:14+08",
  finished_at: "2026-09-11 14:13:48+08",
  expires_at: "2026-09-12 14:13:48+08",
  download_filename: "百股大桥定期检测报告（2类）.docx",
  can_download: true,
  ...overrides,
});

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((yes) => { resolve = yes; });
  return { promise, resolve };
}

function SwitchYear() {
  const navigate = useNavigate();
  return <button onClick={() => navigate("/bridges/b1/inspections/y2/report")}>Switch year</button>;
}

function renderPage() {
  return render(
    <MemoryRouter initialEntries={["/bridges/b1/inspections/y1/report"]}>
      <SwitchYear />
      <Routes>
        <Route
          path="/bridges/:bridgeId/inspections/:inspectionYearId/report"
          element={<ReportGenerationPage />}
        />
      </Routes>
    </MemoryRouter>,
  );
}

describe("ReportGenerationPage", () => {
  beforeEach(() => {
    vi.useRealTimers();
    vi.resetAllMocks();
    vi.mocked(saveReportSettings).mockResolvedValue(settings());
    vi.mocked(fetchReportPreflight).mockResolvedValue(preflight());
    vi.mocked(fetchReportSettings).mockResolvedValue(settings());
    vi.mocked(fetchReportTemplates).mockResolvedValue([]);
    vi.mocked(fetchComparisonCandidates).mockResolvedValue([]);
    vi.mocked(fetchReportPersonnel).mockResolvedValue([]);
    vi.mocked(fetchReportEquipment).mockResolvedValue([]);
    vi.mocked(fetchCurrentReportJob).mockResolvedValue(null);
  });

  it("saves independently entered cover fields with the annual report settings", async () => {
    renderPage();
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await userEvent.type(screen.getByLabelText("检测日期"), "2026-03-18");
    await userEvent.type(screen.getByLabelText("报告编号"), "BG-2026-001");
    await userEvent.type(screen.getByLabelText("项目名称"), "百股大桥定期检测");
    await userEvent.type(screen.getByLabelText("检测单位"), "验收检测机构");
    await userEvent.click(screen.getByRole("button", { name: /保存配置/ }));
    expect(saveReportSettings).toHaveBeenCalledWith("y1", expect.objectContaining({
      inspection_date: "2026-03-18", report_number: "BG-2026-001",
      project_name: "百股大桥定期检测", inspection_org: "验收检测机构",
    }));
  });

  it("discards a late response from the previous year before saving", async () => {
    const first = deferred<ReportPreflight>();
    vi.mocked(fetchReportPreflight)
      .mockImplementation((id) => id === "y1" ? first.promise :
        Promise.resolve(preflight({ inspection_year_id: "y2", inspection_year: 2027 })));
    vi.mocked(fetchReportSettings).mockImplementation(async (id) =>
      settings({ inspection_year_id: id, template_id: id === "y1" ? "t1" : "t2" }));
    renderPage();
    await userEvent.click(screen.getByRole("button", { name: "Switch year" }));
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await act(async () => first.resolve(preflight()));
    await userEvent.click(screen.getByRole("button", { name: /保存配置/ }));
    expect(saveReportSettings).toHaveBeenCalledWith("y2", expect.objectContaining({ template_id: "t2" }));
  });

  it("disables writes immediately while another year loads", async () => {
    const second = deferred<InspectionReportSettings>();
    vi.mocked(fetchReportSettings).mockImplementation((id) =>
      id === "y1" ? Promise.resolve(settings()) : second.promise);
    renderPage();
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await userEvent.click(screen.getByRole("button", { name: "Switch year" }));
    expect(screen.getByRole("button", { name: /保存配置/ })).toBeDisabled();
    expect(screen.getByRole("button", { name: "生成报告" })).toBeDisabled();
    expect(saveReportSettings).not.toHaveBeenCalled();
  });

  it("does not restore an older refresh after a newer refresh completes", async () => {
    renderPage();
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    const earlier = deferred<InspectionReportSettings>();
    vi.mocked(fetchReportSettings).mockReturnValueOnce(earlier.promise)
      .mockResolvedValueOnce(settings({ template_id: "t-new" }));
    await userEvent.click(screen.getByRole("button", { name: /刷新/ }));
    await userEvent.click(screen.getByRole("button", { name: /刷新/ }));
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await act(async () => earlier.resolve(settings({ template_id: "t-old" })));
    await userEvent.click(screen.getByRole("button", { name: /保存配置/ }));
    expect(saveReportSettings).toHaveBeenCalledWith("y1", expect.objectContaining({ template_id: "t-new" }));
  });

  it("ignores a previous year's late generation response", async () => {
    const generated = deferred<ReportJob>();
    vi.mocked(createReportJob).mockReturnValueOnce(generated.promise);
    renderPage();
    await waitFor(() => expect(screen.getByRole("button", { name: "生成报告" })).toBeEnabled());
    await userEvent.click(screen.getByRole("button", { name: "生成报告" }));
    vi.mocked(fetchReportSettings).mockResolvedValue(settings({ inspection_year_id: "y2" }));
    vi.mocked(fetchReportPreflight).mockResolvedValue(preflight({ inspection_year_id: "y2" }));
    await userEvent.click(screen.getByRole("button", { name: "Switch year" }));
    await act(async () => generated.resolve(job({ status: "queued", is_running: true })));
    expect(screen.queryByText("已创建生成任务，正在后台执行。")).not.toBeInTheDocument();
  });

  it("ignores a save completed after switching years", async () => {
    const saved = deferred<InspectionReportSettings>();
    vi.mocked(saveReportSettings).mockReturnValueOnce(saved.promise);
    renderPage();
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await userEvent.click(screen.getByRole("button", { name: /保存配置/ }));
    vi.mocked(fetchReportSettings).mockResolvedValue(settings({ inspection_year_id: "y2", template_id: "t2" }));
    vi.mocked(fetchReportPreflight).mockResolvedValue(preflight({ inspection_year_id: "y2" }));
    await userEvent.click(screen.getByRole("button", { name: "Switch year" }));
    await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
    await act(async () => saved.resolve(settings()));
    expect(screen.queryByText("报告配置已保存。")).not.toBeInTheDocument();
    expect(fetchReportSettings).toHaveBeenCalledTimes(2);
    await userEvent.click(screen.getByRole("button", { name: /保存配置/ }));
    expect(saveReportSettings).toHaveBeenLastCalledWith("y2", expect.objectContaining({ template_id: "t2" }));
  });

  it("ignores a poll completed after switching years", async () => {
    let poll!: () => Promise<void>;
    const timer = vi.spyOn(window, "setInterval").mockImplementation((callback) => {
      poll = callback as () => Promise<void>;
      return 123 as unknown as ReturnType<typeof window.setInterval>;
    });
    try {
      const polled = deferred<ReportJob>();
      vi.mocked(fetchCurrentReportJob).mockResolvedValueOnce(job({ status: "queued", is_running: true }));
      vi.mocked(fetchReportJob).mockReturnValueOnce(polled.promise);
      renderPage();
      await waitFor(() => expect(poll).toBeDefined());
      let pending!: Promise<void>;
      act(() => { pending = poll(); });
      vi.mocked(fetchReportSettings).mockResolvedValue(settings({ inspection_year_id: "y2" }));
      vi.mocked(fetchReportPreflight).mockResolvedValue(preflight({ inspection_year_id: "y2" }));
      await userEvent.click(screen.getByRole("button", { name: "Switch year" }));
      await waitFor(() => expect(screen.getByRole("button", { name: /保存配置/ })).toBeEnabled());
      await act(async () => { polled.resolve(job()); await pending; });
      expect(screen.queryByRole("button", { name: /下载报告/ })).not.toBeInTheDocument();
      expect(fetchReportSettings).toHaveBeenCalledTimes(2);
    } finally {
      timer.mockRestore();
    }
  });

  // 只呈现「当前报告」一个状态：系统不保存报告版本，历史文件到期就删了，
  // 摆一张历史表会让人以为它们还能下（设计 §21.4）。
  it("shows an empty state when this year has never generated a report", async () => {
    renderPage();

    expect(await screen.findByText("这个年度还没有生成过报告")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "生成报告" })).toBeEnabled();
  });

  it("shows the content summary from the preflight", async () => {
    renderPage();

    expect(await screen.findByText("有病害的构件")).toBeInTheDocument();
    expect(screen.getByText("430")).toBeInTheDocument();
    expect(screen.getByText("1,197")).toBeInTheDocument();
    expect(screen.getByText("2类")).toBeInTheDocument();
  });

  // 阻断项必须在点按钮之前就摆出来：让用户点了才知道不行是最糟的交互（设计 §21.4）。
  it("lists blocking findings and refuses to start a job", async () => {
    vi.mocked(fetchReportPreflight).mockResolvedValue(
      preflight({
        can_generate: false,
        findings: [
          {
            code: "report_assessment_missing",
            message: "当前年度没有生效的正式评定。",
            severity: "blocking",
          },
          {
            code: "report_profile_incomplete",
            message: "桥梁档案缺少建成年月。",
            severity: "warning",
          },
        ],
      }),
    );
    renderPage();

    expect(await screen.findByText("当前年度没有生效的正式评定。")).toBeInTheDocument();
    expect(screen.getByText("桥梁档案缺少建成年月。")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "生成报告" })).toBeDisabled();
  });

  // 配置层面的阻断（模板停用、人员停用）与预检同等对待，都要挡住生成。
  it("treats settings blocking notes as blockers too", async () => {
    vi.mocked(fetchReportSettings).mockResolvedValue(
      settings({ blocking_notes: ["所选模板已停用或校验失效，请重新选择。"] }),
    );
    renderPage();

    expect(
      await screen.findByText("所选模板已停用或校验失效，请重新选择。"),
    ).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "生成报告" })).toBeDisabled();
  });

  it("creates a job and shows its stage", async () => {
    vi.mocked(createReportJob).mockResolvedValue(
      job({ id: "j2", status: "queued", is_running: true, can_download: false, progress: {} }),
    );
    vi.mocked(fetchReportJob).mockResolvedValue(
      job({ id: "j2", status: "assembling_docx", is_running: true, can_download: false, progress: {} }),
    );
    renderPage();

    await userEvent.click(await screen.findByRole("button", { name: "生成报告" }));

    expect(createReportJob).toHaveBeenCalledWith("y1");
    expect(await screen.findByText("已创建生成任务，正在后台执行。")).toBeInTheDocument();
    await waitFor(() => expect(screen.getAllByText(/排队中|装配文档/).length).toBeGreaterThan(0));
  });

  // 下载必须走带 Authorization 头的请求。用超链接直接指过去的话，浏览器导航不带
  // 那个头，用户看到的是一屏 {"code":"auth_required"} 而不是文件。
  it("downloads through an authenticated request, not a bare link", async () => {
    vi.mocked(fetchCurrentReportJob).mockResolvedValue(job());
    vi.mocked(downloadReportJob).mockResolvedValue(undefined);
    renderPage();

    expect(await screen.findByText("百股大桥定期检测报告（2类）.docx")).toBeInTheDocument();
    expect(screen.getByText(/101 页 · 455 张图 · 21\.3 MB/)).toBeInTheDocument();
    expect(screen.queryByRole("link", { name: /下载报告/ })).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: /下载报告/ }));

    expect(downloadReportJob).toHaveBeenCalledWith("j1", "百股大桥定期检测报告（2类）.docx");
  });

  it("shows why a download failed instead of silently doing nothing", async () => {
    vi.mocked(fetchCurrentReportJob).mockResolvedValue(job());
    vi.mocked(downloadReportJob).mockRejectedValue(
      new ApiError("report_job_file_missing", "报告文件已不存在，请重新生成。"),
    );
    renderPage();

    await userEvent.click(await screen.findByRole("button", { name: /下载报告/ }));

    expect(await screen.findByText("报告文件已过期或已被清理，请重新生成。")).toBeInTheDocument();
  });

  // 失败只给一句"生成失败"，用户不知道该去哪儿处理；错误码决定提示去哪一页。
  it("explains a failure by its error code", async () => {
    vi.mocked(fetchCurrentReportJob).mockResolvedValue(
      job({
        status: "failed",
        is_running: false,
        can_download: false,
        error_code: "report_field_update_failed",
        error_message: "MicrosoftWordUpdater 在 900 秒内没有完成域更新。",
        download_filename: null,
      }),
    );
    renderPage();

    expect(
      await screen.findByText("MicrosoftWordUpdater 在 900 秒内没有完成域更新。"),
    ).toBeInTheDocument();
    expect(
      screen.getByText(/请确认本机装有其中之一且没有卡住的实例/),
    ).toBeInTheDocument();
  });

  // 过期任务不显示下载：系统不保存报告版本，只能重新生成（设计 §21.4）。
  it("tells the user to regenerate an expired report instead of offering a stale download", async () => {
    vi.mocked(fetchCurrentReportJob).mockResolvedValue(
      job({
        status: "expired",
        is_running: false,
        can_download: false,
        download_filename: null,
        expires_at: null,
      }),
    );
    renderPage();

    expect(await screen.findByText("报告已过期")).toBeInTheDocument();
    expect(screen.getByText(/临时文件已清理/)).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /下载报告/ })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "重新生成" })).toBeEnabled();
  });

  // 模板没覆盖的部位一定要拦住：那些病害没有输出位置，静默丢掉会让病害表与正式病害对不上。
  it("warns when the template does not cover a structure part that has defects", async () => {
    vi.mocked(fetchReportPreflight).mockResolvedValue(
      preflight({
        summary: {
          ...preflight().summary,
          structure_parts_with_data: ["SUPERSTRUCTURE", "WHOLE_BRIDGE"],
          template_covered_parts: ["SUPERSTRUCTURE"],
        },
      }),
    );
    renderPage();

    expect(await screen.findByText("所选模板没有覆盖：全桥")).toBeInTheDocument();
  });

  // 覆盖清单为空是"未知"，不是"一个都没覆盖"：模板还没记录锚点顺序时后端本来就跳过
  // 这项检查，前端把数据涉及的部位全标红等于报一个并不存在的问题。
  it("does not claim a coverage gap when the template has no recorded anchors", async () => {
    vi.mocked(fetchReportPreflight).mockResolvedValue(
      preflight({
        summary: { ...preflight().summary, template_covered_parts: [] },
      }),
    );
    renderPage();

    await screen.findByText("内容摘要");
    expect(screen.queryByText(/所选模板没有覆盖/)).not.toBeInTheDocument();
  });
});
