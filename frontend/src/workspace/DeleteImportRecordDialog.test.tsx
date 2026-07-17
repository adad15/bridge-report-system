import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { deleteImportRecord, fetchImportRecordDeletionImpact } from "../api/workspaceApi";
import { DeleteImportRecordDialog } from "./DeleteImportRecordDialog";

vi.mock("../api/workspaceApi", () => ({
  fetchImportRecordDeletionImpact: vi.fn(),
  deleteImportRecord: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

const impact = {
  import_record: { id: "import-1", system_number: "DRJL-000001", import_name: "百股大桥报告.docx", status: "待校对", source_type: "软件导出Word" },
  bridge: { id: "bridge-1", system_number: "QL-1", bridge_name: "百股大桥" },
  inspection_year: { id: "year-1", year: 2024, version_number: 1 },
  counts: {
    defects: 25, photos: 31, rating_items: 15, parsed_images: 36,
    archived_files_to_delete: 31, temporary_word_files_to_delete: 1,
    parse_work_directories_to_delete: 0, shared_files_retained: 1, formal_fact_references: 0,
  },
  active_edit_locks: [], can_delete: true, block_code: null,
  confirmation_text: "永久删除 DRJL-000001", impact_token: "sha256:impact",
};

describe("DeleteImportRecordDialog", () => {
  beforeEach(() => vi.resetAllMocks());

  it("explains that only one import is deleted and requires exact confirmation", async () => {
    vi.mocked(fetchImportRecordDeletionImpact).mockResolvedValue(impact);
    vi.mocked(deleteImportRecord).mockResolvedValue({ deleted: true, deletion_audit_id: "audit-1" } as never);
    const onDeleted = vi.fn();
    render(<DeleteImportRecordDialog importRecordId="import-1" onClose={vi.fn()} onDeleted={onDeleted} />);

    expect(await screen.findByText(/不会删除 2024 年度检测/)).toBeInTheDocument();
    expect(screen.getByText(/1 个共享文件/)).toBeInTheDocument();
    const deleteButton = screen.getByRole("button", { name: "永久删除此导入记录" });
    expect(deleteButton).toBeDisabled();
    await userEvent.type(screen.getByRole("textbox", { name: "删除原因" }), "重复上传");
    await userEvent.type(screen.getByRole("textbox", { name: /请输入/ }), "永久删除 DRJL-000001");
    expect(deleteButton).toBeEnabled();
    await userEvent.click(deleteButton);
    await waitFor(() => expect(deleteImportRecord).toHaveBeenCalledWith(expect.any(String), "import-1", {
      impact_token: "sha256:impact", confirmation_text: "永久删除 DRJL-000001", reason: "重复上传",
    }));
    expect(onDeleted).toHaveBeenCalled();
  });

  it("shows the editor and blocks deletion while a lock exists", async () => {
    vi.mocked(fetchImportRecordDeletionImpact).mockResolvedValue({
      ...impact, can_delete: false, block_code: "import_record_edit_locked",
      active_edit_locks: [{ import_record_id: "import-1", owner_username: "zhang", owner_display_name: "张工", acquired_at: "a", expires_at: "b" }],
    });
    render(<DeleteImportRecordDialog importRecordId="import-1" onClose={vi.fn()} onDeleted={vi.fn()} />);

    expect(await screen.findByRole("alert")).toHaveTextContent("张工");
    expect(screen.getByRole("button", { name: "永久删除此导入记录" })).toBeDisabled();
  });

  it("explains why confirmed formal data cannot be deleted separately", async () => {
    vi.mocked(fetchImportRecordDeletionImpact).mockResolvedValue({
      ...impact, can_delete: false, block_code: "import_record_has_formal_facts",
      counts: { ...impact.counts, formal_fact_references: 25 },
    });
    render(<DeleteImportRecordDialog importRecordId="import-1" onClose={vi.fn()} onDeleted={vi.fn()} />);

    expect(await screen.findByRole("alert")).toHaveTextContent("请使用年度删除能力");
  });
});
