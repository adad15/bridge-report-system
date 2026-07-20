import type { AssessmentIssue, AssessmentPreviewResponse } from "../../api/assessmentApi";
import type { AssessmentPhase } from "../assessmentState";

const PART_LABELS: Record<string, string> = {
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
};

interface AssessmentSectionProps {
  phase: AssessmentPhase;
  response: AssessmentPreviewResponse | null;
  error: string | null;
  onRetry: () => void;
  onSelectIssue: (issue: AssessmentIssue) => void;
}

export function AssessmentSection({ phase, response, error, onRetry, onSelectIssue }: AssessmentSectionProps) {
  const result = response?.result ?? null;
  return (
    <section className="status-panel assessment-section">
      <div className="assessment-heading">
        <div>
          <h2>系统技术状况评定</h2>
          {response ? (
            <p className="assessment-standard-identity">
              {response.standard.standard_code} · {response.standard.standard_name} · 规则包 {response.standard.package_version}
            </p>
          ) : <p>系统将使用项目锁定的规范和已确认构件台账计算。</p>}
        </div>
        <div className="assessment-heading-actions">
          {phase === "updating" ? <span className="assessment-updating" role="status">评分更新中…</span> : null}
          <button type="button" onClick={onRetry}>重新试算</button>
        </div>
      </div>

      {error ? <p className="error-text">{error}</p> : null}
      {response?.issues.length ? (
        <div className="assessment-issues">
          <h3>待处理项</h3>
          <ul>
            {response.issues.map((issue, index) => (
              <li key={`${issue.code}-${issue.entity_id}-${index}`}>
                <button type="button" className="assessment-issue-link" onClick={() => onSelectIssue(issue)}>{issue.message}</button>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {result ? (
        <>
          <div className="assessment-score-grid">
            <article><span>全桥评分</span><strong>{result.overall_score.toFixed(2)}</strong></article>
            <article><span>系统等级</span><strong>{result.final_grade} 类</strong></article>
            <article><span>桥型</span><strong>{result.bridge_type_id}</strong></article>
          </div>
          <div className="table-scroll">
            <table className="data-table assessment-result-table">
              <thead><tr><th>结构分部</th><th>分数</th><th>等级</th><th>权重</th></tr></thead>
              <tbody>{result.structure_parts.map((part) => (
                <tr key={part.structure_part}><td>{PART_LABELS[part.structure_part] ?? part.structure_part}</td><td>{part.score.toFixed(2)}</td><td>{part.grade} 类</td><td>{part.overall_weight.toFixed(4)}</td></tr>
              ))}</tbody>
            </table>
          </div>
          {result.triggered_controls.length ? <div className="assessment-controls"><h3>单项控制</h3>{result.triggered_controls.map((control) => <p key={control.control_id}>{control.label}（{control.source_reference}）</p>)}</div> : null}
          <details className="assessment-trace"><summary>计算轨迹（{result.trace.length} 步）</summary><ol>{result.trace.map((trace, index) => <li key={`${trace.rule_id}-${index}`}><code>{trace.rule_id}</code>{trace.source_reference ? ` · ${trace.source_reference}` : ""}</li>)}</ol></details>
          <p className="assessment-explanation">{result.explanation}</p>
        </>
      ) : phase === "idle" ? <p>等待试算。</p> : null}
    </section>
  );
}
