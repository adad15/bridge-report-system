import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import { deleteStandardDocument } from "../api/standardDocumentsApi";
import { maintenanceDocument } from "../test/standardDocumentFixtures";
import { DeleteStandardDocumentModal } from "./DeleteStandardDocumentModal";

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return { ...original, deleteStandardDocument: vi.fn() };
});

describe("DeleteStandardDocumentModal", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("says what goes away and deletes after confirmation", async () => {
    vi.mocked(deleteStandardDocument).mockResolvedValue(undefined);
    const onDeleted = vi.fn();
    render(<DeleteStandardDocumentModal document={maintenanceDocument} onClose={vi.fn()} onDeleted={onDeleted} />);

    expect(screen.getByText(/确定删除「公路桥涵养护规范」吗？全部 2 个版本和原文文件会一并删除/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: /^删\s?除$/ }));

    expect(deleteStandardDocument).toHaveBeenCalledWith(expect.any(String), "maintenance");
    expect(onDeleted).toHaveBeenCalledTimes(1);
  });

  it("stays open with the server error when deletion fails", async () => {
    vi.mocked(deleteStandardDocument).mockRejectedValue(new ApiError("standard_document_not_found", "不存在"));
    const onClose = vi.fn();
    const onDeleted = vi.fn();
    render(<DeleteStandardDocumentModal document={maintenanceDocument} onClose={onClose} onDeleted={onDeleted} />);

    await userEvent.click(screen.getByRole("button", { name: /^删\s?除$/ }));

    expect(await screen.findByText("规范不存在或已被删除，请刷新。")).toBeInTheDocument();
    expect(onDeleted).not.toHaveBeenCalled();
    expect(onClose).not.toHaveBeenCalled();
  });
});
