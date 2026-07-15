import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { deleteInspectionYear, fetchInspectionYearDeletionImpact } from "../api/workspaceApi";
import { DeleteInspectionYearDialog } from "./DeleteInspectionYearDialog";

vi.mock("../api/workspaceApi", () => ({
  fetchInspectionYearDeletionImpact: vi.fn(),
  deleteInspectionYear: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

const impact = {
  bridge: { id: "bridge-1", system_number: "QL-1", bridge_name: "绕阳河二号桥" },
  inspection_year: 2026,
  version_numbers: [1, 2],
  counts: {
    inspection_versions: 2, import_records: 1, defect_observations: 25, defect_measurements: 10,
    defect_photos: 31, condition_ratings: 15, archived_files_to_delete: 32, shared_files_retained: 1,
    defect_threads_affected: 8, defect_comparisons: 3,
  },
  active_edit_locks: [],
  confirmation_text: "永久删除 2026",
  impact_token: "sha256:impact",
};

describe("DeleteInspectionYearDialog", () => {
  beforeEach(() => vi.resetAllMocks());

  it("requires reason and exact confirmation before deleting all versions", async () => {
    vi.mocked(fetchInspectionYearDeletionImpact).mockResolvedValue(impact);
    vi.mocked(deleteInspectionYear).mockResolvedValue({ deleted: true, next_inspection_year_id: "year-2025" } as never);
    const onDeleted = vi.fn();
    render(<DeleteInspectionYearDialog inspectionYearId="year-2026" onClose={vi.fn()} onDeleted={onDeleted} />);
    expect(await screen.findByText(/全部版本/)).toHaveTextContent("V1、V2");
    const deleteButton = screen.getByRole("button", { name: "永久删除" });
    expect(deleteButton).toBeDisabled();
    await userEvent.type(screen.getByRole("textbox", { name: "删除原因" }), "误建年度");
    await userEvent.type(screen.getByRole("textbox", { name: /请输入/ }), "永久删除 2026");
    expect(deleteButton).toBeEnabled();
    await userEvent.click(deleteButton);
    await waitFor(() => expect(deleteInspectionYear).toHaveBeenCalledWith(expect.any(String), "year-2026", {
      impact_token: "sha256:impact", confirmation_text: "永久删除 2026", reason: "误建年度",
    }));
    expect(onDeleted).toHaveBeenCalled();
  });

  it("shows the editor and blocks deletion while an active lock exists", async () => {
    vi.mocked(fetchInspectionYearDeletionImpact).mockResolvedValue({
      ...impact,
      active_edit_locks: [{ import_record_id: "import-1", owner_username: "zhang", owner_display_name: "张工", acquired_at: "a", expires_at: "b" }],
    });
    render(<DeleteInspectionYearDialog inspectionYearId="year-2026" onClose={vi.fn()} onDeleted={vi.fn()} />);
    expect(await screen.findByRole("alert")).toHaveTextContent("张工");
    expect(screen.getByRole("button", { name: "永久删除" })).toBeDisabled();
  });
});
