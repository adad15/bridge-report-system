import { render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { data } from "../testFixtures";
import { DefectsSection } from "./DefectsSection";

describe("DefectsSection", () => {
  it("natively disables every editable control in a read-only review", () => {
    const draft = data();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" selectedCandidateId="defect_0001" selectedPhotoCandidateId="photo_0001" onSelect={vi.fn()} dispatch={vi.fn()} disabled />);

    const controls = [...screen.getAllByRole("textbox"), ...screen.getAllByRole("combobox"), ...screen.getAllByRole("button")];
    expect(controls.length).toBeGreaterThan(0);
    controls.forEach((control) => expect(control).toBeDisabled());
  });
});
