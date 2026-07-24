import type { ReviewCounts } from "../grouping";

// "需要处理" 永远排第一位；系统评定展示后端计算结果，不展示 Word 导入评分。
export type GroupKey =
  | "needs_attention"
  | "component_binding"
  | "defect_photos"
  | "ratings"
  | "raw_json";

interface GroupDef {
  key: GroupKey;
  label: string;
  count: (counts: ReviewCounts) => number | null;
}

const GROUPS: GroupDef[] = [
  { key: "needs_attention", label: "需要处理", count: (counts) => counts.needs_attention_count },
  // 绑定进度由绑定分区自己拉取，不在 ReviewCounts 里，故与"原始 JSON"一样不显示计数。
  { key: "component_binding", label: "构件绑定", count: () => null },
  { key: "defect_photos", label: "病害与照片", count: (counts) => counts.defect_count },
  { key: "ratings", label: "系统技术状况评定", count: (counts) => counts.rating_item_count },
  { key: "raw_json", label: "原始 JSON", count: () => null },
];

interface ReviewSidebarProps {
  counts: ReviewCounts;
  active: GroupKey;
  onSelect: (key: GroupKey) => void;
}

export function ReviewSidebar({
  counts,
  active,
  onSelect,
}: ReviewSidebarProps) {
  return (
    <nav className="review-sidebar">
      {GROUPS.map((group) => {
        const count = group.count(counts);
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
