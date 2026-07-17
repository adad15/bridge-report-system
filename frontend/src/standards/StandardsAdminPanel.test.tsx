import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchStandardPackages, setStandardPackageEnabled } from "../api/standardsApi";
import { StandardsAdminPanel } from "./StandardsAdminPanel";

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardPackages: vi.fn(), setStandardPackageEnabled: vi.fn() };
});

const technical = {
  id: "technical-1", family: "technical_condition" as const, standard_id: "H21",
  standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准",
  official_edition: "2011", package_version: "1.0.0", contract_version: 1,
  algorithm_id: "h21", effective_date: "2011-09-01", content_checksum: "sha256:test",
  is_enabled: true, sync_status: "正常" as const, sync_error_code: null, sync_error_message: null,
};

describe("StandardsAdminPanel", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchStandardPackages).mockResolvedValue([technical]);
  });

  it("shows standard identity and package version", async () => {
    render(<StandardsAdminPanel onClose={vi.fn()} />);
    expect(await screen.findByText(/JTG\/T H21—2011/)).toBeInTheDocument();
    expect(screen.getByText(/规则包 1.0.0/)).toBeInTheDocument();
    expect(screen.getByText("已启用")).toBeInTheDocument();
  });

  it("lets an administrator disable a healthy package", async () => {
    vi.mocked(setStandardPackageEnabled).mockResolvedValue({ ...technical, is_enabled: false });
    render(<StandardsAdminPanel onClose={vi.fn()} />);
    await userEvent.click(await screen.findByRole("button", { name: "停用" }));
    expect(setStandardPackageEnabled).toHaveBeenCalledWith(expect.any(String), "technical-1", false);
    expect(await screen.findByText("已停用")).toBeInTheDocument();
  });
});
