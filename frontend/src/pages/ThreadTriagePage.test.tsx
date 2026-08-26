import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import {
  applyTriageBatch,
  fetchTriageBatchDetail,
  fetchTriageSummary,
  resolveTriageCluster,
  type TriageBatchDetail,
  type TriageManualCluster,
  type TriageSummary,
} from "../api/threadTriageApi";
import { ThreadTriagePage } from "./ThreadTriagePage";

vi.mock("../api/threadTriageApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/threadTriageApi")>();
  return {
    ...original,
    fetchTriageSummary: vi.fn(),
    fetchTriageBatchDetail: vi.fn(),
    applyTriageBatch: vi.fn(),
    resolveTriageCluster: vi.fn(),
  };
});

// 旧整理页每张卡各发一次候选请求；这个 spy 盯的就是"新页面一次都不发"。
vi.mock("../api/componentArchiveApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentArchiveApi")>();
  return { ...original, fetchThreadSuggestions: vi.fn() };
});

const mockedSummary = vi.mocked(fetchTriageSummary);
const mockedDetail = vi.mocked(fetchTriageBatchDetail);
const mockedApply = vi.mocked(applyTriageBatch);
const mockedResolve = vi.mocked(resolveTriageCluster);

function summary(): TriageSummary {
  return {
    snapshot_id: "snapshot-1",
    unbound_observation_count: 1197,
    batchable_group_count: 481,
    batchable_observation_count: 1174,
    manual_group_count: 10,
    manual_observation_count: 23,
    manual_clusters: [],
    batches: [
      {
        batch_id: "batch-hinge",
        fingerprint: "fp-hinge",
        action: "create",
        structure_part: "上部结构",
        component_type: "铰缝",
        defect_type: "渗水泛碱",
        defect_location: null,
        year_set: [2024, 2025, 2026],
        group_count: 163,
        observation_count: 489,
        sample_groups: [
          { group_id: "g-1", business_component_code: "1#铰缝", years: [2024, 2025, 2026], target_thread_id: null },
          { group_id: "g-84", business_component_code: "84#铰缝", years: [2024, 2025, 2026], target_thread_id: null },
          { group_id: "g-163", business_component_code: "163#铰缝", years: [2024, 2025, 2026], target_thread_id: null },
        ],
      },
      {
        batch_id: "batch-cap",
        fingerprint: "fp-cap",
        action: "bind",
        structure_part: "下部结构",
        component_type: "盖梁",
        defect_type: "受渗水侵蚀",
        defect_location: "大小里程侧",
        year_set: [2026],
        group_count: 2,
        observation_count: 2,
        sample_groups: [
          { group_id: "g-cap-1", business_component_code: "1#墩盖梁", years: [2026], target_thread_id: "t-1" },
          { group_id: "g-cap-10", business_component_code: "10#墩盖梁", years: [2026], target_thread_id: "t-2" },
        ],
      },
    ],
  };
}

function detail(): TriageBatchDetail {
  return {
    snapshot_id: "snapshot-1",
    batch_id: "batch-hinge",
    fingerprint: "fp-hinge",
    action: "create",
    structure_part: "上部结构",
    component_type: "铰缝",
    defect_type: "渗水泛碱",
    defect_location: null,
    year_set: [2024, 2025, 2026],
    group_count: 3,
    observation_count: 6,
    groups: ["1#铰缝", "2#铰缝", "3#铰缝"].map((code, index) => ({
      group_id: `g-${index + 1}`,
      bridge_component_id: `c-${index + 1}`,
      business_component_code: code,
      target_thread: null,
      observations: [2024, 2025].map((year) => ({
        id: `o-${index + 1}-${year}`,
        inspection_year: year,
        defect_type: "渗水泛碱",
        defect_location: null,
        updated_at: `2026-08-26 10:0${index}:00+08`,
        scale: "2",
        measurements: [],
        photos: [],
      })),
    })),
  };
}

