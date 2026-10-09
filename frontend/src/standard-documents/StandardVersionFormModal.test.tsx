import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import { addStandardVersion, replaceStandardVersionFile, updateStandardVersion } from "../api/standardDocumentsApi";
import { chooseOption } from "../test/antd";
import { h21Document, maintenanceDocument } from "../test/standardDocumentFixtures";
import { StandardVersionFormModal } from "./StandardVersionFormModal";

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return { ...original, addStandardVersion: vi.fn(), updateStandardVersion: vi.fn(), replaceStandardVersionFile: vi.fn() };
});

const fileInput = () => document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement;

describe("StandardVersionFormModal", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("prefills a new version from the latest one and relabels dates for drafts", async () => {
    render(<StandardVersionFormModal mode={{ type: "add", document: h21Document }} onClose={vi.fn()} onSaved={vi.fn()} />);

    expect(screen.getByLabelText("发布部门")).toHaveValue("中华人民共和国交通运输部");
    expect(screen.getByLabelText("标准性质")).toHaveValue("推荐性行业标准");
    expect(screen.getByLabelText("实施日期")).toBeInTheDocument();

    await chooseOption(screen.getByRole("combobox", { name: "稿件类型" }), "报批稿");
    expect(await screen.findByLabelText("稿件日期")).toBeInTheDocument();
    expect(screen.queryByLabelText("实施日期")).not.toBeInTheDocument();
  });

  it("keeps a replacement-derived withdrawal date out of the edit form", async () => {
    vi.mocked(updateStandardVersion).mockResolvedValue(maintenanceDocument);
    const onSaved = vi.fn();
    render(
      <StandardVersionFormModal
        mode={{ type: "edit", document: maintenanceDocument, version: maintenanceDocument.versions[1] }}
        onClose={vi.fn()}
        onSaved={onSaved}
      />,
    );

    expect(screen.getByLabelText("版本名称")).toHaveValue("2004 版");
    expect(screen.getByLabelText(/废止日期/)).toHaveValue("");
    expect(document.querySelector('input[type="file"]')).toBeNull();
    await userEvent.click(screen.getByRole("button", { name: /^保\s?存$/ }));

    expect(updateStandardVersion).toHaveBeenCalledWith(
      expect.any(String),
      "h11",
      expect.objectContaining({ label: "2004 版", effective_date: "2004-10-01", withdrawn_date: null }),
    );
    expect(onSaved).toHaveBeenCalledWith(maintenanceDocument);
  });

  it("replaces only the file", async () => {
    vi.mocked(replaceStandardVersionFile).mockResolvedValue(maintenanceDocument);
    render(
      <StandardVersionFormModal
        mode={{ type: "replace", document: maintenanceDocument, version: maintenanceDocument.versions[0] }}
        onClose={vi.fn()}
        onSaved={vi.fn()}
      />,
    );

    expect(screen.queryByLabelText("版本名称")).not.toBeInTheDocument();
    await userEvent.upload(fileInput(), new File(["%PDF-1.7"], "5120.pdf", { type: "application/pdf" }));
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(replaceStandardVersionFile).toHaveBeenCalledWith(expect.any(String), "jtg-5120", expect.objectContaining({ name: "5120.pdf" }));
  });

  it("shows the server error above the form and keeps the input", async () => {
    vi.mocked(addStandardVersion).mockRejectedValue(new ApiError("standard_version_label_taken", "重名"));
    render(<StandardVersionFormModal mode={{ type: "add", document: maintenanceDocument }} onClose={vi.fn()} onSaved={vi.fn()} />);

    await userEvent.type(screen.getByLabelText("版本名称"), "2021 版");
    await userEvent.type(screen.getByLabelText("标准编号"), "JTG 5120—2021");
    await userEvent.type(screen.getByLabelText("实施日期"), "2021-11-01{Enter}");
    await userEvent.upload(fileInput(), new File(["%PDF-1.7"], "5120.pdf", { type: "application/pdf" }));
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(await screen.findByText("这部规范里已有同名版本。")).toBeInTheDocument();
    expect(screen.getByLabelText("版本名称")).toHaveValue("2021 版");
  });
});
