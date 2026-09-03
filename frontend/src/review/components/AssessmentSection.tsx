import { useState, type CSSProperties } from "react";

import type { AssessmentCategoryResult, AssessmentIssue, AssessmentReport, AssessmentResult } from "../../api/assessmentApi";
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

/* 桥型原本直接显示 h21.bridge_type.beam 这个内部 ID。名称取自规则包 bridge-types.json 的
   官方名（3.2.2 表3.2.2），照本文件 BEAM_CATEGORY_LABELS 的既有做法在前端映射；
   评定结果里没有回传名称，为一个标签走一遍 C++ 后端不值当。未收录的 ID 仍原样显示。 */
const BRIDGE_TYPE_LABELS: Record<string, string> = {
  beam: "梁式桥",
  "h21.bridge_type.beam": "梁式桥",
  "h21.bridge_type.arch_slab_rib_box_double": "板拱、肋拱、箱形拱及双曲拱桥",
  "h21.bridge_type.arch_rigid_frame_truss": "刚架拱及桁架拱桥",
  "h21.bridge_type.arch_steel_concrete_composite": "钢—混凝土组合拱桥",
  "h21.bridge_type.suspension": "悬索桥",
  "h21.bridge_type.cable_stayed": "斜拉桥",
};

function categoryOrder(componentTypeId: string): number {
  return CATEGORY_ORDER.get(componentTypeId) ?? Number.MAX_SAFE_INTEGER;
}

function categoryLabel(componentTypeId: string, packageName?: string): string {
  return BEAM_CATEGORY_LABELS[componentTypeId] ?? packageName ?? componentTypeId;
}

function sortedCategories(categories: AssessmentCategoryResult[]): AssessmentCategoryResult[] {
  return [...categories].sort((left, right) =>
    categoryOrder(left.component_type_id) - categoryOrder(right.component_type_id) ||
    categoryLabel(left.component_type_id, left.component_type_name).localeCompare(
      categoryLabel(right.component_type_id, right.component_type_name),
      "zh-CN",
    ),
  );
}

function componentCount(categories: AssessmentCategoryResult[]): number {
  return categories.reduce((total, category) => total + category.components.length, 0);
}

/* 等级是这个分区唯一的结论性字段，纯文本时 4 类和 1 类视觉权重相同，得逐行读才知道哪里出了问题。
   配色沿用工作台已有的 .review-chip-* 语义色（绿 / 蓝 / 黄），4—5 类往橙红延伸。 */
function GradeChip({ grade }: { grade: number }) {
  return <span className={`assessment-grade assessment-grade-${grade}`}>{grade} 类</span>;
}

/* 得分条是分数的图形复述，作用是吸收表格的富余宽度：其余各列全部定宽，剩下多少都归这一列，
   于是表格能铺满内容区而数字列不会被重新拉散。填色随等级走，51.11 和 100.00 的差距才一眼看得出。 */
function ScoreBar({ score, grade }: { score: number; grade: number }) {
  const ratio = Math.min(100, Math.max(0, score));
  return (
    <span className="assessment-score-bar">
      <span
        className={`assessment-score-bar-fill assessment-score-bar-fill-${grade}`}
        style={{ width: `${ratio}%` }}
      />
    </span>
  );
}

const GRADE_LABELS: Record<number, string> = {
  1: "总体状况良好",
  2: "总体状况良好",
  3: "存在轻度缺损",
  4: "存在明显缺损",
  5: "技术状况危险",
};

const GRADE_COLORS = ["#239447", "#1769e0", "#e89900", "#df5b16", "#c82727"];

function OverallScoreRing({ score }: { score: number }) {
  const ratio = Math.min(100, Math.max(0, score));
  return (
    <div className="assessment-score-ring" style={{ "--assessment-score": `${ratio}%` } as CSSProperties}>
      <strong>{score.toFixed(2)}</strong>
      <span>/ 100</span>
    </div>
  );
}

function BridgeTypeIllustration() {
  return (
    <svg className="assessment-bridge-illustration" viewBox="0 0 132 64" aria-hidden="true">
      <path className="beam-deck" d="M5 15.5h122M8 22h116" />
      <path className="beam-joints" d="M18 15.5v6.5m24-6.5V22m24-6.5V22m24-6.5V22m24-6.5V22" />
      <path className="beam-piers" d="M31 23l-2 28m13-28 2 28M87 23l-2 28m13-28 2 28" />
      <path className="beam-caps" d="M26 25h21m35 0h21M24 52h25m31 0h25" />
      <path className="beam-abutments" d="M10 22v20m112-20v20M6 42h16m88 0h16" />
      <path className="beam-ground" d="M4 57c15-3 27-3 42 0 15 3 27 3 42 0 14-3 26-3 40 0" />
    </svg>
  );
}

