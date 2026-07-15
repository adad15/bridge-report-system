import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createInspectionYear } from "../api/workspaceApi";
import { CreateInspectionDialog } from "./CreateInspectionDialog";

vi.mock("../api/workspaceApi", () => ({
  createInspectionYear: vi.fn(),
  workspaceErrorMessage: (error: Error) => error.message,
}));

describe("CreateInspectionDialog", () => {
  beforeEach(() => vi.resetAllMocks());

  it("creates the selected year and returns its id", async () => {
    vi.mocked(createInspectionYear).mockResolvedValue({ id: "year-1" } as never);
    const onCreated = vi.fn();
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={onCreated} />);
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "2028");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(createInspectionYear).toHaveBeenCalledWith(expect.any(String), "bridge-1", 2028);
    expect(onCreated).toHaveBeenCalledWith("year-1", false);
  });

  it("blocks an out-of-range year before requesting", async () => {
    render(<CreateInspectionDialog bridgeId="bridge-1" onClose={vi.fn()} onCreated={vi.fn()} />);
    const yearInput = screen.getByRole("spinbutton", { name: "检测年度" });
    await userEvent.clear(yearInput);
    await userEvent.type(yearInput, "1800");
    await userEvent.click(screen.getByRole("button", { name: "创建年度" }));
    expect(screen.getByRole("alert")).toHaveTextContent("1900 至 2200");
    expect(createInspectionYear).not.toHaveBeenCalled();
  });
});
