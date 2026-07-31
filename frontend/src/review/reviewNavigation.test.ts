import { describe, expect, it } from "vitest";

import { reviewTargetId } from "./reviewNavigation";

describe("review navigation helpers", () => {
  it("creates encoded stable DOM ids", () => {
    expect(reviewTargetId("defect-field", "defect 1", "component_alias")).toBe("review-target-defect-field-defect%201-component_alias");
  });
});
