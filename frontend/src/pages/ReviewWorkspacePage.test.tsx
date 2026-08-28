import { act, fireEvent, render, screen } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import {
  acquireEditLock,
  confirmImport,
  fetchReview,
  heartbeatEditLock,
  releaseEditLock,
  runPreflight,
  type EditLockSummary,
  type PreflightResponse,
  type ReviewResponse,
} from "../api/reviewApi";
import {
  fetchConfirmedAssessment,
  previewAssessment,
  type AssessmentPreviewResponse,
  type ConfirmedAssessmentResponse,
} from "../api/assessmentApi";
import { ApiError } from "../api/apiClient";
import {
} from "../api/componentInventoryApi";
import {
  applyComponentResolution,
  fetchResolutionWorkspace,
  type ResolutionWorkspace,
} from "../api/resolutionApi";
import { data } from "../review/testFixtures";
import { canModifyDefectStructure, ReviewWorkspacePage } from "./ReviewWorkspacePage";

describe("canModifyDefectStructure", () => {
  it("allows all locked initial editors and only administrators in a full reopen", () => {
    expect(canModifyDefectStructure(false, null, false)).toBe(true);
    expect(canModifyDefectStructure(false, "full", true)).toBe(true);
    expect(canModifyDefectStructure(false, "full", false)).toBe(false);
  });

  it("rejects warnings-only, read-only, busy, or missing-lock sessions", () => {
    expect(canModifyDefectStructure(false, "warnings_only", true)).toBe(false);
    expect(canModifyDefectStructure(true, null, true)).toBe(false);
  });
});

const renderCounters = vi.hoisted(() => ({
  defectsSection: 0,
  statistics: 0,
  attention: 0,
}));

vi.mock("../api/reviewApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/reviewApi")>();
  return {
    ...original,
    acquireEditLock: vi.fn(),
    confirmImport: vi.fn(),
    fetchReview: vi.fn(),
    heartbeatEditLock: vi.fn(),
    releaseEditLock: vi.fn(),
    runPreflight: vi.fn(),
  };
});

vi.mock("../api/assessmentApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/assessmentApi")>();
  return { ...original, previewAssessment: vi.fn(), fetchConfirmedAssessment: vi.fn() };
});

vi.mock("../api/resolutionApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/resolutionApi")>();
  return {
    ...original,
    fetchResolutionWorkspace: vi.fn(),
    applyComponentResolution: vi.fn(),
  };
});

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original };
});

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "tester", display_name: "测试用户", role: "normal" } }),
}));

vi.mock("../review/grouping", async (importOriginal) => {
  const original = await importOriginal<typeof import("../review/grouping")>();
  return {
    ...original,
    buildStatistics: (...args: Parameters<typeof original.buildStatistics>) => {
      renderCounters.statistics += 1;
      return original.buildStatistics(...args);
    },
  };
});

vi.mock("../review/components/DefectsSection", () => ({
  DefectsSection: ({ disabled, draft, dispatch }: { disabled?: boolean; draft: ReturnType<typeof data>; dispatch: (action: unknown) => void }) => {
    renderCounters.defectsSection += 1;
    const defect = draft.defects[0];
    return <input aria-label="测试病害位置" disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} />;
  },
}));

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

function currentLock(): EditLockSummary {
  return {
    owner_username: "tester",
    owner_display_name: "测试用户",
    owned_by_current_user: true,
    acquired_at: new Date(Date.now() - 1_000).toISOString(),
    expires_at: new Date(Date.now() + 120_000).toISOString(),
  };
}

function reviewResponse(): ReviewResponse {
  return {
    import_record: {
      id: "import-1",
      system_number: "DRJL-000001",
      import_name: "测试报告.docx",
      source_type: "软件导出Word",
      import_status: "待校对",
      importer_name: "word-importer",
      importer_version: "1.0.0",
      created_at: "2026-07-17T09:00:00+08:00",
      updated_at: "2026-07-17T09:00:00+08:00",
    },
    bridge: {
      id: "bridge-1",
      system_number: "QL-000001",
      bridge_name: "测试桥",
      route_name: "G1",
    },
    inspection_year: {
      id: "year-1",
      system_number: "NDJC-000001",
      inspection_year: 2026,
      status: "待校对",
      version_number: 1,
      is_current: true,
    },
    parsed_result: data(),
    statistics: {
      defect_count: 1,
      photo_count: 1,
      rating_item_count: 2,
      pending_count: 0,
      confirmed_count: 0,
      modified_count: 0,
      ignored_count: 0,
      object_warning_count: 0,
    },
    has_current_annual_facts: false,
    contract_compatibility: "native_4_0",
    technical_condition_standard: null,
    reopen: null,
    edit_lock: null,
  };
}

