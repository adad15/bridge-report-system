import { describe, expect, it } from "vitest";

import { data } from "./testFixtures";
import {
  attentionObjectLabel,
  defectFieldForWarning,
  defectSequence,
  reviewTargetId,
} from "./reviewNavigation";

describe("review navigation helpers", () => {
  it("derives display-only defect sequence from draft order", () => {
    const draft = data();
    expect(defectSequence(draft, draft.defects[0].candidate_id)).toBe(1);
    expect(defectSequence(draft, "missing")).toBeNull();
  });

  it("maps known warning codes to editable fields", () => {
    expect(defectFieldForWarning("defect_scale_invalid")).toBe("defect_scale");
    expect(defectFieldForWarning("photo_number_unmatched")).toBe("photo_references");
    expect(defectFieldForWarning("unknown")).toBeUndefined();
  });

  it("formats defect and linked photo labels with the business sequence", () => {
    const draft = data();
    expect(attentionObjectLabel({ kind: "defect", candidateId: draft.defects[0].candidate_id, message: "x", severity: "warning" }, draft)).toBe("病害 1");
    expect(attentionObjectLabel({ kind: "photo", candidateId: draft.photos[0].candidate_id, message: "x", severity: "warning" }, draft)).toBe(`病害 1 · 照片 ${draft.photos[0].photo_number}`);
  });

  it("creates encoded stable DOM ids", () => {
    expect(reviewTargetId("defect-field", "defect 1", "component_alias")).toBe("review-target-defect-field-defect%201-component_alias");
  });
});
