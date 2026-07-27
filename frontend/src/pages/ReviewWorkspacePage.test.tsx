import { act, fireEvent, render, screen } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import {
  acquireEditLock,
  fetchReview,
  heartbeatEditLock,
  releaseEditLock,
  type EditLockSummary,
  type ReviewResponse,
} from "../api/reviewApi";
import { previewAssessment, type AssessmentPreviewResponse } from "../api/assessmentApi";
import { ApiError } from "../api/apiClient";
import {
  fetchLatestComponentInventory,
  type ComponentInventoryRevision,
} from "../api/componentInventoryApi";
import { fetchComponentBinding, type ComponentBindingOverview } from "../api/importBindingApi";
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
    fetchReview: vi.fn(),
    heartbeatEditLock: vi.fn(),
    releaseEditLock: vi.fn(),
  };
});

vi.mock("../api/assessmentApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/assessmentApi")>();
  return { ...original, previewAssessment: vi.fn() };
});

vi.mock("../api/importBindingApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/importBindingApi")>();
  return { ...original, fetchComponentBinding: vi.fn() };
});

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original, fetchLatestComponentInventory: vi.fn() };
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
    needsAttention: (...args: Parameters<typeof original.needsAttention>) => {
      renderCounters.attention += 1;
      return original.needsAttention(...args);
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
    contract_compatibility: "native_3_0",
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

function missingBindingOverview(): ComponentBindingOverview {
  return {
    inventory_confirmed: true,
    groups: [{
      part_name: "上部承重构件",
      total: 1,
      bound: 0,
      unmatched: 0,
      ambiguous: 0,
      missing: 1,
      rows: [{
        component_number: "1-1#梁",
        defect_count: 1,
        status: "missing",
        bridge_component_id: null,
        candidate_component_ids: [],
      }],
    }],
  };
}

const bindingInventory: ComponentInventoryRevision = {
  id: "inventory-1",
  bridge_id: "bridge-1",
  revision_number: 1,
  status: "已确认",
  baseline_revision_id: null,
  confirmed_at: "2026-07-17T08:00:00+08:00",
  entries: [],
};

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
    vi.mocked(fetchComponentBinding).mockResolvedValue(missingBindingOverview());
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(bindingInventory);
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it("lazy mounts review groups and keeps mounted state when switching back", async () => {
    const defectInput = await renderEditableReview();
    const bindingRequestsBeforeVisit = vi.mocked(fetchComponentBinding).mock.calls.length;
    expect(fetchLatestComponentInventory).not.toHaveBeenCalled();

    fireEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    const missingFilter = screen.getByRole("button", { name: "已标记缺失 1" });
    expect(fetchComponentBinding).toHaveBeenCalledTimes(bindingRequestsBeforeVisit + 1);
    expect(fetchLatestComponentInventory).toHaveBeenCalledTimes(1);

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
    expect(fetchComponentBinding).toHaveBeenCalledTimes(bindingRequestsBeforeVisit + 1);
    expect(fetchLatestComponentInventory).toHaveBeenCalledTimes(1);
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
