import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import {
  applyTriageBatch,
  fetchTriageBatchDetail,
  fetchTriageSummary,
  type TriageBatchDetail,
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
