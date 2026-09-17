import { Alert, Button, Divider, Flex, Input, Select, Space, Tooltip, theme } from "antd";
import { PlusOutlined, SearchOutlined, SyncOutlined } from "@ant-design/icons";

import { UNBOUND_PART_FILTER } from "../defectPhotoReviewModel";
import type {
  DefectPhotoReviewSummary,
  DefectReviewFilter,
  DefectReviewIssueFilter,
} from "../defectPhotoReviewModel";

interface DefectReviewToolbarProps {
  summary: DefectPhotoReviewSummary;
  filter: DefectReviewFilter;
  issueFilter: DefectReviewIssueFilter | null;
  search: string;
  viewMode: "records" | "groups";
  issueGroupCount: number;
  disabled?: boolean;
  /**
   * 评定树规则还没加载完。此时"待处理/可批量确认"算不出来——规则没到时每条病害都
   * 被记上一条"正在加载评定树规则"的问题，而"可批量确认"的判据是一条问题都没有，
   * 于是全都落进待处理。显示成 0 与 362 会让人以为真是这样，所以显示"—"。
   */
  countsPending?: boolean;
  /** 匹配结果还没回来。没有结果时没有任何一条会被判成"无匹配"，那个 0 是假的。 */
  matchCountsPending?: boolean;
  /** 当前只看哪个部件；null 表示全部。 */
  partFilter: string | null;
  onPartFilterChange: (partFilter: string | null) => void;
  /** 当前只看哪个导入病害类型；null 表示全部。 */
  defectTypeFilter: string | null;
  onDefectTypeFilterChange: (defectTypeFilter: string | null) => void;
  /** 重新匹配的作用域说明与预计处理条数，按钮按下前就要能看清。 */
  rematchScopeLabel: string;
  rematchCount: number;
  rematching?: boolean;
  matchError?: string | null;
  /** 不传就不渲染“新增病害”——只读态和重开校对的仅警告范围没有这个入口。 */
  onAddDefect?: () => void;
  addDefectDisabled?: boolean;
  onFilterChange: (filter: DefectReviewFilter) => void;
  onIssueFilterChange: (issueFilter: DefectReviewIssueFilter | null) => void;
  onSearchChange: (search: string) => void;
  onViewModeChange: (mode: "records" | "groups") => void;
  onRematch: () => void;
}

// 状态与匹配问题共用一个筛选器：主界面只保留一个“全部状态”下拉，避免把同一维度
// 同时做成七个统计按钮和一组筛选控件。完整计数仍放进选项文案，业务能力没有缩水。
type SummaryCountKey = Exclude<keyof DefectPhotoReviewSummary, "parts" | "defectTypes">;

const STATUS_FILTERS: Array<{
  value: DefectReviewFilter;
  label: string;
  count: SummaryCountKey;
  /** 该计数依赖评定树规则，规则没到时算不出来。已确认与全部不看问题，不受影响。 */
  needsTreeRules?: boolean;
}> = [
  { value: "needs_attention", label: "待处理", count: "pending", needsTreeRules: true },
  { value: "batchable", label: "可批量确认", count: "batchable", needsTreeRules: true },
  { value: "confirmed", label: "已确认", count: "confirmed" },
  { value: "all", label: "全部", count: "all" },
];

// 组合病害、多候选和无结果是三种完全不同的人工动作，顶部就要能分开点进去。
const ISSUE_FILTERS: Array<{
  value: DefectReviewIssueFilter;
  label: string;
  count: SummaryCountKey;
}> = [
  { value: "composite", label: "疑似组合病害", count: "composite" },
  { value: "candidates", label: "有多个候选", count: "candidates" },
  { value: "unmatched", label: "无匹配结果", count: "unmatched" },
];

// 构件、标度和照片问题的主入口在右侧“待处理问题”面板。点进去以后，组合筛选框仍要
// 如实显示当前条件；否则数据已经筛过，控件却还写着“待处理”，用户会误以为入口失效。
const QUALITY_ISSUE_LABELS: Partial<Record<DefectReviewIssueFilter, string>> = {
  component_unbound: "构件绑定待处理",
  scale_pending: "标度信息待补充",
  photo_pending: "照片关联或编号异常",
};

// 算不出来的计数一律显示它，而不是 0——"还不知道"和"确定是 0"必须看得出区别。
const UNKNOWN_COUNT = "—";

