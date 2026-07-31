import { describe, expect, it } from "vitest";

import { displayDefectLocation } from "./displayHelpers";

describe("displayDefectLocation", () => {
  it("hides the Word placeholders that stand for an empty location", () => {
    for (const placeholder of ["", "  ", "/", "／", null, undefined]) {
      expect(displayDefectLocation(placeholder)).toBeNull();
    }
  });

  it("keeps a real location and trims it", () => {
    expect(displayDefectLocation(" 左侧翼缘板及腹板 ")).toBe("左侧翼缘板及腹板");
    expect(displayDefectLocation("1/4 跨")).toBe("1/4 跨");
  });
});
