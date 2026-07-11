import { render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { NeedsAttentionSection } from "./NeedsAttentionSection";

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
