import { describe, expect, it } from "vitest";

import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
  PhotoReference,
} from "../contracts/annualInspection";
import { buildDefectPhotoCards } from "./defectPhotoCards";
import { data } from "./testFixtures";

function photo(overrides: Partial<PhotoCandidate>): PhotoCandidate {
  return {
    candidate_id: "photo_x",
    photo_number: "2.1-5",
    linked_defect_candidate_id: "defect_0001",
    extracted_file: {
      temporary_file_name: "photo.jpg",
      original_caption: null,
      archive_relative_path: "photos/photo.jpg",
    },
    match_status: "高置信候选",
    source_ref: { source_type: "word" },
    confidence: 0.9,
    review_status: "待确认",
    warnings: [],
    ...overrides,
  };
}

function reference(overrides: Partial<PhotoReference>): PhotoReference {
  return {
    photo_number: "2.1-5",
    resolution: "pending",
    photo_candidate_id: null,
    resolved_defect_candidate_id: null,
    review_note: null,
    ...overrides,
  };
}

/** Word 引用 2.1-5 与 2.1-6；只有 2.1-5 抽到了图；另有一张人工上传的补-1。 */
function draftWithThreeCards(): { draft: BridgeAnnualInspectionData; defect: DefectCandidate } {
  const draft = data();
  draft.defects[0] = {
    ...draft.defects[0],
    photo_references: [
      reference({ photo_number: "2.1-5" }),
      reference({ photo_number: "2.1-6" }),
    ],
  };
  draft.photos = [
    photo({ candidate_id: "photo_0001", photo_number: "2.1-5" }),
    photo({
      candidate_id: "manual_photo_0001",
      photo_number: "补-1",
      source_ref: { source_type: "manual" },
      match_status: "已确认",
      review_status: "已确认",
    }),
  ];
  return { draft, defect: draft.defects[0] };
}

describe("buildDefectPhotoCards", () => {
  it("lists linked photos, missing Word numbers and uploaded photos in one stable order", () => {
    const { draft, defect } = draftWithThreeCards();

    const cards = buildDefectPhotoCards(draft, defect);

    expect(cards.map((card) => [card.kind, card.photoNumber])).toEqual([
      ["photo", "2.1-5"],
      ["missing", "2.1-6"],
      ["photo", "补-1"],
    ]);
    // Word 承诺的编号按原文顺序在前，人工补充的排在后面。
    expect(cards[0].source).toBe("word");
    expect(cards[2].source).toBe("manual");
    expect(buildDefectPhotoCards(draft, defect)).toEqual(cards);
  });

  it("marks an acknowledged missing photo as handled", () => {
    const { draft, defect } = draftWithThreeCards();
    defect.photo_references[1] = reference({
      photo_number: "2.1-6",
      resolution: "missing",
    });

    const cards = buildDefectPhotoCards(draft, defect);

    const placeholder = cards.find((card) => card.photoNumber === "2.1-6");
    expect(placeholder?.kind).toBe("missing");
    expect(placeholder?.acknowledgedMissing).toBe(true);
  });

  // 新模型不再产生 relinked / unrelated；存量草稿里的这两个值要重新回到待核对，
  // 不能被当成已处理悄悄放过（设计 §9.4）。
  it("re-surfaces legacy relinked and unrelated references as unhandled", () => {
    for (const legacy of ["relinked", "unrelated"] as const) {
      const { draft, defect } = draftWithThreeCards();
      defect.photo_references[1] = reference({
        photo_number: "2.1-6",
        resolution: legacy,
        photo_candidate_id: "photo_other",
        resolved_defect_candidate_id: legacy === "relinked" ? "defect_0002" : null,
      });

      const placeholder = buildDefectPhotoCards(draft, defect)
        .find((card) => card.photoNumber === "2.1-6");

      expect(placeholder?.kind).toBe("missing");
      expect(placeholder?.acknowledgedMissing).toBe(false);
    }
  });

  it("turns a Word photo back into a placeholder once it is unlinked", () => {
    const { draft, defect } = draftWithThreeCards();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };

    const cards = buildDefectPhotoCards(draft, defect);

    expect(cards.map((card) => [card.kind, card.photoNumber])).toEqual([
      ["missing", "2.1-5"],
      ["missing", "2.1-6"],
      ["photo", "补-1"],
    ]);
  });

  it("reports whether each photo card is already confirmed", () => {
    const { draft, defect } = draftWithThreeCards();

    const cards = buildDefectPhotoCards(draft, defect);

    expect(cards.find((card) => card.photoNumber === "2.1-5")?.confirmed).toBe(false);
    expect(cards.find((card) => card.photoNumber === "补-1")?.confirmed).toBe(true);
  });

  it("ignores photos linked to another defect", () => {
    const { draft, defect } = draftWithThreeCards();
    draft.photos.push(photo({
      candidate_id: "photo_other",
      photo_number: "2.2-1",
      linked_defect_candidate_id: "defect_0002",
    }));

    expect(buildDefectPhotoCards(draft, defect).map((card) => card.photoNumber))
      .toEqual(["2.1-5", "2.1-6", "补-1"]);
  });
});
