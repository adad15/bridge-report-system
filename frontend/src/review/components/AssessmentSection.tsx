import { Alert, Button, Card, Col, Empty, Flex, Progress, Row, Statistic, Table, Tag, Typography, theme, type TableColumnsType } from "antd";
import { useState } from "react";

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

const GRADE_LABELS: Record<number, string> = {
  1: "总体状况良好",
  2: "总体状况良好",
  3: "存在轻度缺损",
  4: "存在明显缺损",
  5: "技术状况危险",
};

/* 等级是这个分区唯一的结论性字段：纯文本时 4 类和 1 类视觉权重相同，得逐行读才知道哪里出了问题。
   绿 → 蓝 → 黄 → 橙 → 红，按等级从好到坏。 */
const GRADE_COLORS = ["#239447", "#1769e0", "#e89900", "#df5b16", "#c82727"];
const GRADE_TAG_COLORS = ["success", "processing", "warning", "volcano", "error"];

function gradeColor(grade: number): string {
  return GRADE_COLORS[grade - 1] ?? GRADE_COLORS[GRADE_COLORS.length - 1];
}

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

function GradeTag({ grade }: { grade: number }) {
  return <Tag color={GRADE_TAG_COLORS[grade - 1]} variant="filled">{grade} 类</Tag>;
}

function BridgeTypeIllustration() {
  const stroke = "#8aa2c8";
  return (
    <svg viewBox="0 0 132 64" width={132} height={64} aria-hidden="true" fill="none" stroke={stroke} strokeWidth={2} strokeLinecap="round">
      <path d="M5 15.5h122M8 22h116" />
      <path d="M18 15.5v6.5m24-6.5V22m24-6.5V22m24-6.5V22m24-6.5V22" strokeWidth={1.2} />
      <path d="M31 23l-2 28m13-28 2 28M87 23l-2 28m13-28 2 28" />
      <path d="M26 25h21m35 0h21M24 52h25m31 0h25" />
      <path d="M10 22v20m112-20v20M6 42h16m88 0h16" />
      <path d="M4 57c15-3 27-3 42 0 15 3 27 3 42 0 14-3 26-3 40 0" strokeWidth={1.2} />
    </svg>
  );
}

/** 表格里一行：结构分部行与它下面的部件类别行共用一套列。 */
interface BreakdownRow {
  key: string;
  label: string;
  score: number;
  grade: number;
  weight: number;
  components: number;
  isPart: boolean;
  structurePart: string;
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
  const { token } = theme.useToken();
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

  // 分部行与类别行摊平成一张表：层级靠首列缩进表达，不再靠多个 tbody。
  const breakdownRows: BreakdownRow[] = visibleParts.flatMap((part) => [
    {
      key: part.structure_part,
      label: PART_LABELS[part.structure_part] ?? part.structure_part,
      score: part.score,
      grade: part.grade,
      weight: part.overall_weight,
      components: componentCount(part.categories),
      isPart: true,
      structurePart: part.structure_part,
    },
    ...sortedCategories(part.categories).map((category) => ({
      key: `${part.structure_part}:${category.component_type_id}`,
      label: categoryLabel(category.component_type_id, category.component_type_name),
      score: category.score,
      grade: category.grade,
      weight: category.effective_weight,
      components: category.components.length,
      isPart: false,
      structurePart: part.structure_part,
    })),
  ]);

  const columns: TableColumnsType<BreakdownRow> = [
    {
      title: "结构分部 / 部件类别",
      key: "label",
      width: 220,
      render: (_value, row) => (
        <Typography.Text strong={row.isPart} style={{ paddingInlineStart: row.isPart ? 0 : 16 }}>
          {row.label}
        </Typography.Text>
      ),
    },
    {
      title: "分数",
      key: "score",
      width: 88,
      align: "right",
      render: (_value, row) => <Typography.Text strong={row.isPart}>{row.score.toFixed(2)}</Typography.Text>,
    },
    {
      // 得分条吸收表格的富余宽度：其余各列全部定宽，剩下多少都归这一列。
      title: "",
      key: "bar",
      render: (_value, row) => (
        <Progress
          percent={Math.min(100, Math.max(0, row.score))}
          showInfo={false}
          strokeColor={gradeColor(row.grade)}
          railColor={token.colorFillTertiary}
          size={{ height: row.isPart ? 10 : 8 }}
          style={{ margin: 0 }}
        />
      ),
    },
    { title: "等级", key: "grade", width: 84, render: (_value, row) => <GradeTag grade={row.grade} /> },
    {
      title: "权重",
      key: "weight",
      width: 90,
      align: "right",
      render: (_value, row) => <Typography.Text type="secondary">{row.weight.toFixed(4)}</Typography.Text>,
    },
    {
      title: "构件数",
      key: "components",
      width: 90,
      align: "right",
      render: (_value, row) => row.components,
    },
    {
      title: "操作",
      key: "actions",
      width: 100,
      render: (_value, row) => (row.isPart ? (
        <Button type="link" size="small" onClick={() => setActivePart(row.structurePart)}>查看明细</Button>
      ) : <Typography.Text type="secondary">—</Typography.Text>),
    },
  ];

