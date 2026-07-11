import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";
import { data as fixtureData } from "../testFixtures";

describe("UnlinkedPhotosPanel", () => {
  it("keeps an unlinked photo visible and lets the reviewer relink it", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null, review_status: "已忽略" };
    const dispatch = vi.fn();
    render(<UnlinkedPhotosPanel draft={draft} importRecordId="record-1" baseUrl="http://backend" dispatch={dispatch} />);

    expect(screen.getByRole("img", { name: `照片 ${draft.photos[0].photo_number}` })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "关联到病害" }));
    expect(dispatch).toHaveBeenCalledWith({ type: "photo_relink", candidateId: draft.photos[0].candidate_id, defectCandidateId: draft.defects[0].candidate_id });
  });
});