function bindDetail(): TriageBatchDetail {
  return {
    ...detail(),
    batch_id: "batch-cap",
    fingerprint: "fp-cap",
    action: "bind",
    component_type: "盖梁",
    year_set: [2026],
    groups: [
      {
        group_id: "g-cap-1",
        bridge_component_id: "c-cap-1",
        business_component_code: "1#墩盖梁",
        target_thread: { thread_id: "t-1", system_number: "BHXS-000123", thread_name: "受渗水侵蚀｜大小里程侧" },
        observations: [{
          id: "o-cap-1", inspection_year: 2026, defect_type: "受渗水侵蚀",
          defect_location: "大小里程侧", updated_at: "2026-08-26 10:00:00+08",
        }],
      },
      {
        group_id: "g-cap-10",
        bridge_component_id: "c-cap-10",
        business_component_code: "10#墩盖梁",
        target_thread: { thread_id: "t-2", system_number: "BHXS-000456", thread_name: "受渗水侵蚀｜大小里程侧" },
        observations: [{
          id: "o-cap-10", inspection_year: 2026, defect_type: "受渗水侵蚀",
          defect_location: "大小里程侧", updated_at: "2026-08-26 10:01:00+08",
        }],
      },
    ],
  };
}

// 位置写法不同的两组：系统看得出它们相关，看不出该合还是该分。
function overlapCluster(): TriageManualCluster {
  return {
    cluster_id: "cl-1",
    reason_codes: ["location_overlap"],
    group_count: 2,
    observation_count: 3,
    overlap_targets: [],
    related_threads: [{
      id: "t-9", system_number: "BHXS-000900", thread_name: "横向裂缝｜梁底",
      bridge_component_id: "c-beam", defect_type: "横向裂缝", defect_location: "梁底",
    }],
    groups: [
      {
        group_id: "cg-1", bridge_component_id: "c-beam", business_component_code: "1#板梁",
        defect_type: "横向裂缝", defect_location: "梁底", target_thread_id: "t-9",
        observations: [
          {
            id: "co-1", inspection_year: 2024, defect_type: "横向裂缝",
            defect_location: "梁底", updated_at: "2026-08-26 10:00:00+08",
          },
          {
            id: "co-2", inspection_year: 2025, defect_type: "横向裂缝",
            defect_location: "梁底", updated_at: "2026-08-26 10:01:00+08",
          },
        ],
      },
      {
        group_id: "cg-2", bridge_component_id: "c-beam", business_component_code: "1#板梁",
        defect_type: "横向裂缝", defect_location: "梁底部", target_thread_id: null,
        observations: [{
          id: "co-3", inspection_year: 2025, defect_type: "横向裂缝",
          defect_location: "梁底部", updated_at: "2026-08-26 10:02:00+08",
        }],
      },
    ],
  };
}

// 同一年两条：可能是同一处记了两遍，也可能真是两处，得并排看着才判得了。
function sameYearCluster(): TriageManualCluster {
  return {
    cluster_id: "cl-2",
    reason_codes: ["multiple_in_year"],
    group_count: 1,
    observation_count: 2,
    overlap_targets: [],
    related_threads: [],
    groups: [{
      group_id: "cg-3", bridge_component_id: "c-rail", business_component_code: "3#栏杆",
      defect_type: "破损", defect_location: "", target_thread_id: null,
      observations: [
        {
          id: "co-4", inspection_year: 2026, defect_type: "破损",
          defect_location: "", updated_at: "2026-08-26 11:00:00+08",
        },
        {
          id: "co-5", inspection_year: 2026, defect_type: "破损",
          defect_location: "", updated_at: "2026-08-26 11:01:00+08",
        },
      ],
    }],
  };
}

// 一条观测精确命中了两条已有线索——系统没有理由偏向其中任何一条。
function ambiguousCluster(): TriageManualCluster {
  const base = sameYearCluster();
  return {
    ...base,
    cluster_id: "cl-3",
    reason_codes: ["ambiguous_thread"],
    observation_count: 1,
    groups: [{ ...base.groups[0], observations: [base.groups[0].observations[0]] }],
    related_threads: [
      {
        id: "t-a", system_number: "BHXS-000700", thread_name: "破损｜上游侧",
        bridge_component_id: "c-rail", defect_type: "破损", defect_location: "上游侧",
      },
      {
        id: "t-b", system_number: "BHXS-000800", thread_name: "破损｜下游侧",
        bridge_component_id: "c-rail", defect_type: "破损", defect_location: "下游侧",
      },
    ],
  };
}

function withClusters(clusters: TriageManualCluster[]): TriageSummary {
  return { ...summary(), manual_clusters: clusters };
}

function renderPage() {
  render(
    <MemoryRouter initialEntries={["/bridges/bridge-1/defect-threads/triage"]}>
      <Routes>
        <Route path="/bridges/:bridgeId/defect-threads/triage" element={<ThreadTriagePage />} />
      </Routes>
    </MemoryRouter>,
  );
}

beforeEach(() => {
  mockedSummary.mockReset();
  mockedDetail.mockReset();
  mockedApply.mockReset();
  mockedResolve.mockReset();
  mockedSummary.mockResolvedValue(summary());
  mockedDetail.mockResolvedValue(detail());
});

