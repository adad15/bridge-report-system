import { describe, expect, it } from "vitest";

import type { PreflightResponse } from "../api/reviewApi";
import { canPressConfirm, parsePreflightDetails, validateRevisionForm } from "./confirmFlow";

function preflight(overrides: Partial<PreflightResponse> = {}): PreflightResponse {
  return {
    can_confirm: false,
    requires_revision_confirmation: false,
    blocking_errors: [],
    warnings: [],
    ...overrides,
  };
}

describe("canPressConfirm", () => {
  it("returns false when no preflight check has run yet", () => {
    expect(canPressConfirm(null)).toBe(false);
  });

  it("returns false when the latest preflight still reports can_confirm=false", () => {
    expect(canPressConfirm(preflight({ can_confirm: false }))).toBe(false);
  });

  it("returns true when the latest preflight reports can_confirm=true", () => {
    expect(canPressConfirm(preflight({ can_confirm: true }))).toBe(true);
  });
});

describe("validateRevisionForm", () => {
  it("rejects when the revision checkbox is unchecked, regardless of note content", () => {
    const result = validateRevisionForm(false, "已核实为修订版");
    expect(result.valid).toBe(false);
    expect(result.error).toMatch(/修订版/);
  });

  it("rejects an empty or blank confirmation note even when checked", () => {
    expect(validateRevisionForm(true, "").valid).toBe(false);
    expect(validateRevisionForm(true, "   ").valid).toBe(false);
    expect(validateRevisionForm(true, "").error).toMatch(/说明/);
  });

  it("accepts a checked box paired with a non-blank note", () => {
    expect(validateRevisionForm(true, "作为修订版确认入库")).toEqual({ valid: true });
  });
});

describe("parsePreflightDetails", () => {
  it("recognizes a well-formed PreflightReport-shaped body", () => {
    const details = preflight({
      can_confirm: false,
      blocking_errors: [{ code: "candidate_pending_review", message: "仍有待确认候选。", target_candidate_id: "defect_0005" }],
    });
    expect(parsePreflightDetails(details)).toEqual(details);
  });

  it("returns null for details that are not a PreflightReport shape", () => {
    expect(parsePreflightDetails(null)).toBeNull();
    expect(parsePreflightDetails(undefined)).toBeNull();
    expect(parsePreflightDetails("oops")).toBeNull();
    expect(parsePreflightDetails({ code: "some_unrelated_error" })).toBeNull();
  });
});