function assessmentResponse(revision: number): AssessmentPreviewResponse {
  return {
    client_revision: revision,
    input_checksum: `sha256:${"a".repeat(64)}`,
    input_summary: {},
    standard: { standard_id: "H21", standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准", official_edition: "2011", package_version: "1.0.1", content_checksum: `sha256:${"b".repeat(64)}`, algorithm_id: "h21" },
    result: null,
    issues: [],
    assessment_run_id: null,
  };
}

function missingBindingOverview(): ResolutionWorkspace {
  return {
    import_record_id: "import-1",
    bridge_id: "bridge-1",
    draft_version: 1,
    inventory_confirmed: true,
    inventory_revision_id: "rev-1",
    rating_tree: null,
    groups: [{
      group_id: "g1",
      source_component_name: "上部承重构件",
      source_component_number: "1-1#梁",
      normalized_component_number: "1-1#梁",
      resolution_mode: "single",
      status: "missing",
      match_method: null,
      inventory_revision_id: "rev-1",
      version: 1,
      ambiguous: false,
      split_eligible: false,
      split_expanded_count: null,
      side_pair_option: null,
      targets: [],
      candidates: [],
      members: [
        { member_id: "m1", source_candidate_id: "d1", source_order: 0, instances: [] },
      ],
      allowed_actions: ["clear"],
      blocked_reasons: [],
    }],
    parts: [],
    progress: {
      group_count: 1, bound_count: 0, unresolved_count: 0, ambiguous_count: 0,
      missing_count: 1, instance_count: 0, active_instance_count: 0,
      rating_matched_count: 0, rating_unresolved_count: 0, rating_missing_count: 0,
    },
  };
}

async function renderEditableReview(): Promise<HTMLInputElement> {
  render(
    <MemoryRouter initialEntries={["/imports/import-1/review"]}>
      <Routes>
        <Route path="/imports/:importRecordId/review" element={<ReviewWorkspacePage />} />
      </Routes>
    </MemoryRouter>
  );

  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
  await act(async () => {
    vi.advanceTimersByTime(0);
    await Promise.resolve();
    await Promise.resolve();
  });

  fireEvent.click(screen.getByRole("button", { name: /病害与照片/ }));
  return screen.getByRole("textbox", { name: "测试病害位置" }) as HTMLInputElement;
}

describe("ReviewWorkspacePage edit-lock heartbeat", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetAllMocks();
    renderCounters.defectsSection = 0;
    renderCounters.statistics = 0;
    renderCounters.attention = 0;
    vi.mocked(fetchReview).mockResolvedValue(reviewResponse());
    const lock = currentLock();
    vi.mocked(acquireEditLock).mockResolvedValue({
      acquired: true,
      lock_token: "lock-token",
      heartbeat_interval_seconds: 30,
      lock,
    });
    vi.mocked(releaseEditLock).mockResolvedValue({ released: true });
    vi.mocked(previewAssessment).mockImplementation(async (_baseUrl, _recordId, _draft, revision) => assessmentResponse(revision));
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(missingBindingOverview());
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it("keeps the edit-lock notice in its own row above the review body", async () => {
    await renderEditableReview();

    const notice = screen.getByText("你正在编辑此导入记录。").closest(".review-edit-lock-banner");
    expect(notice?.parentElement).toHaveClass("review-workspace-notices");
    expect(notice?.parentElement?.nextElementSibling).toHaveClass("review-body");
  });

  it("clears a stale successful preflight message when confirmation fails", async () => {
    const passedPreflight: PreflightResponse = {
      can_confirm: true,
      requires_revision_confirmation: false,
      blocking_errors: [],
      warnings: [],
    };
    vi.mocked(runPreflight).mockResolvedValue(passedPreflight);
    vi.mocked(confirmImport).mockRejectedValue(
      new ApiError("database_error", "评定审计记录写入失败。"),
    );
    await renderEditableReview();

    fireEvent.click(screen.getByRole("button", { name: "入库前检查" }));
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(screen.getByText("检查通过，可以确认入库。")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "确认年度事实入库" }));
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(screen.getByText("评定审计记录写入失败。")).toBeInTheDocument();
    expect(screen.queryByText("检查通过，可以确认入库。")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "确认年度事实入库" })).toBeDisabled();
  });

  it("retries once after a reload gives the previous page time to release its lock", async () => {
    const navigationSpy = vi.spyOn(window.performance, "getEntriesByType").mockReturnValue([
      { type: "reload" } as PerformanceNavigationTiming,
    ]);
    vi.mocked(acquireEditLock)
      .mockRejectedValueOnce(new ApiError("import_record_locked", "另一个页面正在编辑。", {
        details: { lock: currentLock() },
      }))
      .mockResolvedValueOnce({
        acquired: true,
        lock_token: "new-token",
        heartbeat_interval_seconds: 30,
        lock: currentLock(),
      });

    render(
      <MemoryRouter initialEntries={["/imports/import-1/review"]}>
        <Routes>
          <Route path="/imports/:importRecordId/review" element={<ReviewWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(0);
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(acquireEditLock).toHaveBeenCalledTimes(1);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(250);
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(acquireEditLock).toHaveBeenCalledTimes(2);
    expect(screen.getByText("你正在编辑此导入记录。")).toBeInTheDocument();
    navigationSpy.mockRestore();
  });

  it("does not retry when another tab still owns the lock during normal navigation", async () => {
    const navigationSpy = vi.spyOn(window.performance, "getEntriesByType").mockReturnValue([
      { type: "navigate" } as PerformanceNavigationTiming,
    ]);
    vi.mocked(acquireEditLock).mockRejectedValueOnce(new ApiError("import_record_locked", "另一个页面正在编辑。", {
      details: { lock: currentLock() },
    }));

    await renderEditableReview();

    expect(acquireEditLock).toHaveBeenCalledTimes(1);
    expect(screen.getAllByText(/另一个页面正在编辑/)).not.toHaveLength(0);
    navigationSpy.mockRestore();
  });

  it("releases the held lock when a reload or close hides the page", async () => {
    await renderEditableReview();
    vi.mocked(releaseEditLock).mockClear();

    window.dispatchEvent(new PageTransitionEvent("pagehide", { persisted: false }));

    expect(releaseEditLock).toHaveBeenCalledWith(
      "http://127.0.0.1:18080",
      "import-1",
      "lock-token",
      true,
    );
  });

  it("removes the transparent top gap from the component-binding scroll area", async () => {
    await renderEditableReview();

    fireEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    expect(document.querySelector(".review-main")).toHaveClass("review-main-component-binding");
  });

  // 构件绑定现在是落地分区（流程第一步）；没点进去过的分区仍然不挂载，
  // 点进去过的分区切走再切回来要保住自己的内部状态，且不重复取数。
  it("lazy mounts unvisited groups and keeps visited ones mounted when switching back", async () => {
    const defectInput = await renderEditableReview();
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    const bindingRequests = vi.mocked(fetchResolutionWorkspace).mock.calls.length;
    // 从没点开过的分区连 DOM 都不存在。
    expect(document.querySelector("[data-review-group='ratings']")).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    const missingFilter = screen.getByRole("button", { name: "已标记缺失 1" });
    fireEvent.click(missingFilter);
    expect(missingFilter).toHaveAttribute("aria-pressed", "true");
    const bindingPanel = missingFilter.closest("[data-review-group='component_binding']");
    expect(bindingPanel).not.toBeNull();

    fireEvent.click(screen.getByRole("button", { name: /病害与照片/ }));
    expect(bindingPanel).toHaveAttribute("hidden");
    expect(screen.getByRole("textbox", { name: "测试病害位置" })).toBe(defectInput);

    fireEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    expect(screen.getByRole("button", { name: "已标记缺失 1" })).toBe(missingFilter);
    expect(missingFilter).toHaveAttribute("aria-pressed", "true");
    expect(fetchResolutionWorkspace).toHaveBeenCalledTimes(bindingRequests);
  });

  // 绑定分区的写操作（绑定 / 批量替换 / 标记缺失 / 取消绑定 / 范围拆分）由后端直接
  // 改写 parsed_result_json。页面草稿是首屏拉取后独立持有的，不重取就会一直显示
  // 拆分前的旧病害，保存时还会把旧内容盖回去。
  it("reloads the review draft after a binding operation rewrites it server-side", async () => {
    const defectInput = await renderEditableReview();
    expect(defectInput.value).toBe("第二跨");

    const splitDraft = data();
    splitDraft.defects[0] = { ...splitDraft.defects[0], defect_location: "拆分后的位置" };
    vi.mocked(fetchReview).mockResolvedValue({ ...reviewResponse(), parsed_result: splitDraft });
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: missingBindingOverview().progress });

    fireEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    // 缺失行默认收起，先展开这一组才能点到它的动作按钮。
    fireEvent.click(screen.getByRole("button", { name: "已标记缺失 1" }));
    fireEvent.click(screen.getByLabelText("取消标记 1-1#梁"));
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    // 合并短时间内的连续绑定操作，超时后才重取一次最新草稿。
    await act(async () => {
      vi.advanceTimersByTime(300);
      await Promise.resolve();
      await Promise.resolve();
    });

    fireEvent.click(screen.getByRole("button", { name: /病害与照片/ }));
    expect(
      (screen.getByRole("textbox", { name: "测试病害位置" }) as HTMLInputElement).value,
    ).toBe("拆分后的位置");
  });

  it("keeps the focused defect field editable while a normal heartbeat is pending", async () => {
    const pendingHeartbeat = deferred<{ renewed: true; lock: EditLockSummary }>();
    vi.mocked(heartbeatEditLock).mockReturnValue(pendingHeartbeat.promise);
    const input = await renderEditableReview();
    expect(screen.getByRole("button", { name: /系统技术状况评定/ })).toBeInTheDocument();
    input.focus();
    expect(input).toBeEnabled();
    expect(input).toHaveFocus();

    await act(async () => {
      vi.advanceTimersByTime(30_000);
      await Promise.resolve();
    });

    expect(heartbeatEditLock).toHaveBeenCalledTimes(1);
    expect(input).toBeEnabled();
    expect(input).toHaveFocus();
  });

  it("does not recompute the whole review or rerender defects for an unchanged heartbeat", async () => {
    const lock = currentLock();
    vi.mocked(acquireEditLock).mockResolvedValue({
      acquired: true,
      lock_token: "lock-token",
      heartbeat_interval_seconds: 30,
      lock,
    });
    const largeResponse = reviewResponse();
    const sourceDefect = largeResponse.parsed_result.defects[0];
    largeResponse.parsed_result.defects = Array.from({ length: 279 }, (_, index) => ({
      ...sourceDefect,
      candidate_id: `defect_${String(index + 1).padStart(4, "0")}`,
    }));
    vi.mocked(fetchReview).mockResolvedValue(largeResponse);
    const pendingHeartbeat = deferred<{ renewed: true; lock: EditLockSummary }>();
    vi.mocked(heartbeatEditLock).mockReturnValue(pendingHeartbeat.promise);
    await renderEditableReview();
    await act(async () => {
      vi.advanceTimersByTime(650);
      await Promise.resolve();
      await Promise.resolve();
    });
    const before = { ...renderCounters };

    await act(async () => {
      vi.advanceTimersByTime(30_000);
      await Promise.resolve();
    });
    pendingHeartbeat.resolve({ renewed: true, lock });
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(renderCounters.statistics).toBe(before.statistics);
    expect(renderCounters.attention).toBe(before.attention);
    expect(renderCounters.defectsSection).toBe(before.defectsSection);
  });

  it("keeps editing enabled during a transient retry and recovers on the next heartbeat", async () => {
    const renewedLock = { ...currentLock(), expires_at: new Date(Date.now() + 180_000).toISOString() };
    vi.mocked(heartbeatEditLock)
      .mockRejectedValueOnce(new Error("temporary network error"))
      .mockResolvedValueOnce({ renewed: true, lock: renewedLock });
    const input = await renderEditableReview();

    await act(async () => {
      vi.advanceTimersByTime(30_000);
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(input).toBeEnabled();
    expect(screen.getByText(/续租暂时失败/)).toBeInTheDocument();

    await act(async () => {
      vi.advanceTimersByTime(30_000);
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(input).toBeEnabled();
    expect(screen.getByText("你正在编辑此导入记录。")).toBeInTheDocument();
  });

  it("switches to read-only only after the backend confirms that the lock is invalid", async () => {
    vi.mocked(heartbeatEditLock).mockRejectedValueOnce(new ApiError("edit_lock_expired", "编辑锁已过期。"));
    const input = await renderEditableReview();

    await act(async () => {
      vi.advanceTimersByTime(30_000);
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(input).toBeDisabled();
    expect(screen.getAllByText("编辑锁已过期。").length).toBeGreaterThan(0);
  });

  it("stops editing when the last server-confirmed lease expires during a stalled heartbeat", async () => {
    const pendingHeartbeat = deferred<{ renewed: true; lock: EditLockSummary }>();
    vi.mocked(heartbeatEditLock).mockReturnValue(pendingHeartbeat.promise);
    const input = await renderEditableReview();

    await act(async () => {
      vi.advanceTimersByTime(120_100);
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(heartbeatEditLock).toHaveBeenCalledTimes(1);
    expect(input).toBeDisabled();
    expect(screen.getAllByText(/租约已到期/).length).toBeGreaterThan(0);
  });

  it("merges assessment edits for 650ms without disabling or stealing focus", async () => {
    const pending = deferred<AssessmentPreviewResponse>();
    vi.mocked(previewAssessment).mockReturnValue(pending.promise);
    const input = await renderEditableReview();
    input.focus();

    await act(async () => {
      vi.advanceTimersByTime(649);
      await Promise.resolve();
    });
    expect(previewAssessment).not.toHaveBeenCalled();

    await act(async () => {
      vi.advanceTimersByTime(1);
      await Promise.resolve();
    });
    expect(previewAssessment).toHaveBeenCalledTimes(1);
    expect(input).toBeEnabled();
    expect(input).toHaveFocus();
  });

  it("aborts the older assessment request after a newer draft is submitted", async () => {
    const pending = deferred<AssessmentPreviewResponse>();
    vi.mocked(previewAssessment).mockReturnValue(pending.promise);
    const input = await renderEditableReview();
    await act(async () => {
      vi.advanceTimersByTime(650);
      await Promise.resolve();
    });
    const firstSignal = vi.mocked(previewAssessment).mock.calls[0][5];
    expect(firstSignal?.aborted).toBe(false);

    fireEvent.change(input, { target: { value: "第三跨" } });
    await act(async () => {
      vi.advanceTimersByTime(650);
      await Promise.resolve();
    });
    expect(previewAssessment).toHaveBeenCalledTimes(2);
    expect(firstSignal?.aborted).toBe(true);
    expect(vi.mocked(previewAssessment).mock.calls[1][3]).toBe(1);
  });
});

// ── 已入库记录的评定区 ────────────────────────────────────────────────
// 只读记录跑不了试算：预览端点要编辑锁，只读态拿不到锁，前端也先一步短路了。
// 这一段盯的是"入库之后分数还看得见"，以及别再摆一个按下去必被拒的「重新试算」。

function confirmedReviewResponse(): ReviewResponse {
  const response = reviewResponse();
  response.import_record.import_status = "已确认";
  if (response.inspection_year) response.inspection_year.status = "已确认";
  return response;
}

function confirmedAssessmentResponse(): ConfirmedAssessmentResponse {
  return {
    standard: assessmentResponse(0).standard,
    issues: [],
    result: {
      standard_id: "H21",
      package_version: "1.0.1",
      bridge_type_id: "h21.bridge_type.beam",
      overall_score: 83.25,
      calculated_grade: 2,
      final_grade: 2,
      explanation: "入库时写下的评定结论",
      structure_parts: [],
      triggered_controls: [],
      trace: [],
    },
    assessment_run_id: "run-1",
    formal_revision_number: 1,
    is_current: true,
    confirmed_at: "2026-08-25T02:46:10+08:00",
    inspection_year: 2024,
    inspection_year_version: 1,
    inspection_year_is_current: true,
  };
}

async function renderConfirmedReview(): Promise<void> {
  render(
    <MemoryRouter initialEntries={["/imports/import-1/review"]}>
      <Routes>
        <Route path="/imports/:importRecordId/review" element={<ReviewWorkspacePage />} />
      </Routes>
    </MemoryRouter>
  );
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
  await act(async () => {
    vi.advanceTimersByTime(0);
    await Promise.resolve();
    await Promise.resolve();
  });
  fireEvent.click(screen.getByRole("button", { name: /系统技术状况评定/ }));
}

describe("ReviewWorkspacePage confirmed assessment", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetAllMocks();
    vi.mocked(fetchReview).mockResolvedValue(confirmedReviewResponse());
    vi.mocked(releaseEditLock).mockResolvedValue({ released: true });
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(missingBindingOverview());
    vi.mocked(fetchConfirmedAssessment).mockResolvedValue(confirmedAssessmentResponse());
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it("reads the stored result instead of asking for a preview it cannot run", async () => {
    await renderConfirmedReview();

    expect(fetchConfirmedAssessment).toHaveBeenCalledTimes(1);
    expect(previewAssessment).not.toHaveBeenCalled();
    expect(screen.getByText("83.25")).toBeInTheDocument();
    expect(screen.getByText("入库时写下的评定结论")).toBeInTheDocument();
    expect(screen.queryByText("等待试算。")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "重新试算" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "重新加载" })).toBeInTheDocument();
  });

  it("explains a record that was never confirmed instead of failing silently", async () => {
    vi.mocked(fetchConfirmedAssessment).mockRejectedValue(
      new ApiError("assessment_report_not_found", "该导入记录没有已入库的系统评定结果。"));

    await renderConfirmedReview();

    expect(screen.getByText("本记录没有已入库的评定结果。")).toBeInTheDocument();
  });
});
