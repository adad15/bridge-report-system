import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createInspectionYear, fetchRatingTreeVersions } from "../api/workspaceApi";
import { CreateInspectionDialog } from "./CreateInspectionDialog";

vi.mock("../api/workspaceApi", () => ({
  createInspectionYear: vi.fn(),
  fetchRatingTreeVersions: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

const trees = [{
  id: "rating-tree-1",
  tree_name: "单位桥梁有效评定树",
  package_version: "1.0.0",
  h21_package_version: "1.0.1",
  maintenance_package_version: "1.0.0",
  is_default: true,
}];

describe("CreateInspectionDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchRatingTreeVersions).mockResolvedValue(trees as never);
  });

  it("creates the selected year and returns its id", async () => {
    vi.mocked(createInspectionYear).mockResolvedValue({ id: "year-1" } as never);
    const onCreated = vi.fn();
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={onCreated} />);
    expect(await screen.findByRole("option", { name: /单位桥梁有效评定树.*H21 1.0.1.*JTG 5120 1.0.0/ })).toBeInTheDocument();
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "2028");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(createInspectionYear).toHaveBeenCalledWith(expect.any(String), "bridge-1", {
      inspection_year: 2028,
      rating_tree_version_id: "rating-tree-1",
    });
    expect(onCreated).toHaveBeenCalledWith("year-1", false);
  });

  it("blocks an out-of-range year before requesting", async () => {
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={vi.fn()} />);
    await screen.findByRole("option", { name: /单位桥梁有效评定树/ });
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "1800");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(screen.getByRole("alert")).toHaveTextContent("1900 至 2200");
    expect(createInspectionYear).not.toHaveBeenCalled();
  });

  it("auto-selects the marked default when multiple tree versions are published", async () => {
    vi.mocked(fetchRatingTreeVersions).mockResolvedValue([
      { ...trees[0], is_default: false },
      { ...trees[0], id: "rating-tree-2", package_version: "2.0.0", is_default: true },
    ] as never);
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={vi.fn()} />);
    const tree = await screen.findByRole("combobox", { name: "桥梁评定树" });
    expect(tree).toHaveValue("rating-tree-2");
  });
});
