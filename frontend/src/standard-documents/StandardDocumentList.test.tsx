import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { allStandardDocuments, h21Document } from "../test/standardDocumentFixtures";
import { StandardDocumentList } from "./StandardDocumentList";

describe("StandardDocumentList", () => {
  it("groups standards by category and filters by any version's code", async () => {
    const onSelect = vi.fn();
    render(<StandardDocumentList documents={allStandardDocuments} selectedId="h21" isAdmin={false} onSelect={onSelect} onCreate={vi.fn()} />);

    const group = screen.getByRole("group", { name: "技术状况评定" });
    expect(within(group).getByText("JTG/T H21—2011 · 3 个版本")).toBeInTheDocument();

    await userEvent.type(screen.getByRole("textbox", { name: "搜索规范名称或编号" }), "H11");
    expect(screen.queryByRole("group", { name: "技术状况评定" })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "公路桥涵养护规范" }));
    expect(onSelect).toHaveBeenCalledWith("maintenance");
  });

  it("invites administrators to add the first standard", async () => {
    const onCreate = vi.fn();
    render(<StandardDocumentList documents={[]} selectedId={null} isAdmin onSelect={vi.fn()} onCreate={onCreate} />);
    await userEvent.click(screen.getByRole("button", { name: "新增规范" }));
    expect(onCreate).toHaveBeenCalledTimes(1);
  });

  it("marks a standard without versions", () => {
    const empty = { ...h21Document, default_version_id: null, versions: [] };
    render(<StandardDocumentList documents={[empty]} selectedId={null} isAdmin={false} onSelect={vi.fn()} onCreate={vi.fn()} />);
    expect(screen.getByText("暂无版本")).toBeInTheDocument();
  });
});
