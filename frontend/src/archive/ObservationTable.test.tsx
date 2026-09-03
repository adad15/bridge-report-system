import { render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { ArchiveMeasurement, ArchiveObservation } from "../api/componentArchiveApi";
import { ObservationTable } from "./ObservationTable";

vi.mock("../api/componentArchiveApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentArchiveApi")>();
  return { ...original, fetchObservationEvidence: vi.fn() };
});

function measurement(overrides: Partial<ArchiveMeasurement> = {}): ArchiveMeasurement {
  return {
    measurement_type: "面积",
    value_type: "single",
    numeric_value: 80,
    minimum_value: null,
    maximum_value: null,
    unit: "m2",
    is_approximate: false,
    raw_text: "80.0m²",
    ...overrides,
  };
}

function observation(overrides: Partial<ArchiveObservation> = {}): ArchiveObservation {
  return {
    id: "o-1",
    system_number: "BH-000001",
    inspection_year: 2026,
    defect_thread_id: null,
    defect_type: "横向裂缝",
    defect_location: "3#墩顶",
    scale: "2",
    defect_description: "横向裂缝",
    review_status: "已确认",
    updated_at: "2026-08-26 10:00:00+08",
    measurements: [measurement()],
    photos: [{ id: "p-1", photo_number: "2.3-14", photo_title: null }],
    ...overrides,
  };
}

describe("ObservationTable", () => {
  // 线索卡、待整理观测区、历史修订共用这一份列定义，改列会同时影响三处。
  it("shows 尺寸 instead of 照片 as a column", () => {
    render(<ObservationTable observations={[observation()]} />);

    const headers = screen.getAllByRole("columnheader").map((cell) => cell.textContent);
    expect(headers).toEqual(["年度", "病害类型", "位置", "标度", "尺寸", "操作"]);
    expect(screen.queryByRole("columnheader", { name: "照片" })).not.toBeInTheDocument();
  });

  it("renders measurement raw text, joining multiple entries", () => {
    render(
      <ObservationTable
        observations={[
          observation({
            measurements: [measurement(), measurement({ measurement_type: "长度", raw_text: "0.5~4.0m" })],
          }),
        ]}
      />,
    );

    expect(screen.getByText("80.0m²；0.5~4.0m")).toBeInTheDocument();
  });

  // 缺尺寸是正常数据，不应该显示 "0" 或空白单元格。
  it("falls back to a dash when the year has no measurement", () => {
    render(<ObservationTable observations={[observation({ measurements: [] })]} />);

    const row = screen.getAllByRole("row")[1];
    expect(within(row).getByText("-")).toBeInTheDocument();
  });
});
