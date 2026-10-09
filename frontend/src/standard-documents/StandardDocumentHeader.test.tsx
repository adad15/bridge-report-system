import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { h21Document, maintenanceDocument, standardVersion } from "../test/standardDocumentFixtures";
import { StandardDocumentHeader } from "./StandardDocumentHeader";

function renderHeader(props: Partial<Parameters<typeof StandardDocumentHeader>[0]> = {}) {
  const handlers = { onSelectVersion: vi.fn(), onEditDocument: vi.fn(), onDeleteDocument: vi.fn() };
  render(
    <StandardDocumentHeader
      document={maintenanceDocument}
      version={maintenanceDocument.versions[1]}
      isAdmin={false}
      {...handlers}
      {...props}
    />,
  );
  return handlers;
}

describe("StandardDocumentHeader", () => {
  it("explains a withdrawn version and offers the current one", async () => {
    const { onSelectVersion } = renderHeader();

    expect(screen.getByText("JTG H11—2004 已于 2021-11-01 废止，由 JTG 5120—2021 替代；仅供查阅历史报告的评定依据。"))
      .toBeInTheDocument();
    expect(screen.getByText("JTG H11—2004 · 交通运输部发布 · 2004-10-01 实施 · 2021-11-01 废止")).toBeInTheDocument();
    expect(screen.getByRole("link", { name: /下载原文/ })).toHaveAttribute("href", expect.stringContaining("download=1"));

    await userEvent.click(screen.getByRole("button", { name: "查看现行版本" }));
    expect(onSelectVersion).toHaveBeenCalledWith("jtg-5120");
  });

  it("warns that a draft cannot be used for assessment", () => {
    renderHeader({ document: h21Document, version: h21Document.versions[0] });
    expect(screen.getByText("2026 报批稿尚未正式发布，内容可能与最终发布版本不同，不能作为评定依据。")).toBeInTheDocument();
    expect(screen.getByText("JTG/T 52XX—2026 · 尚未发布 · 稿件日期 2026-02-11")).toBeInTheDocument();
  });

  it("says when an upcoming version takes over", () => {
    const upcoming = standardVersion({ id: "next", label: "2027 版", code: "JTG 5120—2027", status: "upcoming", effective_date: "2027-03-01" });
    const document = { ...maintenanceDocument, versions: [upcoming, ...maintenanceDocument.versions] };
    renderHeader({ document, version: upcoming });
    expect(screen.getByText("JTG 5120—2027 将于 2027-03-01 实施。在此之前现行版本仍是 JTG 5120—2021。")).toBeInTheDocument();
  });

  it("shows no notice and no maintenance menu for a reader on the current version", () => {
    renderHeader({ version: maintenanceDocument.versions[0] });
    expect(screen.queryByRole("alert")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "更多" })).not.toBeInTheDocument();
  });

  it("gives administrators edit and delete in the more menu", async () => {
    const { onDeleteDocument } = renderHeader({ isAdmin: true, version: maintenanceDocument.versions[0] });
    await userEvent.click(screen.getByRole("button", { name: "更多" }));
    await userEvent.click(await screen.findByText("删除规范"));
    expect(onDeleteDocument).toHaveBeenCalledTimes(1);
  });
});
