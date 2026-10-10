import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { h21Document, maintenanceDocument, standardVersion } from "../test/standardDocumentFixtures";
import { StandardVersionTable } from "./StandardVersionTable";

function renderTable(isAdmin: boolean, document = maintenanceDocument) {
  const handlers = {
    onRead: vi.fn(), onEdit: vi.fn(), onReplace: vi.fn(),
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

  // 现行版本已有接替者：接替关系现在就写出来，不等它真的废止；没填发布日期的就不写「— 发布」。
  it("names the coming replacement and skips a missing publish date", () => {
    const next = standardVersion({
      id: "next", label: "2027 版", code: "JTG 5120—2027", status: "upcoming", effective_date: "2027-03-01", replaces_version_id: "jtg-5120",
    });
    const current = {
      ...maintenanceDocument.versions[0], withdrawn_date: "2027-03-01", withdrawn_date_source: "replacement" as const, replaced_by_version_id: "next",
    };
    renderTable(false, { ...maintenanceDocument, versions: [next, current, maintenanceDocument.versions[1]] });

    expect(screen.getByText("替代 JTG H11—2004；2027-03-01 起由 JTG 5120—2027 替代")).toBeInTheDocument();
    expect(screen.getByText("替代 JTG 5120—2021")).toBeInTheDocument();
    expect(screen.getByText("2027-03-01 实施")).toBeInTheDocument();
    expect(screen.queryByText(/— 发布/)).not.toBeInTheDocument();
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
