import type { ReviewCounts } from "../grouping";

// 构件绑定排第一位：构件不绑好，评定树匹配跑不起来，它是流程上的第一步。
// 系统评定展示后端计算结果，不展示 Word 导入评分。
export type GroupKey =
  | "component_binding"
  | "defect_photos"
  | "ratings"
  | "raw_json";

// 绑定待处理数不在后端的 ReviewStatistics 里（它来自导入绑定概览），故单列一项；
// 台账未确认时无法绑定，此时为 null，显示 "-" 而不是 0——0 会被读成"都处理完了"。
interface SidebarCounts extends ReviewCounts {
  binding_pending_count: number | null;
}

interface GroupDef {
  key: GroupKey;
  label: string;
  count: (counts: SidebarCounts) => number | null;
}

const GROUPS: GroupDef[] = [
  { key: "component_binding", label: "构件绑定", count: (counts) => counts.binding_pending_count },
  { key: "defect_photos", label: "病害与照片", count: (counts) => counts.defect_count },
  { key: "ratings", label: "系统技术状况评定", count: (counts) => counts.rating_item_count },
  { key: "raw_json", label: "原始 JSON", count: () => null },
];

interface ReviewSidebarProps {
  counts: ReviewCounts;
  bindingPendingCount: number | null;
  active: GroupKey;
  onSelect: (key: GroupKey) => void;
}

export function ReviewSidebar({
  counts,
  bindingPendingCount,
  active,
  onSelect,
}: ReviewSidebarProps) {
  const sidebarCounts: SidebarCounts = { ...counts, binding_pending_count: bindingPendingCount };
  return (
    <nav className="review-sidebar">
      {GROUPS.map((group) => {
        const count = group.count(sidebarCounts);
        return (
          <button
            key={group.key}
            type="button"
            className={group.key === active ? "review-sidebar-item active" : "review-sidebar-item"}
            onClick={() => onSelect(group.key)}
          >
            <span>{group.label}</span>
            <span className="review-sidebar-count">{count === null ? "-" : count}</span>
          </button>
        );
      })}
    </nav>
  );
}
