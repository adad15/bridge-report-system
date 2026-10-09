import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createStandardDocument, updateStandardDocument } from "../api/standardDocumentsApi";
import { h21Document } from "../test/standardDocumentFixtures";
import { StandardDocumentFormModal } from "./StandardDocumentFormModal";

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return { ...original, createStandardDocument: vi.fn(), updateStandardDocument: vi.fn() };
});

const pdf = () => new File(["%PDF-1.4"], "h21.pdf", { type: "application/pdf" });
const fileInput = () => document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement;

describe("StandardDocumentFormModal", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("submits the standard together with its first version and the PDF", async () => {
    vi.mocked(createStandardDocument).mockResolvedValue(h21Document);
    const onSaved = vi.fn();
    render(<StandardDocumentFormModal mode={{ type: "create", categories: ["技术状况评定"] }} onClose={vi.fn()} onSaved={onSaved} />);

    await userEvent.type(screen.getByLabelText("规范名称"), "公路桥梁技术状况评定标准");
    await userEvent.type(screen.getByRole("combobox", { name: "分类" }), "技术状况评定");
    await userEvent.type(screen.getByLabelText("版本名称"), "2011 版");
    await userEvent.type(screen.getByLabelText("标准编号"), "JTG/T H21—2011");
    await userEvent.type(screen.getByLabelText("实施日期"), "2011-09-01{Enter}");
    await userEvent.upload(fileInput(), pdf());
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(createStandardDocument).toHaveBeenCalledWith(
      expect.any(String),
      { name: "公路桥梁技术状况评定标准", english_name: null, category: "技术状况评定" },
      expect.objectContaining({
        label: "2011 版", code: "JTG/T H21—2011", kind: "official",
        issuer: "中华人民共和国交通运输部", effective_date: "2011-09-01", withdrawn_date: null,
      }),
      expect.objectContaining({ name: "h21.pdf" }),
    );
    expect(onSaved).toHaveBeenCalledWith(h21Document);
  });

  it("refuses to submit without the required fields and a PDF", async () => {
    render(<StandardDocumentFormModal mode={{ type: "create", categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    for (const message of ["请填写规范名称", "请填写分类", "请填写版本名称", "请填写标准编号", "正式版必须填写实施日期", "请选择 PDF 文件"]) {
      expect(await screen.findByText(message)).toBeInTheDocument();
    }
    expect(createStandardDocument).not.toHaveBeenCalled();
  });

  it("rejects a file that is not a PDF", async () => {
    const user = userEvent.setup({ applyAccept: false });
    render(<StandardDocumentFormModal mode={{ type: "create", categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);
    await user.upload(fileInput(), new File(["hello"], "notes.txt", { type: "text/plain" }));
    await user.click(screen.getByRole("button", { name: /^上\s?传$/ }));
    expect(await screen.findByText("只能上传 PDF 文件")).toBeInTheDocument();
  });

  it("edits only the standard information", async () => {
    vi.mocked(updateStandardDocument).mockResolvedValue({ ...h21Document, name: "新名称" });
    render(<StandardDocumentFormModal mode={{ type: "edit", document: h21Document, categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);

    expect(screen.queryByLabelText("版本名称")).not.toBeInTheDocument();
    const name = screen.getByLabelText("规范名称");
    await userEvent.clear(name);
    await userEvent.type(name, "新名称");
    await userEvent.click(screen.getByRole("button", { name: /^保\s?存$/ }));

    expect(updateStandardDocument).toHaveBeenCalledWith(expect.any(String), "h21", {
      name: "新名称",
      english_name: "Standards for Technical Condition Evaluation of Highway Bridges",
      category: "技术状况评定",
    });
  });
});
