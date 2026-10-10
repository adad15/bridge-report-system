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

  // 规范管理页不设页头，「新增规范」放在目录卡片的标题栏里；目录为空时不再另放一个。
  it("offers 新增规范 in the catalog header to administrators only", async () => {
    const onCreate = vi.fn();
    const filled = render(
      <StandardDocumentList documents={allStandardDocuments} selectedId="h21" isAdmin onSelect={vi.fn()} onCreate={onCreate} />,
    );
    await userEvent.click(screen.getByRole("button", { name: /新增规范/ }));
    expect(onCreate).toHaveBeenCalledTimes(1);
    filled.unmount();

    const empty = render(<StandardDocumentList documents={[]} selectedId={null} isAdmin onSelect={vi.fn()} onCreate={vi.fn()} />);
    expect(screen.getAllByRole("button", { name: /新增规范/ })).toHaveLength(1);
    empty.unmount();

    render(<StandardDocumentList documents={allStandardDocuments} selectedId="h21" isAdmin={false} onSelect={vi.fn()} onCreate={vi.fn()} />);
    expect(screen.queryByRole("button", { name: /新增规范/ })).not.toBeInTheDocument();
  });

  it("marks a standard without versions", () => {
    const empty = { ...h21Document, default_version_id: null, versions: [] };
    render(<StandardDocumentList documents={[empty]} selectedId={null} isAdmin={false} onSelect={vi.fn()} onCreate={vi.fn()} />);
    expect(screen.getByText("暂无版本")).toBeInTheDocument();
  });
});
