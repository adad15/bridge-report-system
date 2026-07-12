import type { ReviewCounts } from "../grouping";

// 六个分组（模块 05 §7.2）。"需要处理" 永远排第一位；"来源证据"/"原始 JSON" 不带独立计数。
export type GroupKey = "needs_attention" | "defect_photos" | "ratings" | "raw_json";

interface GroupDef {
  key: GroupKey;
  label: string;
  count: (counts: ReviewCounts) => number | null;
}

const GROUPS: GroupDef[] = [
  { key: "needs_attention", label: "需要处理", count: (counts) => counts.needs_attention_count },
  { key: "defect_photos", label: "病害与照片", count: (counts) => counts.defect_count },
  { key: "ratings", label: "技术状况评定", count: (counts) => counts.rating_item_count },
  { key: "raw_json", label: "原始 JSON", count: () => null },
];

interface ReviewSidebarProps {
  counts: ReviewCounts;
  active: GroupKey;
  onSelect: (key: GroupKey) => void;
}

export function ReviewSidebar({ counts, active, onSelect }: ReviewSidebarProps) {
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