  return (
    <Card
      title="系统技术状况评定"
      extra={
        <Flex align="center" gap={10}>
          {phase === "updating" ? (
            <Typography.Text type="secondary" role="status">{confirmed ? "正在读取…" : "评分更新中…"}</Typography.Text>
          ) : null}
          <Button
            disabled={!canRetry}
            title={canRetry ? undefined : "需要先获取编辑权才能试算"}
            onClick={onRetry}
          >
            {confirmed ? "重新加载" : "重新试算"}
          </Button>
        </Flex>
      }
    >
      <Flex vertical gap={16}>
        {response ? (
          <Typography.Text type="secondary">
            {response.standard.standard_code} · {response.standard.standard_name} · 规则包 {response.standard.package_version}
          </Typography.Text>
        ) : (
          <Typography.Text type="secondary">
            {confirmed
              ? "正在读取本记录入库时写下的评定结果。"
              : "系统将使用项目锁定的规范和已确认构件台账计算。"}
          </Typography.Text>
        )}
        {note ? <Alert type="warning" showIcon title={note} /> : null}
        {error ? <Alert type="error" showIcon title={error} /> : null}

        {response?.issues.length ? (
          <Card size="small" title="待处理项">
            <Flex vertical gap={4} align="start">
              {response.issues.map((issue, index) => (
                <Button
                  key={`${issue.code}-${issue.entity_id}-${index}`}
                  type="link"
                  style={{ padding: 0, height: "auto", textAlign: "left" }}
                  onClick={() => onSelectIssue(issue)}
                >
                  {issue.message}
                </Button>
              ))}
            </Flex>
          </Card>
        ) : null}

        {result ? (
          <>
            <Row gutter={[12, 12]} aria-label="系统评定概览">
              <Col xs={12} xl={6}>
                <Card size="small" style={{ height: "100%" }}>
                  <Flex vertical align="center" gap={6}>
                    <Typography.Text type="secondary">全桥评分</Typography.Text>
                    <Progress
                      type="dashboard"
                      size={110}
                      percent={Math.min(100, Math.max(0, result.overall_score))}
                      strokeColor={gradeColor(result.final_grade)}
                      format={() => (
                        <Flex vertical align="center">
                          <Typography.Text strong style={{ fontSize: 22 }}>{result.overall_score.toFixed(2)}</Typography.Text>
                          <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>/ 100</Typography.Text>
                        </Flex>
                      )}
                    />
                  </Flex>
                </Card>
              </Col>
              <Col xs={12} xl={6}>
                <Card size="small" style={{ height: "100%" }}>
                  <Flex vertical gap={8}>
                    <Typography.Text type="secondary">技术状况等级</Typography.Text>
                    <Flex align="baseline" gap={8}>
                      <Typography.Title level={2} style={{ margin: 0, color: gradeColor(result.final_grade) }}>
                        {result.final_grade} 类
                      </Typography.Title>
                    </Flex>
                    <Typography.Text type="secondary">
                      {GRADE_LABELS[result.final_grade] ?? "查看详细评定结果"}
                    </Typography.Text>
                  </Flex>
                </Card>
              </Col>
              <Col xs={12} xl={6}>
                <Card size="small" style={{ height: "100%" }}>
                  <Flex vertical gap={8}>
                    <Typography.Text type="secondary">桥梁类型</Typography.Text>
                    <Typography.Text strong>{BRIDGE_TYPE_LABELS[result.bridge_type_id] ?? result.bridge_type_id}</Typography.Text>
                    <BridgeTypeIllustration />
                  </Flex>
                </Card>
              </Col>
              <Col xs={12} xl={6}>
                <Card size="small" style={{ height: "100%" }}>
                  <Flex vertical gap={8}>
                    <Statistic title="评定构件" value={totalComponents} suffix="项" />
                    <Flex align="center" justify="space-between">
                      <Typography.Text type="secondary">完成度</Typography.Text>
                      <Typography.Text strong>100%</Typography.Text>
                    </Flex>
                    <Progress percent={100} showInfo={false} strokeColor={token.colorSuccess} size={{ height: 8 }} style={{ margin: 0 }} />
                  </Flex>
                </Card>
              </Col>
            </Row>

            <Flex vertical gap={8}>
              <Alert type="success" showIcon role="note" title="评定计算完成，规则校验通过" />
              {lowestPart ? (
                <Alert
                  type="warning"
                  showIcon
                  role="note"
                  title={`${PART_LABELS[lowestPart.structure_part] ?? lowestPart.structure_part}评分 ${lowestPart.score.toFixed(2)}${
                    attentionCategories.length
                      ? `，建议重点复核${attentionCategories.slice(0, 2).map((category) => categoryLabel(category.component_type_id, category.component_type_name)).join("与")}`
                      : "。"}`}
                />
              ) : null}
            </Flex>

            <Row gutter={[14, 14]} align="stretch">
              <Col xs={24} xl={16}>
                <Card
                  size="small"
                  style={{ height: "100%" }}
                  title="结构分部评分"
                  extra={
                    <Flex align="center" gap={6} role="group" aria-label="按结构分部筛选">
                      <Button
                        size="small"
                        aria-pressed={activePart === "all"}
                        type={activePart === "all" ? "primary" : "default"}
                        onClick={() => setActivePart("all")}
                      >
                        全部
                      </Button>
                      {result.structure_parts.map((part) => (
                        <Button
                          key={part.structure_part}
                          size="small"
                          aria-pressed={activePart === part.structure_part}
                          type={activePart === part.structure_part ? "primary" : "default"}
                          onClick={() => setActivePart(part.structure_part)}
                        >
                          {PART_LABELS[part.structure_part] ?? part.structure_part}
                        </Button>
                      ))}
                    </Flex>
                  }
                >
                  <Table<BreakdownRow>
                    rowKey="key"
                    size="small"
                    columns={columns}
                    dataSource={breakdownRows}
                    pagination={false}
                    scroll={{ x: 760 }}
                    onRow={(row) => ({
                      style: row.grade >= 4 ? { background: token.colorErrorBg } : undefined,
                    })}
                  />
                </Card>
              </Col>

              <Col xs={24} xl={8}>
                <Flex vertical gap={14} style={{ height: "100%" }}>
                  <Card size="small" title="等级分布">
                    <Flex vertical gap={8}>
                      {gradeCounts.map((count, index) => (
                        <Flex key={index} align="center" gap={10}>
                          <Typography.Text style={{ width: 40, flex: "none" }}>{index + 1}类</Typography.Text>
                          <Progress
                            percent={gradeTotal ? (count / gradeTotal) * 100 : 0}
                            showInfo={false}
                            strokeColor={GRADE_COLORS[index]}
                            railColor={token.colorFillTertiary}
                            size={{ height: 8 }}
                            style={{ flex: 1, margin: 0 }}
                          />
                          <Typography.Text style={{ width: 48, flex: "none", textAlign: "right" }}>{count} 项</Typography.Text>
                          <Typography.Text type="secondary" style={{ width: 52, flex: "none", textAlign: "right" }}>
                            {gradeTotal ? `${(count / gradeTotal * 100).toFixed(1)}%` : "0%"}
                          </Typography.Text>
                        </Flex>
                      ))}
                    </Flex>
                  </Card>

                  <Card
                    size="small"
                    style={{ flex: 1 }}
                    title={<>重点关注 <Typography.Text strong>{attentionCategories.length}</Typography.Text> 项</>}
                  >
                    {attentionCategories.length ? (
                      <Flex vertical gap={10}>
                        {attentionCategories.map((category, index) => (
                          <Flex key={category.component_type_id} align="center" gap={8} wrap>
                            <Typography.Text type="secondary">{index + 1}</Typography.Text>
                            <Typography.Text strong>
                              {categoryLabel(category.component_type_id, category.component_type_name)}
                            </Typography.Text>
                            <GradeTag grade={category.grade} />
                            <Typography.Text type="secondary">分数 {category.score.toFixed(2)}</Typography.Text>
                            <Typography.Text type="secondary">构件数 {category.components.length}</Typography.Text>
                          </Flex>
                        ))}
                      </Flex>
                    ) : (
                      <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="暂无 4—5 类重点关注项。" />
                    )}
                  </Card>
                </Flex>
              </Col>
            </Row>

            {result.triggered_controls.length ? (
              <Card size="small" title="单项控制">
                <Flex vertical gap={4}>
                  {result.triggered_controls.map((control) => (
                    <Typography.Text key={control.control_id}>
                      {control.label}（{control.source_reference}）
                    </Typography.Text>
                  ))}
                </Flex>
              </Card>
            ) : null}
            <Typography.Text type="secondary">{result.explanation}</Typography.Text>
          </>
        ) : phase === "idle" ? (
          <Typography.Text type="secondary">{confirmed ? "本记录没有已入库的评定结果。" : "等待试算。"}</Typography.Text>
        ) : null}
      </Flex>
    </Card>
  );
}
