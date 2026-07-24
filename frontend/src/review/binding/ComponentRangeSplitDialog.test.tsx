import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { ComponentRangeSplitDialog } from "./ComponentRangeSplitDialog";

const targets = [{ part_name: "上部承重构件", component_number: "1-1#梁~1-25#梁" }];
const preview = {
  items: [{
    ...targets[0],
    expanded_component_count: 25,
    source_defect_count: 3,
    result_defect_count: 75,
    result_photo_count: 75,
    bound_count: 70,
    ambiguous_count: 3,
    unmatched_count: 2,
  }],
  totals: {
    selected_range_count: 1,
    source_defect_count: 3,
    result_defect_count: 75,
    result_photo_count: 75,
    bound_count: 70,
    ambiguous_count: 3,
    unmatched_count: 2,
  },
  impact_token: "sha256:preview",
};

describe("ComponentRangeSplitDialog", () => {
  it("shows scoring impact and applies the exact preview token", () => {
    const onApply = vi.fn();
    render(
      <ComponentRangeSplitDialog
        preview={preview}
        targets={targets}
        busy={false}
        error={null}
        onClose={() => undefined}
        onApply={onApply}
      />
    );
    expect(screen.getByText(/总扣分增加/)).toBeInTheDocument();
    expect(screen.getByText("3 → 75")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "确认拆分" }));
    expect(onApply).toHaveBeenCalledWith(targets, "sha256:preview");
  });
});
