import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";

import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";
import { data as fixtureData } from "../testFixtures";

describe("UnlinkedPhotosPanel", () => {
  it("keeps an unlinked photo visible with its count", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null, review_status: "已忽略" };

    render(<UnlinkedPhotosPanel draft={draft} importRecordId="record-1" baseUrl="http://backend" />);

    expect(screen.getByRole("img", { name: `照片 ${draft.photos[0].photo_number}` })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "未归属的照片（1 张）" })).toBeInTheDocument();
  });

  // 归属只有病害卡片里的「添加照片」一个入口，这里不再提供第二套说法。
  it("offers no ownership actions of its own", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };

    render(<UnlinkedPhotosPanel draft={draft} importRecordId="record-1" baseUrl="http://backend" />);

    expect(screen.queryByRole("button", { name: "关联到病害" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "重置校对状态" })).not.toBeInTheDocument();
    expect(screen.queryByRole("combobox", { name: "目标病害" })).not.toBeInTheDocument();
    // 只剩缩略图这一类只读按钮。
    expect(screen.getAllByRole("button")).toHaveLength(1);
  });

  it("explains the raw status fields instead of dumping them", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = {
      ...draft.photos[0],
      linked_defect_candidate_id: null,
      match_status: "高置信候选",
      review_status: "待确认",
    };

    render(<UnlinkedPhotosPanel draft={draft} importRecordId="record-1" baseUrl="http://backend" />);

    expect(screen.getByText("系统判断：高置信候选 · 我的处理：待确认")).toBeInTheDocument();
  });
});
