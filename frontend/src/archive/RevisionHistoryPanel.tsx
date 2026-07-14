import type { RevisionGroup } from "../api/componentArchiveApi";
import { ObservationYearRow } from "./ObservationYearRow";

// 历史修订入口（模块 06 §7.4）：旧修订版按 年份+版本号 分组独立只读展示，
// 明确标注被当前版本替代的关系；不提供任何绑定操作，也不计入主档案统计。
export function RevisionHistoryPanel({ revisions }: { revisions: RevisionGroup[] }) {
  if (revisions.length === 0) {
    return <p className="archive-empty-hint">该构件没有历史修订版观测。</p>;
  }

  return (
    <div className="archive-revision-panel">
      {revisions.map((group) => (
        <section key={`${group.inspection_year}-v${group.version_number}`} className="archive-revision-group">
          <header>
            <strong>
              {group.inspection_year} 年 v{group.version_number}
            </strong>
            <span className="review-status-badge review-status-neutral">{group.inspection_status}</span>
            {group.superseded_by_version !== null ? (
              <span className="archive-legacy-hint">已被修订，当前有效版本为 v{group.superseded_by_version}</span>
            ) : null}
          </header>
          {group.observations.map((observation) => (
            <ObservationYearRow key={observation.id} observation={observation} />
          ))}
        </section>
      ))}
    </div>
  );
}
