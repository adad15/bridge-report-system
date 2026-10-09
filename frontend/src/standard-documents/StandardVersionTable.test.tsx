import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { h21Document, maintenanceDocument } from "../test/standardDocumentFixtures";
import { StandardVersionTable } from "./StandardVersionTable";

function renderTable(isAdmin: boolean, document = maintenanceDocument) {
  const handlers = {
    onRead: vi.fn(), onUpload: vi.fn(), onEdit: vi.fn(), onReplace: vi.fn(),
    onDelete: vi.fn().mockResolvedValue(undefined),
  };
  render(<StandardVersionTable document={document} viewingId={document.versions[0].id} isAdmin={isAdmin} {...handlers} />);
  return handlers;
}

describe("StandardVersionTable", () => {
  it("states replacements, dates and sources for every version", () => {
    renderTable(false);
    expect(screen.getByText("2021-11-01 起由 JTG 5120—2021 替代")).toBeInTheDocument();
    expect(screen.getByText("替代 JTG H11—2004")).toBeInTheDocument();
    expect(screen.getByText("2004-06-28 发布")).toBeInTheDocument();
    expect(screen.getByText("原文来源：本地上传")).toBeInTheDocument();
    expect(screen.getByText("正在查看")).toBeInTheDocument();
    expect(screen.queryByText("替换原文")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /上传新版本/ })).not.toBeInTheDocument();
  });

  it("shows the draft date for drafts", () => {
    renderTable(false, h21Document);
    expect(screen.getByText("稿件日期 2026-02-11")).toBeInTheDocument();
    expect(screen.getByText("未发布")).toBeInTheDocument();
  });

  it("asks before deleting a version", async () => {
    const { onDelete, onRead } = renderTable(true);
    const row = screen.getByText("2004 版").closest("tr") as HTMLElement;

    await userEvent.click(within(row).getByText("阅读"));
    expect(onRead).toHaveBeenCalledWith("h11");

    await userEvent.click(within(row).getByText("删除"));
    await userEvent.click(await screen.findByRole("button", { name: /^删\s?除$/ }));
    expect(onDelete).toHaveBeenCalledWith(maintenanceDocument.versions[1]);
  });
});
