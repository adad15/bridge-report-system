import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import type { DefectIssueGroup } from "../defectIssueGroups";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import { DefectIssueGroupConfirmDialog } from "./DefectIssueGroupConfirmDialog";

it("explains no-photo confirmation and excludes rows with other blockers", () => {
  const confirmable = { candidateId: "defect-1" } as DefectReviewRow;
  const blocked = { candidateId: "defect-2" } as DefectReviewRow;
  const group = {
    title: "渗水泛碱",
    rows: [confirmable, blocked],
    rangeSplitConfirmableRows: [confirmable],
  } as DefectIssueGroup;
  const onConfirm = vi.fn();

  render(
    <DefectIssueGroupConfirmDialog
      group={group}
      onCancel={vi.fn()}
      onConfirm={onConfirm}
    />,
  );

  expect(screen.getByText("无照片记录也会被确认；现有病害选择和照片关联保持不变。")).toBeInTheDocument();
  expect(screen.getByText("另有 1 条存在其他待处理问题，本次不会确认。")).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "确认 1 条" }));
  expect(onConfirm).toHaveBeenCalledOnce();
});