export function DefectReviewToolbar({
  summary,
  filter,
  issueFilter,
  search,
  viewMode,
  issueGroupCount,
  disabled = false,
  countsPending = false,
  matchCountsPending = false,
  partFilter,
  onPartFilterChange,
  defectTypeFilter,
  onDefectTypeFilterChange,
  rematchScopeLabel,
  rematchCount,
  rematching = false,
  matchError = null,
  onAddDefect,
  addDefectDisabled = false,
  onFilterChange,
  onIssueFilterChange,
  onSearchChange,
  onViewModeChange,
  onRematch,
}: DefectReviewToolbarProps) {
  const { token } = theme.useToken();
  const clearFilters = () => {
    onFilterChange("all");
    onIssueFilterChange(null);
    onSearchChange("");
    onPartFilterChange(null);
    onDefectTypeFilterChange(null);
  };

  const activeFilterValue = issueFilter ? `issue:${issueFilter}` : `status:${filter}`;
  const activeQualityIssueLabel = issueFilter ? QUALITY_ISSUE_LABELS[issueFilter] : undefined;
  const handleCombinedFilterChange = (value: string) => {
    if (value.startsWith("issue:")) {
      onFilterChange("all");
      onIssueFilterChange(value.slice("issue:".length) as DefectReviewIssueFilter);
      return;
    }
    onIssueFilterChange(null);
    onFilterChange(value.slice("status:".length) as DefectReviewFilter);
  };

  const filterOptions = [
    {
      label: "处理状态",
      options: STATUS_FILTERS.map((item) => {
        const unknown = Boolean(item.needsTreeRules && countsPending);
        return {
          value: `status:${item.value}`,
          disabled: unknown,
          label: `${item.value === "all" ? "全部状态" : item.label}（${unknown ? UNKNOWN_COUNT : summary[item.count]}）`,
        };
      }),
    },
    {
      label: "匹配问题",
      options: ISSUE_FILTERS.map((item) => ({
        value: `issue:${item.value}`,
        disabled: matchCountsPending,
        label: `${item.label}（${matchCountsPending ? UNKNOWN_COUNT : summary[item.count]}）`,
      })),
    },
    ...(activeQualityIssueLabel ? [{
      label: "当前校对问题",
      options: [{ value: `issue:${issueFilter}`, label: activeQualityIssueLabel }],
    }] : []),
  ];

  return (
    <Flex vertical gap={10}>
      {matchError ? (
        <Alert
          type="error"
          showIcon
          role="alert"
          title={matchError}
          action={<Button size="small" disabled={rematching} onClick={onRematch}>重试</Button>}
        />
      ) : null}
      {/* 一行排完：左边查看方式，中间搜索与三个筛选，右边两个全局动作。
          全选与批量确认挪到了列表表头——它们操作的是那张列表的勾选状态。 */}
      <Flex align="center" gap={10} wrap>
        <Space.Compact role="group" aria-label="病害查看方式">
          <Button
            aria-pressed={viewMode === "records"}
            type={viewMode === "records" ? "primary" : "default"}
            onClick={() => onViewModeChange("records")}
          >
            逐条查看
          </Button>
          <Button
            aria-pressed={viewMode === "groups"}
            type={viewMode === "groups" ? "primary" : "default"}
            onClick={() => onViewModeChange("groups")}
          >
            问题分组 {issueGroupCount}
          </Button>
        </Space.Compact>

        <Divider orientation="vertical" style={{ marginInline: 2 }} />

        <Input
          aria-label="搜索病害"
          placeholder="搜索构件、位置、病害或照片编号"
          allowClear
          prefix={<SearchOutlined style={{ color: token.colorTextTertiary }} />}
          style={{ flex: "1 1 220px", maxWidth: 320 }}
          value={search}
          onChange={(event) => onSearchChange(event.target.value)}
        />

        {/* 按部件筛选。与状态筛选正交：那边筛"问题类型"，这里筛"部件"，
            两者可以叠加（"只看铰缝里无匹配的"）。选项按走查顺序排、与列表顺序一致，
            只列这份草稿里真的出现过的部件。 */}
        <Select
          aria-label="按部件筛选"
          style={{ width: 150 }}
          value={partFilter ?? ""}
          onChange={(value: string) => onPartFilterChange(value || null)}
          options={[
            { value: "", label: "全部部件" },
            ...summary.parts.map((part) => ({
              value: part.name,
              label: `${part.name === UNBOUND_PART_FILTER ? "未绑定构件" : part.name}（${part.count}）`,
            })),
          ]}
        />

        <Select
          aria-label="按病害类型筛选"
          style={{ width: 170 }}
          value={defectTypeFilter ?? ""}
          onChange={(value: string) => onDefectTypeFilterChange(value || null)}
          options={[
            { value: "", label: `全部病害类型（${summary.all}）` },
            ...summary.defectTypes.map((item) => ({ value: item.name, label: `${item.name}（${item.count}）` })),
          ]}
        />

        <Select
          aria-label="按状态筛选"
          style={{ width: 170 }}
          value={activeFilterValue}
          onChange={handleCombinedFilterChange}
          options={filterOptions}
        />

        {filter !== "all" || issueFilter || search || partFilter || defectTypeFilter ? (
          <Button type="link" style={{ paddingInline: 4 }} onClick={clearFilters}>清除筛选</Button>
        ) : null}

        <Flex align="center" gap={8} style={{ marginInlineStart: "auto" }}>
          {onAddDefect ? (
            <Button icon={<PlusOutlined />} aria-label="新增病害" disabled={addDefectDisabled} onClick={onAddDefect}>新增病害</Button>
          ) : null}
          {/* 重新匹配是低频的兜底动作，收成图标按钮；作用范围写在悬停提示里。 */}
          <Tooltip title={rematching ? "匹配中…" : `重新匹配${rematchScopeLabel}的 ${rematchCount} 条未确认病害`}>
            <Button
              aria-label="重新匹配"
              icon={<SyncOutlined spin={rematching} />}
              disabled={disabled || rematching}
              onClick={onRematch}
            />
          </Tooltip>
        </Flex>
      </Flex>
    </Flex>
  );
}
