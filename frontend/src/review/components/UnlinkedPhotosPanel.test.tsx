import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";

import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";
import { data as fixtureData } from "../testFixtures";

describe("UnlinkedPhotosPanel", () => {
  it("keeps an unlinked photo visible with its count", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };

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
    // 只剩缩略图这一类只读按钮（antd 图片自带的放大遮罩不算）。
    expect(screen.getAllByRole("button", { name: /查看未归属照片/ })).toHaveLength(1);
  });

  it("shows the photo caption without obsolete review state", () => {
    const draft: BridgeAnnualInspectionData = fixtureData();
    draft.photos[0] = {
      ...draft.photos[0],
      linked_defect_candidate_id: null,
      extracted_file: { ...draft.photos[0].extracted_file, original_caption: "梁底裂缝" },
    };

    render(<UnlinkedPhotosPanel draft={draft} importRecordId="record-1" baseUrl="http://backend" />);

    expect(screen.getByText("梁底裂缝")).toBeInTheDocument();
    expect(screen.queryByText(/系统判断|我的处理/)).not.toBeInTheDocument();
  });
});
