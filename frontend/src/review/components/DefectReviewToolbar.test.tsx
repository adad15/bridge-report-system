import { render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { chooseOption, findOption, optionLabels } from "../../test/antd";
import { DefectReviewToolbar } from "./DefectReviewToolbar";

const summary = {
  parts: [{ name: "板", count: 7 }, { name: "铰缝", count: 5 }],
  defectTypes: [{ name: "渗水泛碱", count: 10 }, { name: "其它病害", count: 2 }],
  all: 12,
  pending: 4,
  batchable: 8,
  confirmed: 0,
  composite: 0,
  candidates: 0,
  unmatched: 0,
};

function renderToolbar({
  countsPending = false,
  matchCountsPending = false,
} = {}) {
  render(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      viewMode="records"
      issueGroupCount={4}
      countsPending={countsPending}
      matchCountsPending={matchCountsPending}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={vi.fn()}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onViewModeChange={vi.fn()}
      onRematch={vi.fn()}
    />,
  );
}

// 全选与批量确认已挪到列表表头，用例在 DefectListHeader.test.tsx。
it("keeps selection controls out of the toolbar", () => {
  renderToolbar();

  expect(screen.queryByRole("checkbox")).not.toBeInTheDocument();
  expect(screen.queryByRole("button", { name: /批量确认/ })).not.toBeInTheDocument();
});

// 评定树规则没到时，"待处理/可批量确认"是算不出来的：规则缺席会给每条病害记上一条
// 问题，而"可批量确认"的判据是一条问题都没有，于是全都落进待处理。显示成 362 与 0
// 会让人以为真是这样——加载期显示的必须是"还不知道"，不是一个假数字。
it("shows a dash instead of a fake zero while the rating tree rules load", async () => {
  renderToolbar({ countsPending: true });

  const labels = await optionLabels(screen.getByLabelText("按状态筛选"));
  expect(labels).toContain("待处理（—）");
  expect(labels).toContain("可批量确认（—）");
  // 已确认与全部不看问题，规则没到也照常算得出来，不该跟着变成"—"。
  expect(labels).toContain("已确认（0）");
  expect(labels).toContain("全部状态（12）");
});

// 留着能点的话，点进去是一屏空列表——那和"确实一条都没有"又长得一样，
// 等于换个地方继续误导。
it("disables the counts it cannot compute yet", async () => {
  renderToolbar({ countsPending: true });

  const statusFilter = screen.getByLabelText("按状态筛选");
  expect(await findOption(statusFilter, "待处理（—）")).toHaveAttribute("aria-disabled", "true");
  expect(await findOption(statusFilter, "可批量确认（—）")).toHaveAttribute("aria-disabled", "true");
  expect(await findOption(statusFilter, "全部状态（12）")).toHaveAttribute("aria-disabled", "false");
});

// 匹配结果没回来时没有任何一条会被判成"无匹配"，那个 0 同样是假的。
it("marks the match statistics as unknown until the match results arrive", async () => {
  renderToolbar({ matchCountsPending: true });

  const statusFilter = screen.getByLabelText("按状态筛选");
  expect(await findOption(statusFilter, "无匹配结果（—）")).toHaveAttribute("aria-disabled", "true");
  expect(await findOption(statusFilter, "疑似组合病害（—）")).toHaveAttribute("aria-disabled", "true");
  expect(await findOption(statusFilter, "有多个候选（—）")).toHaveAttribute("aria-disabled", "true");
  // 两路数据互不相干：匹配没到不该把待处理也说成未知。
  expect(await findOption(statusFilter, "待处理（4）")).toHaveAttribute("aria-disabled", "false");
});

it("shows the real counts once both sides are ready", async () => {
  renderToolbar();

  const labels = await optionLabels(screen.getByLabelText("按状态筛选"));
  expect(labels).toContain("待处理（4）");
  expect(labels).toContain("可批量确认（8）");
  expect(labels).toContain("无匹配结果（0）");
});

it("filters by imported defect type", async () => {
  const onDefectTypeFilterChange = vi.fn();
  render(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      viewMode="records"
      issueGroupCount={4}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={onDefectTypeFilterChange}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onViewModeChange={vi.fn()}
      onRematch={vi.fn()}
    />,
  );

  expect(screen.getByRole("textbox", { name: "搜索病害" })).toBeInTheDocument();
  await chooseOption(screen.getByLabelText("按病害类型筛选"), "其它病害（2）");
  expect(onDefectTypeFilterChange).toHaveBeenCalledWith("其它病害");
});