describe("ThreadTriagePage", () => {
  // 旧页面的死法：1197 张卡各发一次候选请求，浏览器每域名 6 条连接，一张也加载不完。
  it("loads the whole workbench with one request and no per-observation candidates", async () => {
    const { fetchThreadSuggestions } = await import("../api/componentArchiveApi");
    renderPage();

    expect(await screen.findByText(/163 个构件/)).toBeInTheDocument();
    expect(mockedSummary).toHaveBeenCalledTimes(1);
    expect(vi.mocked(fetchThreadSuggestions)).not.toHaveBeenCalled();
    expect(mockedDetail).not.toHaveBeenCalled() ;
  });

  // 三张卡都写着"盖梁"的话人分不出是哪一个——旧页面正是因为这一处从根上没法用。
  it("shows the business component code rather than the part name", async () => {
    renderPage();

    expect(await screen.findByText("1#铰缝")).toBeInTheDocument();
    expect(screen.getByText("84#铰缝")).toBeInTheDocument();
    expect(screen.getByText("163#铰缝")).toBeInTheDocument();
  });

  it("opens a batch with a single detail request", async () => {
    renderPage();
    fireEvent.click((await screen.findAllByRole("button", { name: "展开逐组核对" }))[0]);

    await waitFor(() => expect(mockedDetail).toHaveBeenCalledTimes(1));
    expect(await screen.findByLabelText("批次逐组明细")).toBeInTheDocument();
  });

  it("drops the excluded groups from the payload it submits", async () => {
    mockedApply.mockResolvedValue({
      status: "applied", groups_applied: 2, threads_created: 2,
      observations_bound: 4, results: [],
    });
    renderPage();
    fireEvent.click((await screen.findAllByRole("button", { name: "展开逐组核对" }))[0]);
    await screen.findByLabelText("批次逐组明细");

    fireEvent.click(screen.getByLabelText("纳入 2#铰缝"));
    fireEvent.click((await screen.findAllByRole("button", { name: /确认这 \d+ 组/ }))[0]);

    await waitFor(() => expect(mockedApply).toHaveBeenCalledTimes(1));
    const payload = mockedApply.mock.calls[0][2];
    expect(payload.groups.map((group) => group.group_id)).toEqual(["g-1", "g-3"]);
    expect(payload.batch_fingerprint).toBe("fp-hinge");
    expect(payload.groups[0].observations[0]).toHaveProperty("updated_at");
  });

  // 没展开也能确认，所以处理量必须在提交前说清楚。
  it("states how much will be processed before submitting", async () => {
    renderPage();
    fireEvent.click((await screen.findAllByRole("button", { name: "展开逐组核对" }))[0]);
    await screen.findByLabelText("批次逐组明细");

    expect(screen.getByText(/将处理 3 组、6 条观测/)).toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("纳入 2#铰缝"));
    expect(screen.getByText(/将处理 2 组、4 条观测（已剔除 1 组）/)).toBeInTheDocument();
  });

  // 线索属于具体构件：跨多构件的 bind 批次会绑到多条线索，批次层面没有唯一编号。
  it("never shows one BHXS number for a bind batch, only per group", async () => {
    mockedDetail.mockResolvedValue(bindDetail());
    renderPage();

    expect(await screen.findByText("将分别绑定到各构件中精确命中的已有线索")).toBeInTheDocument();
    expect(screen.queryByText(/BHXS-/)).not.toBeInTheDocument();

    fireEvent.click((await screen.findAllByRole("button", { name: "展开逐组核对" }))[1]);
    await screen.findByLabelText("批次逐组明细");
    expect(screen.getByText("BHXS-000123")).toBeInTheDocument();
    expect(screen.getByText("BHXS-000456")).toBeInTheDocument();
  });

  it("marks the failing groups when the batch is rejected", async () => {
    mockedApply.mockRejectedValue(new ApiError("thread_triage_conflict", "批次冲突", {
      details: {
        issues: [{
          reason_code: "observation_revision_conflict",
          message: "该观测已被其他操作更新。",
          group_id: "g-2", bridge_component_id: "c-2", observation_id: "o-2-2024",
        }],
      },
    }));
    renderPage();
    fireEvent.click((await screen.findAllByRole("button", { name: "展开逐组核对" }))[0]);
    await screen.findByLabelText("批次逐组明细");
    fireEvent.click((await screen.findAllByRole("button", { name: /确认这 \d+ 组/ }))[0]);

    expect(await screen.findByText(/该观测已被其他操作更新/)).toBeInTheDocument();
  });

  // 空位置是合法取值而非缺漏：整座构件的通病本来就没有具体位置，标签必须写出来，
  // 不能留白让人以为数据缺了一块。
  it("labels a batch with no location rather than leaving it blank", async () => {
    renderPage();

    expect(await screen.findByText(/铰缝 · 渗水泛碱 · （无位置）/)).toBeInTheDocument();
  });

  // "暂不处理"只在本次会话折叠，不产生任何服务端写入——未归入线索不是错误状态。
  it("skips a batch locally without writing anything", async () => {
    renderPage();

    const before = await screen.findAllByRole("button", { name: "暂不处理" });
    fireEvent.click(before[0]);

    await waitFor(() =>
      expect(screen.queryByText("上部结构｜铰缝 · 渗水泛碱 · （无位置）")).not.toBeInTheDocument());
    expect(mockedApply).not.toHaveBeenCalled();
    expect(mockedSummary).toHaveBeenCalledTimes(1);
  });

  it("reports a repeat submission as already handled rather than as a failure", async () => {
    mockedApply.mockResolvedValue({
      status: "already_completed", groups_applied: 3, threads_created: 0,
      observations_bound: 0, results: [],
    });
    renderPage();
    fireEvent.click((await screen.findAllByRole("button", { name: /确认这 \d+ 组/ }))[0]);

    expect(await screen.findByText(/已经处理过：3 组/)).toBeInTheDocument();
  });
});

