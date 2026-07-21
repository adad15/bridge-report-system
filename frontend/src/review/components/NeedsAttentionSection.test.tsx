import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { NeedsAttentionSection } from "./NeedsAttentionSection";
import type { AttentionItem } from "../grouping";
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

it("paginates a large warning list at 100 rows per page", () => {
  const items: AttentionItem[] = Array.from({ length: 250 }, (_, index) => ({
    kind: "import" as const,
    candidateId: `import_${index}`,
    message: `问题 ${index}`,
    severity: "warning" as const,
  }));

  render(<NeedsAttentionSection items={items} draft={{ defects: [], photos: [] } as never} onSelect={vi.fn()} />);

  expect(screen.getByText("问题 0")).toBeInTheDocument();
  expect(screen.queryByText("问题 100")).not.toBeInTheDocument();
  expect(screen.getByText("第 1 / 3 页（共 250 条）")).toBeInTheDocument();

  fireEvent.click(screen.getByRole("button", { name: "下一页" }));
  expect(screen.getByText("问题 100")).toBeInTheDocument();
  expect(screen.queryByText("问题 0")).not.toBeInTheDocument();
});

it("shows a business defect sequence and returns the complete attention item", () => {
  const draft = data();
  const item = { kind: "defect" as const, candidateId: draft.defects[0].candidate_id, message: "构件编号为空", severity: "warning" as const, targetField: "component_number" as const };
  const onSelect = vi.fn();

  render(<NeedsAttentionSection items={[item]} draft={draft} onSelect={onSelect} />);

  expect(screen.getByText("病害 1")).toBeInTheDocument();
  fireEvent.click(screen.getByText("构件编号为空"));
  expect(onSelect).toHaveBeenCalledWith(item);
});
