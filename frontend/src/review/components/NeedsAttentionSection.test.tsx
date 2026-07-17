import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { NeedsAttentionSection } from "./NeedsAttentionSection";
import { data } from "../testFixtures";

it("renders blocking errors separately from warnings", () => {
  render(
    <NeedsAttentionSection
      items={[
        { kind: "photo", candidateId: "photo_1", message: "归档缺失", severity: "error" },
        { kind: "defect", candidateId: "defect_1", message: "尚未确认", severity: "warning" },
      ]}
      draft={{ defects: [], photos: [] } as never}
      onSelect={vi.fn()}
    />
  );

  expect(screen.getByRole("heading", { name: "错误" })).toBeInTheDocument();
  expect(screen.getByRole("heading", { name: "警告" })).toBeInTheDocument();
  expect(screen.getByText("归档缺失")).toBeInTheDocument();
  expect(screen.getByText("尚未确认")).toBeInTheDocument();
});

it("shows a business defect sequence and returns the complete attention item", () => {
  const draft = data();
  const item = { kind: "defect" as const, candidateId: draft.defects[0].candidate_id, message: "构件编号为空", severity: "warning" as const, targetField: "component_alias" as const };
  const onSelect = vi.fn();

  render(<NeedsAttentionSection items={[item]} draft={draft} onSelect={onSelect} />);

  expect(screen.getByText("病害 1")).toBeInTheDocument();
  fireEvent.click(screen.getByText("构件编号为空"));
  expect(onSelect).toHaveBeenCalledWith(item);
});
