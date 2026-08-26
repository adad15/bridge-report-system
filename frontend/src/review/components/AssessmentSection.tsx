import type { AssessmentCategoryResult, AssessmentIssue, AssessmentReport } from "../../api/assessmentApi";
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
          <div className="assessment-score-summary">
            <div className="assessment-score-primary">
              <span>全桥评分</span>
              <strong>{result.overall_score.toFixed(2)}</strong>
            </div>
            <div className="assessment-score-grade">
              <span>系统等级</span>
              <GradeChip grade={result.final_grade} />
            </div>
            <div className="assessment-score-bridge-type">
              <span>桥型</span>
              <strong>{BRIDGE_TYPE_LABELS[result.bridge_type_id] ?? result.bridge_type_id}</strong>
              {BRIDGE_TYPE_LABELS[result.bridge_type_id] ? <code>{result.bridge_type_id}</code> : null}
            </div>
            {/* 表里只有各分部的小计，全桥总数别处没有。 */}
            <div className="assessment-score-total">
              <span>参评构件数</span>
              <strong>
                {result.structure_parts
                  .reduce((total, part) => total + componentCount(part.categories), 0)
                  .toLocaleString("zh-CN")}
              </strong>
            </div>
          </div>
          {/* 分部与部件类别原本是两张全宽表，第一列把三个分部名重复了十几次；并成一张分组表后
              分部行既是组标题也是那一组的合计行，重复列和纯渲染序号列一起消失。 */}
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
                </tr>
              </thead>
              {result.structure_parts.map((part) => (
                <tbody key={part.structure_part}>
                  <tr className="assessment-part-row">
                    <th scope="rowgroup">{PART_LABELS[part.structure_part] ?? part.structure_part}</th>
                    <td className="numeric-cell">{part.score.toFixed(2)}</td>
                    <td className="assessment-score-bar-cell"><ScoreBar score={part.score} grade={part.grade} /></td>
                    <td><GradeChip grade={part.grade} /></td>
                    <td className="numeric-cell assessment-muted-cell">{part.overall_weight.toFixed(4)}</td>
                    <td className="numeric-cell assessment-muted-cell">{componentCount(part.categories)}</td>
                  </tr>
                  {sortedCategories(part.categories).map((category) => (
                    <tr key={category.component_type_id}>
                      <td className="assessment-category-cell">{categoryLabel(category.component_type_id, category.component_type_name)}</td>
                      <td className="numeric-cell">{category.score.toFixed(2)}</td>
                      <td className="assessment-score-bar-cell"><ScoreBar score={category.score} grade={category.grade} /></td>
                      <td><GradeChip grade={category.grade} /></td>
                      <td className="numeric-cell assessment-muted-cell">{category.effective_weight.toFixed(4)}</td>
                      <td className="numeric-cell">{category.components.length}</td>
                    </tr>
                  ))}
                </tbody>
              ))}
            </table>
          </div>
          {result.triggered_controls.length ? <div className="assessment-controls"><h3>单项控制</h3>{result.triggered_controls.map((control) => <p key={control.control_id}>{control.label}（{control.source_reference}）</p>)}</div> : null}
          <details className="assessment-trace"><summary>计算轨迹（{result.trace.length} 步）</summary><ol>{result.trace.map((trace, index) => <li key={`${trace.rule_id}-${index}`}><code>{trace.rule_id}</code>{trace.source_reference ? ` · ${trace.source_reference}` : ""}</li>)}</ol></details>
          <p className="assessment-explanation">{result.explanation}</p>
        </>
      ) : phase === "idle" ? (
        <p>{confirmed ? "本记录没有已入库的评定结果。" : "等待试算。"}</p>
      ) : null}
    </section>
  );
}
