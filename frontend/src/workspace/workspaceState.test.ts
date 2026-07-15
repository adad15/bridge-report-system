import { describe, expect, it } from "vitest";

import {
  deriveInspectionProgress,
  inspectionWorkspacePath,
  reviewPath,
} from "./workspaceState";

describe("workspaceState", () => {
  it.each([
    ["待校对", [], "empty"],
    ["待校对", [{ import_status: "已上传" }], "uploaded"],
    ["待校对", [{ import_status: "解析中" }], "parsing"],
    ["待校对", [{ import_status: "解析失败" }], "parse_failed"],
    ["待校对", [{ import_status: "待校对" }], "review"],
    ["已确认", [{ import_status: "已取消" }], "completed"],
    ["已归档", [], "completed"],
  ])("derives %s / imports as %s", (status, imports, expected) => {
    expect(deriveInspectionProgress({ status }, imports).stage).toBe(expected);
  });

  it("encodes every dynamic route segment", () => {
    expect(inspectionWorkspacePath("bridge/1", "year 1")).toBe("/bridges/bridge%2F1/inspections/year%201");
    expect(reviewPath("bridge/1", "year 1", "import#1")).toBe(
      "/bridges/bridge%2F1/inspections/year%201/imports/import%231/review"
    );
  });
});
