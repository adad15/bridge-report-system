import type { AssessmentIssue, AssessmentPreviewResponse } from "../../api/assessmentApi";
import type { AssessmentPhase } from "../assessmentState";

const PART_LABELS: Record<string, string> = {
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
};

const BEAM_CATEGORY_ORDER = [
  "h21.component.beam.upper_bearing",
  "h21.component.beam.upper_general",
  "h21.component.bearing",
  "h21.component.lower.wing_or_ear_wall",
  "h21.component.lower.cone_or_protection_slope",
  "h21.component.lower.pier",
  "h21.component.lower.abutment",
  "h21.component.lower.foundation",
  "h21.component.lower.riverbed",
  "h21.component.lower.regulation_structure",
  "h21.component.deck.pavement",
  "h21.component.deck.expansion_joint",
  "h21.component.deck.sidewalk",
  "h21.component.deck.railing",
  "h21.component.deck.drainage",
  "h21.component.deck.lighting_signs",
];

const CATEGORY_ORDER = new Map(BEAM_CATEGORY_ORDER.map((id, index) => [id, index]));
const BEAM_CATEGORY_LABELS: Record<string, string> = {
  "h21.component.beam.upper_bearing": "上部承重构件",
  "h21.component.beam.upper_general": "上部一般构件",
  "h21.component.bearing": "支座",
  "h21.component.lower.wing_or_ear_wall": "翼墙、耳墙",
  "h21.component.lower.cone_or_protection_slope": "锥坡、护坡",
  "h21.component.lower.pier": "桥墩",
  "h21.component.lower.abutment": "桥台",
  "h21.component.lower.foundation": "墩台基础",
  "h21.component.lower.riverbed": "河床",
  "h21.component.lower.regulation_structure": "调治构造物",
  "h21.component.deck.pavement": "桥面铺装",
  "h21.component.deck.expansion_joint": "伸缩缝装置",
  "h21.component.deck.sidewalk": "人行道",
  "h21.component.deck.railing": "栏杆、护栏",
  "h21.component.deck.drainage": "防排水系统",
  "h21.component.deck.lighting_signs": "照明、标志",
};

function categoryOrder(componentTypeId: string): number {
  return CATEGORY_ORDER.get(componentTypeId) ?? Number.MAX_SAFE_INTEGER;
}

function categoryLabel(componentTypeId: string, packageName?: string): string {
  return BEAM_CATEGORY_LABELS[componentTypeId] ?? packageName ?? componentTypeId;
}

interface AssessmentSectionProps {
  phase: AssessmentPhase;
  response: AssessmentPreviewResponse | null;
  error: string | null;
  onRetry: () => void;
  onSelectIssue: (issue: AssessmentIssue) => void;
}

export function AssessmentSection({ phase, response, error, onRetry, onSelectIssue }: AssessmentSectionProps) {
  const result = response?.result ?? null;
  const categoryRows = result?.structure_parts.flatMap((part) =>
    [...part.categories]
      .sort((left, right) =>
        categoryOrder(left.component_type_id) - categoryOrder(right.component_type_id) ||
        categoryLabel(left.component_type_id, left.component_type_name).localeCompare(
          categoryLabel(right.component_type_id, right.component_type_name),
          "zh-CN",
        ),
      )
      .map((category) => ({ ...category, structure_part: part.structure_part })),
  ) ?? [];
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
          {categoryRows.length ? (
            <div className="assessment-category-results">
              <h3>部件类别评分</h3>
              <div className="table-scroll">
                <table className="data-table assessment-category-table">
                  <thead>
                    <tr><th>结构分部</th><th>序号</th><th>部件类别</th><th>分数</th><th>等级</th><th>分部内权重</th><th>构件数</th></tr>
                  </thead>
                  <tbody>{categoryRows.map((category, index) => (
                    <tr key={category.component_type_id}>
                      <td>{PART_LABELS[category.structure_part] ?? category.structure_part}</td>
                      <td>{index + 1}</td>
                      <td>{categoryLabel(category.component_type_id, category.component_type_name)}</td>
                      <td>{category.score.toFixed(2)}</td>
                      <td>{category.grade} 类</td>
                      <td>{category.effective_weight.toFixed(4)}</td>
                      <td>{category.components.length}</td>
                    </tr>
                  ))}</tbody>
                </table>
              </div>
            </div>
          ) : null}
          {result.triggered_controls.length ? <div className="assessment-controls"><h3>单项控制</h3>{result.triggered_controls.map((control) => <p key={control.control_id}>{control.label}（{control.source_reference}）</p>)}</div> : null}
          <details className="assessment-trace"><summary>计算轨迹（{result.trace.length} 步）</summary><ol>{result.trace.map((trace, index) => <li key={`${trace.rule_id}-${index}`}><code>{trace.rule_id}</code>{trace.source_reference ? ` · ${trace.source_reference}` : ""}</li>)}</ol></details>
          <p className="assessment-explanation">{result.explanation}</p>
        </>
      ) : phase === "idle" ? <p>等待试算。</p> : null}
    </section>
  );
}
