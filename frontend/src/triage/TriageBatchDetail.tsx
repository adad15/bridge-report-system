import type { TriageApplyIssue, TriageBatchDetail } from "../api/threadTriageApi";

/**
 * 批次明细：以构件为行、年份为列横向排开。
 *
 * 这是"写病害发展"最需要的视角——一眼看出同一处病害三年怎么变的。明细一次取全不分页：
 * 分页会让"跨页剔除"与"提交清单"对不上，用户在第 3 页去掉两组，提交时前端得凑齐全部
 * 组才能表达"这批除了这两组"。
 */
interface TriageBatchDetailProps {
  detail: TriageBatchDetail;
  excludedGroupIds: ReadonlySet<string>;
  issues: TriageApplyIssue[];
  onToggleGroup: (groupId: string) => void;
}

export function TriageBatchDetailTable({
  detail, excludedGroupIds, issues, onToggleGroup,
}: TriageBatchDetailProps) {
  const issuesByGroup = new Map<string, TriageApplyIssue[]>();
  for (const issue of issues) {
    if (!issue.group_id) continue;
    const bucket = issuesByGroup.get(issue.group_id) ?? [];
    bucket.push(issue);
    issuesByGroup.set(issue.group_id, bucket);
  }

  return (
    <div className="triage-detail" aria-label="批次逐组明细">
      <table className="data-table triage-detail-table">
        <thead>
          <tr>
            <th scope="col">纳入</th>
            <th scope="col">构件</th>
            {detail.year_set.map((year) => <th key={year} scope="col">{year}</th>)}
            {detail.action === "bind" ? <th scope="col">目标线索</th> : null}
          </tr>
        </thead>
        <tbody>
          {detail.groups.map((group) => {
            const excluded = excludedGroupIds.has(group.group_id);
            const groupIssues = issuesByGroup.get(group.group_id) ?? [];
            return (
              <tr
                key={group.group_id}
                className={`${excluded ? "triage-row-excluded" : ""} ${
                  groupIssues.length > 0 ? "triage-row-problem" : ""}`}
              >
                <td>
                  <input
                    type="checkbox"
                    checked={!excluded}
                    aria-label={`纳入 ${group.business_component_code ?? group.group_id}`}
                    onChange={() => onToggleGroup(group.group_id)}
                  />
                </td>
                <td>{group.business_component_code ?? group.group_id.slice(0, 8)}</td>
                {detail.year_set.map((year) => {
                  const observation = group.observations.find(
                    (item) => item.inspection_year === year);
                  return (
                    <td key={year}>
                      {observation ? (
                        <span className="triage-cell">
                          {observation.scale ? `标度 ${observation.scale}` : "—"}
                          {observation.measurements && observation.measurements.length > 0
                            ? ` · ${observation.measurements.join("；")}`
                            : ""}
                          {observation.photos && observation.photos.length > 0
                            ? ` · ${observation.photos.length} 张`
                            : ""}
                        </span>
                      ) : <span className="triage-cell-empty">—</span>}
                    </td>
                  );
                })}
                {detail.action === "bind" ? (
                  // 逐组显示各自的 BHXS：批次层面没有单一目标。
                  <td>{group.target_thread?.system_number ?? "—"}</td>
                ) : null}
              </tr>
            );
          })}
        </tbody>
      </table>

      {issues.length > 0 ? (
        <ul className="triage-detail-issues" role="alert">
          {issues.map((issue, index) => (
            <li key={`${issue.reason_code}-${index}`}>
              {issue.group_id ? `${issue.group_id.slice(0, 8)}：` : ""}{issue.message}
            </li>
          ))}
        </ul>
      ) : null}
    </div>
  );
}