interface AssessmentSectionProps {
  /**
   * preview：跟着草稿现算的试算；confirmed：入库时写下、只读取不重算的那一份。
   * 两者的分数结构相同，区别全在措辞和动作上——把"重新试算"摆在已入库的记录上，
   * 按下去只会得到一次注定被拒的请求。
   */
  mode: "preview" | "confirmed";
  phase: AssessmentPhase;
  response: AssessmentReport | null;
  error: string | null;
  /** 这份评定已不是当前有效版本时的说明。 */
  note?: string | null;
  /** 现在这一下点得动吗——试算要编辑锁，没锁就别摆一个按下去必被拒的按钮。 */
  canRetry: boolean;
  onRetry: () => void;
  onSelectIssue: (issue: AssessmentIssue) => void;
}

export function AssessmentSection({ mode, phase, response, error, note, canRetry, onRetry, onSelectIssue }: AssessmentSectionProps) {
  const result = response?.result ?? null;
  const confirmed = mode === "confirmed";
  const [activePart, setActivePart] = useState("all");
  const allCategories = result?.structure_parts.flatMap((part) => part.categories) ?? [];
  const totalComponents = result?.structure_parts
    .reduce((total, part) => total + componentCount(part.categories), 0) ?? 0;
  const gradeCounts = [1, 2, 3, 4, 5].map((grade) =>
    allCategories.filter((category) => category.grade === grade).length
  );
  const attentionCategories = [...allCategories]
    .filter((category) => category.grade >= 4)
    .sort((left, right) => left.score - right.score || categoryOrder(left.component_type_id) - categoryOrder(right.component_type_id));
  const visibleParts = result?.structure_parts.filter((part) =>
    activePart === "all" || part.structure_part === activePart
  ) ?? [];
  const lowestPart = result?.structure_parts.reduce<AssessmentResult["structure_parts"][number] | null>((lowest, part) =>
    !lowest || part.score < lowest.score ? part : lowest
  , null) ?? null;
  const gradeTotal = gradeCounts.reduce((total, count) => total + count, 0);
  let gradeCursor = 0;
  const gradeGradient = gradeCounts.map((count, index) => {
    const start = gradeTotal ? gradeCursor / gradeTotal * 360 : 0;
    gradeCursor += count;
    const end = gradeTotal ? gradeCursor / gradeTotal * 360 : 0;
    return `${GRADE_COLORS[index]} ${start}deg ${end}deg`;
  }).join(", ");
  return (
    <section className="status-panel assessment-section">
      <div className="assessment-heading">
        <div>
          <h2>系统技术状况评定</h2>
          {response ? (
            <p className="assessment-standard-identity">
              {response.standard.standard_code} · {response.standard.standard_name} · 规则包 {response.standard.package_version}
            </p>
          ) : (
            <p>
              {confirmed
                ? "正在读取本记录入库时写下的评定结果。"
                : "系统将使用项目锁定的规范和已确认构件台账计算。"}
            </p>
          )}
          {note ? <p className="assessment-superseded-note">{note}</p> : null}
        </div>
        <div className="assessment-heading-actions">
          {phase === "updating" ? (
            <span className="assessment-updating" role="status">
              {confirmed ? "正在读取…" : "评分更新中…"}
            </span>
          ) : null}
          <button
            type="button"
            disabled={!canRetry}
            title={canRetry ? undefined : "需要先获取编辑权才能试算"}
            onClick={onRetry}
          >
            {confirmed ? "重新加载" : "重新试算"}
          </button>
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
          <div className="assessment-overview-cards" aria-label="系统评定概览">
            <article className="assessment-overview-card score">
              <span>全桥评分</span>
              <div className="assessment-overview-card-content">
                <OverallScoreRing score={result.overall_score} />
              </div>
            </article>
            <article className="assessment-overview-card grade">
              <span>技术状况等级</span>
              <div className="assessment-overview-card-content">
                <strong>{result.final_grade} 类</strong>
                <em>{GRADE_LABELS[result.final_grade] ?? "查看详细评定结果"}</em>
              </div>
            </article>
            <article className="assessment-overview-card bridge-type">
              <span>桥梁类型</span>
              <div className="assessment-overview-card-content">
                <strong>{BRIDGE_TYPE_LABELS[result.bridge_type_id] ?? result.bridge_type_id}</strong>
                <BridgeTypeIllustration />
              </div>
            </article>
            <article className="assessment-overview-card components">
              <span>评定构件</span>
              <strong>{totalComponents.toLocaleString("zh-CN")}<small> 项</small></strong>
              <div className="assessment-completion-label"><span>完成度</span><b>100%</b></div>
              <span className="assessment-completion-track"><i /></span>
            </article>
          </div>
          <div className="assessment-validation-strip">
            <p className="success"><b>✓</b><span>评定计算完成，规则校验通过</span></p>
            {lowestPart ? (
              <p className="warning"><b>!</b><span>{PART_LABELS[lowestPart.structure_part] ?? lowestPart.structure_part}评分 {lowestPart.score.toFixed(2)}{attentionCategories.length ? `，建议重点复核${attentionCategories.slice(0, 2).map((category) => categoryLabel(category.component_type_id, category.component_type_name)).join("与")}` : "。"}</span></p>
            ) : null}
          </div>
          <div className="assessment-dashboard-grid">
            <div className="assessment-breakdown-card">
              <div className="assessment-breakdown-heading">
                <h3>结构分部评分</h3>
                <div className="assessment-part-tabs" role="group" aria-label="按结构分部筛选">
                  <button type="button" aria-pressed={activePart === "all"} onClick={() => setActivePart("all")}>全部</button>
                  {result.structure_parts.map((part) => (
                    <button key={part.structure_part} type="button" aria-pressed={activePart === part.structure_part} onClick={() => setActivePart(part.structure_part)}>{PART_LABELS[part.structure_part] ?? part.structure_part}</button>
                  ))}
                </div>
                <div className="assessment-grade-legend" aria-label="技术状况等级图例">
                  {[1, 2, 3, 4, 5].map((grade, index) => <span key={grade}><i style={{ background: GRADE_COLORS[index] }} />{grade}类</span>)}
                </div>
              </div>
              <div className="table-scroll">
                <table className="data-table assessment-result-table">
                  <thead>
                    <tr>
                      <th>结构分部 / 部件类别</th>
                      <th className="numeric-cell">分数</th>
                      <th className="assessment-score-bar-head" />
                      <th>等级</th>
                      <th className="numeric-cell">权重</th>
                      <th className="numeric-cell">构件数</th>
                      <th>操作</th>
                    </tr>
                  </thead>
                  {visibleParts.map((part) => (
                    <tbody key={part.structure_part}>
                      <tr className="assessment-part-row">
                        <th scope="rowgroup">{PART_LABELS[part.structure_part] ?? part.structure_part}</th>
                        <td className="numeric-cell">{part.score.toFixed(2)}</td>
                        <td className="assessment-score-bar-cell"><ScoreBar score={part.score} grade={part.grade} /></td>
                        <td><GradeChip grade={part.grade} /></td>
                        <td className="numeric-cell assessment-muted-cell">{part.overall_weight.toFixed(4)}</td>
                        <td className="numeric-cell assessment-muted-cell">{componentCount(part.categories)}</td>
                        <td><button type="button" className="assessment-detail-link" onClick={() => setActivePart(part.structure_part)}>查看明细</button></td>
                      </tr>
                      {sortedCategories(part.categories).map((category) => (
                        <tr key={category.component_type_id} className={category.grade >= 4 ? "assessment-attention-row" : ""}>
                          <td className="assessment-category-cell">{categoryLabel(category.component_type_id, category.component_type_name)}</td>
                          <td className="numeric-cell">{category.score.toFixed(2)}</td>
                          <td className="assessment-score-bar-cell"><ScoreBar score={category.score} grade={category.grade} /></td>
                          <td><GradeChip grade={category.grade} /></td>
                          <td className="numeric-cell assessment-muted-cell">{category.effective_weight.toFixed(4)}</td>
                          <td className="numeric-cell">{category.components.length}</td>
                          <td className="assessment-muted-cell">—</td>
                        </tr>
                      ))}
                    </tbody>
                  ))}
                </table>
              </div>
            </div>
            <aside className="assessment-insights">
              <section className="assessment-grade-distribution">
                <h3>等级分布</h3>
                <div className="assessment-grade-distribution-body">
                  <span className="assessment-grade-donut" style={{ background: `conic-gradient(${gradeGradient || "#e5e7eb 0deg 360deg"})` }} aria-label={`共 ${gradeTotal} 个部件类别`}><i /></span>
                  <div>
                    {gradeCounts.map((count, index) => (
                      <p key={index}><span><i style={{ background: GRADE_COLORS[index] }} />{index + 1}类</span><b>{count} 项</b><em>{gradeTotal ? `${(count / gradeTotal * 100).toFixed(1)}%` : "0%"}</em></p>
                    ))}
                  </div>
                </div>
              </section>
              <section className="assessment-attention-card">
                <h3>重点关注 <strong>{attentionCategories.length}</strong> 项</h3>
                {attentionCategories.length ? attentionCategories.map((category, index) => (
                  <article key={category.component_type_id}>
                    <span>{index + 1}</span>
                    <strong>{categoryLabel(category.component_type_id, category.component_type_name)}</strong>
                    <GradeChip grade={category.grade} />
                    <small>分数 {category.score.toFixed(2)}</small>
                    <small>构件数 {category.components.length}</small>
                  </article>
                )) : <p className="assessment-no-attention">暂无 4—5 类重点关注项。</p>}
              </section>
            </aside>
          </div>
          {result.triggered_controls.length ? <div className="assessment-controls"><h3>单项控制</h3>{result.triggered_controls.map((control) => <p key={control.control_id}>{control.label}（{control.source_reference}）</p>)}</div> : null}
          <p className="assessment-explanation">{result.explanation}</p>
        </>
      ) : phase === "idle" ? (
        <p>{confirmed ? "本记录没有已入库的评定结果。" : "等待试算。"}</p>
      ) : null}
    </section>
  );
}
