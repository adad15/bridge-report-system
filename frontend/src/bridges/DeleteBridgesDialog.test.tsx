import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  advanceBridgeCleanup,
  deleteBridges,
  fetchBridgeDeletionImpact,
} from "../api/bridgeAdministrationApi";
import { DeleteBridgesDialog } from "./DeleteBridgesDialog";

vi.mock("../api/bridgeAdministrationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/bridgeAdministrationApi")>();
  return {
    ...original,
    fetchBridgeDeletionImpact: vi.fn(),
    deleteBridges: vi.fn(),
    advanceBridgeCleanup: vi.fn(),
  };
});

const preview = {
  bridges: [{
    bridge: { id: "b1", system_number: "QL-000001", bridge_name: "百股大桥", route_number: null, route_name: null, station_mark: null, status: "在用" },
    counts: { inspection_years: 1, inspection_versions: 1, import_records: 1, bridge_aliases: 0, bridge_components: 5173, component_aliases: 0, defect_threads: 0, defect_observations: 279, defect_measurements: 0, defect_photos: 0, condition_ratings: 0, defect_comparisons: 0, archived_files_to_delete: 166, temporary_source_files_to_delete: 0, shared_files_retained: 0 },
    active_edit_locks: [],
    impact_token: "sha256:x",
  }],
  totals: {} as never,
  confirmation_text: "永久删除 QL-000001",
};

describe("DeleteBridgesDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchBridgeDeletionImpact).mockResolvedValue(preview as never);
  });

  it("polls cleanup and advances the progress bar to full", async () => {
    vi.mocked(deleteBridges).mockResolvedValue({
      batch_id: "batch-1",
      results: [{ bridge_id: "b1", system_number: "QL-000001", bridge_name: "百股大桥", status: "deleted", file_cleanup_status: "pending", pending_file_count: 141, total_file_count: 166, audit_id: "audit-1" }],
    });
    vi.mocked(advanceBridgeCleanup)
      .mockResolvedValueOnce({ total: 166, completed: 100, failed: 0, pending: 66, done: false })
      .mockResolvedValue({ total: 166, completed: 166, failed: 0, pending: 0, done: true });

    render(<DeleteBridgesDialog bridgeIds={["b1"]} onClose={vi.fn()} onSelectionChanged={vi.fn()} onCompleted={vi.fn()} />);

    await userEvent.type(await screen.findByRole("textbox", { name: "删除原因" }), "误建");
    await userEvent.type(screen.getByRole("textbox", { name: /永久删除 QL-000001/ }), "永久删除 QL-000001");
    await userEvent.click(screen.getByRole("button", { name: "永久删除" }));

    expect(await screen.findByText(/业务档案已完整删除/)).toBeInTheDocument();
    await waitFor(() => expect(screen.getByText(/归档文件已全部清理（共 166 个）/)).toBeInTheDocument());
    const bar = screen.getByRole("progressbar");
    expect(bar).toHaveAttribute("aria-valuenow", "166");
  });

  it("does not reload the impact preview after a successful delete", async () => {
    vi.mocked(deleteBridges).mockResolvedValue({
      batch_id: "batch-1",
      results: [{ bridge_id: "b1", system_number: "QL-000001", bridge_name: "百股大桥", status: "deleted", file_cleanup_status: "completed", pending_file_count: 0, total_file_count: 166, audit_id: "audit-1" }],
    });
    vi.mocked(advanceBridgeCleanup).mockResolvedValue({ total: 166, completed: 166, failed: 0, pending: 0, done: true });

    const { rerender } = render(<DeleteBridgesDialog bridgeIds={["b1"]} onClose={vi.fn()} onSelectionChanged={vi.fn()} onCompleted={vi.fn()} />);
    await screen.findByText("此操作不可撤销");
    await userEvent.type(screen.getAllByRole("textbox")[0], "误建");
    await userEvent.type(screen.getByRole("textbox", { name: /永久删除 QL-000001/ }), "永久删除 QL-000001");
    await userEvent.click(screen.getByRole("button", { name: "永久删除" }));
    await screen.findByText(/业务档案已完整删除/);

    // 父组件删除后可能以新的数组引用重渲染；此时不应再次拉取影响预览。
    vi.mocked(fetchBridgeDeletionImpact).mockClear();
    rerender(<DeleteBridgesDialog bridgeIds={["b1"]} onClose={vi.fn()} onSelectionChanged={vi.fn()} onCompleted={vi.fn()} />);
    expect(fetchBridgeDeletionImpact).not.toHaveBeenCalled();
    expect(screen.queryByText("删除影响加载失败。")).not.toBeInTheDocument();
  });
});
