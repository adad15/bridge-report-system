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
    source_ref: { source_type: "word" },
    confidence: 0.9,
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

  it("does not add a second confirmation state to photo cards", () => {
    const { draft, defect } = draftWithThreeCards();

    const cards = buildDefectPhotoCards(draft, defect);

    expect(cards.every((card) => !("confirmed" in card))).toBe(true);
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

  it("pairs unnumbered source-software photos by candidate id", () => {
    // 来源软件导入没有照片编号：照片靠外键绑在病害上，引用带的是 photo_candidate_id。
    // 若仍按编号配对，null === null 会让三条引用全部命中第一张图。
    const draft = data();
    draft.defects[0] = {
      ...draft.defects[0],
      photo_references: [
        reference({ photo_number: null, resolution: "matched", photo_candidate_id: "src_1", resolved_defect_candidate_id: "defect_0001" }),
        reference({ photo_number: null, resolution: "matched", photo_candidate_id: "src_2", resolved_defect_candidate_id: "defect_0001" }),
        reference({ photo_number: null, resolution: "matched", photo_candidate_id: "src_3", resolved_defect_candidate_id: "defect_0001" }),
      ],
    };
    draft.photos = [
      photo({ candidate_id: "src_1", photo_number: null }),
      photo({ candidate_id: "src_2", photo_number: null }),
      photo({ candidate_id: "src_3", photo_number: null }),
    ];

    const cards = buildDefectPhotoCards(draft, draft.defects[0]);

    expect(cards.map((card) => card.kind)).toEqual(["photo", "photo", "photo"]);
    expect(cards.map((card) => card.photo?.candidate_id)).toEqual(["src_1", "src_2", "src_3"]);
    expect(cards.map((card) => card.photoNumber)).toEqual([null, null, null]);
    // 渲染 key 必须互不相同，否则 React 会把三张图当成同一张。
    expect(new Set(cards.map((card) => card.key)).size).toBe(3);
  });

  it("keeps distinct keys when unnumbered references lose their photo", () => {
    // 照片被摘除后引用还在：来源软件路这时既没编号也找不到图，key 仍要互不相同。
    const draft = data();
    draft.defects[0] = {
      ...draft.defects[0],
      photo_references: [
        reference({ photo_number: null, resolution: "matched", photo_candidate_id: "src_1", resolved_defect_candidate_id: "defect_0001" }),
        reference({ photo_number: null, resolution: "matched", photo_candidate_id: "src_2", resolved_defect_candidate_id: "defect_0001" }),
      ],
    };
    draft.photos = [];

    const cards = buildDefectPhotoCards(draft, draft.defects[0]);

    expect(cards.map((card) => card.kind)).toEqual(["missing", "missing"]);
    expect(new Set(cards.map((card) => card.key)).size).toBe(2);
  });
});
