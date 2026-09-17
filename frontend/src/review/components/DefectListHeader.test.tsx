import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { DefectListHeader } from "./DefectListHeader";

function renderHeader(overrides: Partial<Parameters<typeof DefectListHeader>[0]> = {}) {
  const props = {
    total: 12,
    selectedCount: 0,
    selectableCount: 8,
    allSelectableSelected: false,
    someSelectableSelected: false,
    onToggleSelectAll: vi.fn(),
    onBatchConfirm: vi.fn(),
    ...overrides,
  };
  const result = render(<DefectListHeader {...props} />);
  return { ...result, props };
}

it("selects every batch-eligible defect in the current filter", () => {
  const { props } = renderHeader();

  fireEvent.click(screen.getByRole("checkbox", { name: "全选可确认项（8）" }));

  expect(props.onToggleSelectAll).toHaveBeenCalledTimes(1);
});

it("shows partial and complete selection states", () => {
  const { rerender, props } = renderHeader({ selectedCount: 3, someSelectableSelected: true });
  const checkbox = screen.getByRole("checkbox", { name: "全选可确认项（8）" }) as HTMLInputElement;
  expect(checkbox.indeterminate).toBe(true);
  expect(checkbox).not.toBeChecked();

  rerender(<DefectListHeader {...props} selectedCount={8} allSelectableSelected someSelectableSelected={false} />);
  expect(checkbox.indeterminate).toBe(false);
  expect(checkbox).toBeChecked();
});

it("disables select all when the current filter has no batch-eligible defects", () => {
  renderHeader({ selectableCount: 0 });

  expect(screen.getByRole("checkbox", { name: "全选可确认项（0）" })).toBeDisabled();
});

// 批量确认只作用于勾选项：一条都没勾时不该能点。
it("enables batch confirmation only once something is selected", () => {
  const { rerender, props } = renderHeader();
  expect(screen.getByRole("button", { name: "批量确认" })).toBeDisabled();

  rerender(<DefectListHeader {...props} selectedCount={2} someSelectableSelected />);
  fireEvent.click(screen.getByRole("button", { name: "批量确认（2）" }));
  expect(props.onBatchConfirm).toHaveBeenCalledTimes(1);
});

// 只读态看的是结果，勾选和批量确认整片不出现，而不是一排灰掉的控件。
it("shows only the count in a read-only review", () => {
  renderHeader({ readOnly: true });

  expect(screen.getByText("病害 12 条")).toBeInTheDocument();
  expect(screen.queryByRole("checkbox")).not.toBeInTheDocument();
  expect(screen.queryByRole("button", { name: /批量确认/ })).not.toBeInTheDocument();
});
