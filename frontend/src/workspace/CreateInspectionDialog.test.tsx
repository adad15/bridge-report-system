import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createInspectionYear } from "../api/workspaceApi";
import { fetchStandardPackages } from "../api/standardsApi";
import { CreateInspectionDialog } from "./CreateInspectionDialog";

vi.mock("../api/workspaceApi", () => ({
  createInspectionYear: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

vi.mock("../api/standardsApi", () => ({
  fetchStandardPackages: vi.fn(),
  standardsErrorMessage: (error: Error) => error.message,
}));

const packages = [
  { id: "technical-1", family: "technical_condition", standard_code: "JTG/T H21—2011", official_edition: "2011", package_version: "1.0.0", is_enabled: true, sync_status: "正常" },
  { id: "maintenance-1", family: "maintenance", standard_code: "JTG 5120—2021", official_edition: "2021", package_version: "1.0.0", is_enabled: true, sync_status: "正常" },
];

describe("CreateInspectionDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchStandardPackages).mockResolvedValue(packages as never);
  });

  it("creates the selected year and returns its id", async () => {
    vi.mocked(createInspectionYear).mockResolvedValue({ id: "year-1" } as never);
    const onCreated = vi.fn();
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={onCreated} />);
    expect(await screen.findByRole("option", { name: /JTG\/T H21—2011.*规则包 1.0.0/ })).toBeInTheDocument();
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "2028");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(createInspectionYear).toHaveBeenCalledWith(expect.any(String), "bridge-1", {
      inspection_year: 2028,
      technical_condition_package_id: "technical-1",
      maintenance_package_id: "maintenance-1",
    });
    expect(onCreated).toHaveBeenCalledWith("year-1", false);
  });

  it("blocks an out-of-range year before requesting", async () => {
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={vi.fn()} />);
    await screen.findByRole("option", { name: /JTG\/T H21—2011/ });
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "1800");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(screen.getByRole("alert")).toHaveTextContent("1900 至 2200");
    expect(createInspectionYear).not.toHaveBeenCalled();
  });

  it("does not auto-select when multiple versions are enabled", async () => {
    vi.mocked(fetchStandardPackages).mockResolvedValue([
      ...packages,
      { ...packages[0], id: "technical-2", package_version: "2.0.0" },
    ] as never);
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={vi.fn()} />);
    const technical = await screen.findByRole("combobox", { name: "技术状况评定标准" });
    expect(technical).toHaveValue("");
    expect(screen.getByRole("combobox", { name: "桥涵养护规范" })).toHaveValue("maintenance-1");
  });
});