// 一旦退化成“一观测一张卡”，判断该合还是该分所需的上下文恰好被拆没了。
describe("ThreadTriagePage 异常簇", () => {
  it("shows every related location group and its yearly observations on one screen", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    renderPage();

    const table = await screen.findByLabelText("簇内各位置历年观测");
    expect(within(table).getByText("梁底")).toBeInTheDocument();
    expect(within(table).getByText("梁底部")).toBeInTheDocument();
    expect(within(table).getByRole("columnheader", { name: "2024" })).toBeInTheDocument();
    expect(within(table).getByRole("columnheader", { name: "2025" })).toBeInTheDocument();
    expect(within(table).getByLabelText("梁底 2024 第 1 条")).toBeInTheDocument();
    expect(within(table).getByLabelText("梁底部 2025 第 1 条")).toBeInTheDocument();
  });

  it("names the existing thread with its BHXS number", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    renderPage();

    expect(await screen.findByText("BHXS-000900")).toBeInTheDocument();
    expect(screen.getByText("横向裂缝｜梁底")).toBeInTheDocument();
  });

  it("puts two observations of the same year side by side", async () => {
    mockedSummary.mockResolvedValue(withClusters([sameYearCluster()]));
    renderPage();

    expect(await screen.findByLabelText("（无位置） 2026 第 1 条")).toBeInTheDocument();
    expect(screen.getByLabelText("（无位置） 2026 第 2 条")).toBeInTheDocument();
  });

  // 组和观测是两个口径：2 组 3 条，写成“2 条”人会以为工作量只有一半。
  it("counts groups and observations separately", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    renderPage();

    expect(await screen.findByText(/2 组 · 3 条观测/)).toBeInTheDocument();
  });

  it("binds the selected observations to the chosen existing thread", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    mockedResolve.mockResolvedValue({
      status: "applied", groups_applied: 2, threads_created: 0, observations_bound: 3,
      results: [{
        group_id: "cg-1", bridge_component_id: "c-beam", thread_id: "t-9",
        thread_system_number: "BHXS-000900", outcome: "bound",
      }],
    });
    renderPage();

    fireEvent.click(await screen.findByRole("button", { name: "绑定到该线索" }));

    await waitFor(() => expect(mockedResolve).toHaveBeenCalledTimes(1));
    const payload = mockedResolve.mock.calls[0][2];
    expect(payload.action).toBe("bind");
    expect(payload.target_thread_id).toBe("t-9");
    expect(payload.bridge_component_id).toBe("c-beam");
    // 三条都带 updated_at：服务端靠它判并发，缺一条整批就得退回。
    expect(payload.observations.map((item) => item.id)).toEqual(["co-1", "co-2", "co-3"]);
    expect(payload.observations.every((item) => Boolean(item.updated_at))).toBe(true);
  });

  // 写法不一致就是这一簇存在的原因，人不点头系统不能替他合。
  it("refuses to merge mixed spellings until the person takes responsibility", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    renderPage();

    fireEvent.click(await screen.findByLabelText("合并为一条新线索"));
    const submit = screen.getByRole("button", { name: "建为一条线索" });
    expect(submit).toBeDisabled();

    fireEvent.click(screen.getByLabelText(/我确认它们是同一处病害/));
    expect(submit).toBeEnabled();

    // 位置由人定：默认值只是起点，两种写法谁也不天然权威。
    fireEvent.change(screen.getByLabelText("位置（可留空）"), { target: { value: "梁底" } });

    mockedResolve.mockResolvedValue({
      status: "applied", groups_applied: 2, threads_created: 1, observations_bound: 3,
      results: [{
        group_id: "cg-1", bridge_component_id: "c-beam", thread_id: "t-new",
        thread_system_number: "BHXS-001000", outcome: "created",
      }],
    });
    fireEvent.click(submit);

    await waitFor(() => expect(mockedResolve).toHaveBeenCalledTimes(1));
    const payload = mockedResolve.mock.calls[0][2];
    expect(payload.confirm_inexact_merge).toBe(true);
    // 类型与位置由人选定，不是从任一条观测抄的。
    expect(payload.defect_type).toBe("横向裂缝");
    expect(payload.defect_location).toBe("梁底");
  });

  // 拆分与合并共用一个动作：少勾几条就是拆，不需要另一套界面。
  it("lets the person split a year by selecting only part of the observations", async () => {
    mockedSummary.mockResolvedValue(withClusters([sameYearCluster()]));
    mockedResolve.mockResolvedValue({
      status: "applied", groups_applied: 1, threads_created: 1, observations_bound: 1,
      results: [{
        group_id: "cg-3", bridge_component_id: "c-rail", thread_id: "t-a",
        thread_system_number: "BHXS-001100", outcome: "created",
      }],
    });
    renderPage();

    fireEvent.click(await screen.findByLabelText("（无位置） 2026 第 2 条"));
    expect(screen.getByText(/余下 1 条这次不处理/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "建为一条线索" }));

    await waitFor(() => expect(mockedResolve).toHaveBeenCalledTimes(1));
    expect(mockedResolve.mock.calls[0][2].observations.map((item) => item.id)).toEqual(["co-4"]);
  });

  it("reports the resulting thread number after resolving", async () => {
    mockedSummary.mockResolvedValue(withClusters([sameYearCluster()]));
    mockedResolve.mockResolvedValue({
      status: "applied", groups_applied: 1, threads_created: 1, observations_bound: 2,
      results: [{
        group_id: "cg-3", bridge_component_id: "c-rail", thread_id: "t-a",
        thread_system_number: "BHXS-001100", outcome: "created",
      }],
    });
    renderPage();

    fireEvent.click(await screen.findByRole("button", { name: "建为一条线索" }));

    expect(await screen.findByText(/已新建线索 BHXS-001100，含 2 条观测/)).toBeInTheDocument();
  });
  it("skips a cluster locally without writing anything", async () => {
    mockedSummary.mockResolvedValue(withClusters([overlapCluster()]));
    renderPage();

    // 批次卡上也有同名按钮，必须限定在这张簇卡里点。
    const card = await screen.findByLabelText("异常簇 1#板梁");
    fireEvent.click(within(card).getByRole("button", { name: "暂不处理" }));

    await waitFor(() =>
      expect(screen.queryByLabelText("簇内各位置历年观测")).not.toBeInTheDocument());
    expect(mockedResolve).not.toHaveBeenCalled();
  });

  // 命中多条时全部列出并给编号：少列一条，人就在不知情的情况下被替他选了。
  it("lists every matched thread and binds to the one the person picks", async () => {
    mockedSummary.mockResolvedValue(withClusters([ambiguousCluster()]));
    mockedResolve.mockResolvedValue({
      status: "applied", groups_applied: 1, threads_created: 0, observations_bound: 1,
      results: [{
        group_id: "cg-3", bridge_component_id: "c-rail", thread_id: "t-b",
        thread_system_number: "BHXS-000800", outcome: "bound",
      }],
    });
    renderPage();

    expect(await screen.findByText("BHXS-000700")).toBeInTheDocument();
    expect(screen.getByText("BHXS-000800")).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("目标线索"), { target: { value: "t-b" } });
    fireEvent.click(screen.getByRole("button", { name: "绑定到该线索" }));

    await waitFor(() => expect(mockedResolve).toHaveBeenCalledTimes(1));
    expect(mockedResolve.mock.calls[0][2].target_thread_id).toBe("t-b");
  });
});
