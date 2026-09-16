import {
  CheckCircleFilled,
  ClockCircleFilled,
  CloseCircleFilled,
  DownloadOutlined,
  ExclamationCircleFilled,
  FileWordOutlined,
  ReloadOutlined,
  SaveOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Avatar,
  Button,
  Card,
  Col,
  Empty,
  Flex,
  Form,
  Input,
  Progress,
  Row,
  Select,
  Space,
  Statistic,
  Tag,
  Typography,
  theme,
} from "antd";
import { useCallback, useEffect, useRef, useState } from "react";
import { Link, useParams } from "react-router-dom";

import {
  createReportJob,
  fetchComparisonCandidates,
  fetchReportEquipment,
  fetchCurrentReportJob,
  fetchReportJob,
  fetchReportPersonnel,
  fetchReportPreflight,
  fetchReportSettings,
  fetchReportTemplates,
  personnelRoleLabel,
  downloadReportJob,
  saveReportSettings,
  structurePartLabel,
  REPORT_JOB_STAGE_LABELS,
  type ComparisonCandidate,
  type InspectionReportSettings,
  type ReportEquipment,
  type ReportJob,
  type ReportPersonnel,
  type ReportPreflight,
  type ReportTemplate,
} from "../api/reportApi";
import { reportErrorMessage, reportJobFailureHint } from "../report/reportErrors";
import { PageHeader } from "../design-system";
import { PERSONNEL_ROLE_OPTIONS } from "../report/reportNumberFormats";

/** 任务还在跑时的轮询间隔。装配一份真实规模的报告要一分多钟，不必问得更密。 */
const POLL_INTERVAL_MS = 3000;

/** 各阶段在进度条上占的位置。只是给用户一个"走到哪儿了"的感觉，不是精确耗时。 */
const STAGE_PERCENT: Record<string, number> = {
  queued: 5,
  validating_data: 15,
  assembling_docx: 45,
  updating_fields: 80,
  validating_docx: 92,
  ready: 100,
  failed: 100,
  expired: 100,
};

/**
 * 年度检查 · 生成报告（设计 §21.4）。
 *
 * 四块，顺序就是用户的心智顺序：能不能生成 → 用什么配置 → 会生成出什么 → 生成与下载。
 *
 * 一条贯穿全页的纪律：**阻断项必须在点按钮之前就摆出来。** 后端在创建任务时会再查
 * 一遍（§16），但让用户点了才知道不行是最糟的交互。
 */
export function ReportGenerationPage() {
  const { bridgeId, inspectionYearId } = useParams<{
    bridgeId: string;
    inspectionYearId: string;
  }>();
  const yearId = inspectionYearId ?? "";

  const [preflight, setPreflight] = useState<ReportPreflight | null>(null);
  const [settings, setSettings] = useState<InspectionReportSettings | null>(null);
  const [templates, setTemplates] = useState<ReportTemplate[]>([]);
  const [candidates, setCandidates] = useState<ComparisonCandidate[]>([]);
  const [personnelPool, setPersonnelPool] = useState<ReportPersonnel[]>([]);
  const [equipmentPool, setEquipmentPool] = useState<ReportEquipment[]>([]);
  const [job, setJob] = useState<ReportJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const load = useCallback(async () => {
    if (!yearId) return;
    setError(null);
    try {
      const [check, saved, templateList, candidateList, people, gear, currentJob] =
        await Promise.all([
          fetchReportPreflight(yearId),
          fetchReportSettings(yearId),
          fetchReportTemplates(),
          fetchComparisonCandidates(yearId),
          fetchReportPersonnel(true),
          fetchReportEquipment(true),
          fetchCurrentReportJob(yearId),
        ]);
      setPreflight(check);
      setSettings(saved);
      setTemplates(templateList);
      setCandidates(candidateList);
      setPersonnelPool(people);
      setEquipmentPool(gear);
      setJob(currentJob);
    } catch (caught) {
      setError(reportErrorMessage(caught));
    }
  }, [yearId]);

  useEffect(() => {
    void load();
  }, [load]);

  // 有任务在跑就轮询，跑完立刻停。effect 卸载时清掉定时器，切换年度不会留下幽灵请求。
  const runningJobId = job?.is_running ? job.id : null;
  const reloadRef = useRef(load);
  reloadRef.current = load;

  useEffect(() => {
    if (!runningJobId) return undefined;
    let cancelled = false;
    const timer = window.setInterval(async () => {
      try {
        const latest = await fetchReportJob(runningJobId);
        if (cancelled) return;
        setJob(latest);
        // 跑完了就整页刷一次：配置和内容摘要可能也变了。
        if (!latest.is_running) void reloadRef.current();
      } catch {
        // 轮询失败不打断页面：下一拍再试，真出问题时任务本身也会转 failed。
      }
    }, POLL_INTERVAL_MS);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [runningJobId]);

  async function generate() {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const created = await createReportJob(yearId);
      setJob(created);
      setNotice(
        created.status === "queued"
          ? "已创建生成任务，正在后台执行。"
          : "这个年度已有正在执行的任务，继续显示它的进度。",
      );
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  if (!yearId) {
    return <Alert type="error" showIcon title="缺少年度检查标识。" />;
  }

  const blocking = preflight?.findings.filter((item) => item.severity === "blocking") ?? [];
  const warnings = preflight?.findings.filter((item) => item.severity === "warning") ?? [];
  const settingsNotes = settings?.blocking_notes ?? [];
  const canGenerate = (preflight?.can_generate ?? false) && settingsNotes.length === 0;

  return (
    <Flex vertical gap={16}>
      <PageHeader
        title="生成报告"
        description={`${preflight ? `${preflight.inspection_year} 年度` : "年度"}定期检测报告。生成结果是临时下载文件，过期后重新生成即可。`}
        extra={
          <>
            <Button icon={<ReloadOutlined />} onClick={() => void load()}>
              刷新
            </Button>
            {bridgeId ? (
              <Link to={`/bridges/${encodeURIComponent(bridgeId)}/inspections/${encodeURIComponent(yearId)}`}>
                <Button>返回年度</Button>
              </Link>
            ) : null}
          </>
        }
      />

      {error ? <Alert type="error" showIcon title={error} closable onClose={() => setError(null)} /> : null}
      {notice ? <Alert type="success" showIcon title={notice} closable onClose={() => setNotice(null)} /> : null}

      <PreflightCard preflight={preflight} blocking={blocking} warnings={warnings} notes={settingsNotes} />

      <SettingsCard
        yearId={yearId}
        settings={settings}
        templates={templates}
        candidates={candidates}
        personnelPool={personnelPool}
        equipmentPool={equipmentPool}
        onSaved={(saved) => {
          setSettings(saved);
          setNotice("报告配置已保存。");
          void load();
        }}
        onError={setError}
      />

      <SummaryCard preflight={preflight} settings={settings} />

      <CurrentReportCard
        job={job}
        busy={busy}
        canGenerate={canGenerate}
        onGenerate={() => void generate()}
      />
    </Flex>
  );
}

/** 一条检查结果：代码在前，说明在后。 */
function FindingLine({ code, message }: { code: string; message: string }) {
  return (
    <Flex gap={8} align="baseline" wrap>
      <Typography.Text code>{code}</Typography.Text>
      <Typography.Text>{message}</Typography.Text>
    </Flex>
  );
}

// ---------------------------------------------------------------------------
// 一、生成条件
// ---------------------------------------------------------------------------

function PreflightCard({
  preflight,
  blocking,
  warnings,
  notes,
}: {
  preflight: ReportPreflight | null;
  blocking: ReportPreflight["findings"];
  warnings: ReportPreflight["findings"];
  notes: string[];
}) {
  const ready = (preflight?.can_generate ?? false) && notes.length === 0;
  return (
    <Card
      title={
        <Space>
          <span>生成条件</span>
          {preflight === null ? null : ready ? (
            <Tag icon={<CheckCircleFilled />} color="green">全部满足</Tag>
          ) : (
            <Tag icon={<CloseCircleFilled />} color="red">
              {blocking.length + notes.length} 项待处理
            </Tag>
          )}
        </Space>
      }
    >
      {preflight === null ? (
        <Typography.Text type="secondary">正在检查…</Typography.Text>
      ) : ready && warnings.length === 0 ? (
        <Typography.Text type="secondary">数据、评定、模板、照片和字段更新环境都已就绪，可以生成。</Typography.Text>
      ) : (
        <Flex vertical gap={16}>
          {blocking.length > 0 || notes.length > 0 ? (
            <Flex vertical gap={6}>
              <Typography.Text strong>
                <Typography.Text type="danger"><CloseCircleFilled /></Typography.Text> 阻断项（处理完才能生成）
              </Typography.Text>
              {blocking.map((item) => <FindingLine key={item.code} code={item.code} message={item.message} />)}
              {notes.map((note) => <FindingLine key={note} code="report_settings" message={note} />)}
            </Flex>
          ) : null}
          {warnings.length > 0 ? (
            <Flex vertical gap={6}>
              <Typography.Text strong>
                <Typography.Text type="warning"><ExclamationCircleFilled /></Typography.Text> 提示（不阻断生成）
              </Typography.Text>
              {warnings.map((item) => <FindingLine key={item.code} code={item.code} message={item.message} />)}
            </Flex>
          ) : null}
        </Flex>
      )}
    </Card>
  );
}

// ---------------------------------------------------------------------------
// 二、报告配置
// ---------------------------------------------------------------------------

interface PersonnelDraft {
  personnel_id: string;
  role_code: string;
}

function SettingsCard({
  yearId,
  settings,
  templates,
  candidates,
  personnelPool,
  equipmentPool,
  onSaved,
  onError,
}: {
  yearId: string;
  settings: InspectionReportSettings | null;
  templates: ReportTemplate[];
  candidates: ComparisonCandidate[];
  personnelPool: ReportPersonnel[];
  equipmentPool: ReportEquipment[];
  onSaved: (saved: InspectionReportSettings) => void;
  onError: (message: string) => void;
}) {
  const [templateId, setTemplateId] = useState<string | null>(null);
  const [comparisonId, setComparisonId] = useState<string | null>(null);
  const [people, setPeople] = useState<PersonnelDraft[]>([]);
  const [gear, setGear] = useState<{ equipment_id: string; purpose: string }[]>([]);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (settings === null) return;
    setTemplateId(settings.template_id);
    setComparisonId(settings.comparison_inspection_id);
    setPeople(
      settings.personnel.map((item) => ({
        personnel_id: item.personnel_id,
        role_code: item.role_code,
      })),
    );
    setGear(
      settings.equipment.map((item) => ({
        equipment_id: item.equipment_id,
        purpose: item.purpose ?? "",
      })),
    );
  }, [settings]);

  const selectedTemplate = templates.find((item) => item.id === templateId) ?? null;
  const requiredRoles = selectedTemplate?.contract_config?.required_personnel_roles ?? [];
  const coveredRoles = new Set(people.map((item) => item.role_code));
  const missingRoles = requiredRoles.filter((role) => !coveredRoles.has(role));

  async function save() {
    setBusy(true);
    try {
      const saved = await saveReportSettings(yearId, {
        template_id: templateId,
        comparison_inspection_id: comparisonId,
        personnel: people.map((item, index) => ({ ...item, sort_order: index })),
        equipment: gear.map((item, index) => ({
          equipment_id: item.equipment_id,
          purpose: item.purpose.trim() || undefined,
          sort_order: index,
        })),
      });
      onSaved(saved);
    } catch (caught) {
      onError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card
      title="报告配置"
      extra={
        <Button
          type="primary"
          icon={<SaveOutlined />}
          loading={busy}
          disabled={settings === null}
          onClick={() => void save()}
        >
          保存配置
        </Button>
      }
    >
      {settings === null ? (
        <Typography.Text type="secondary">正在加载…</Typography.Text>
      ) : (
        <Flex vertical gap={16}>
          <Form layout="vertical">
            <Row gutter={16}>
              <Col xs={24} md={12}>
                <Form.Item label="报告模板" htmlFor="report-settings-template" style={{ marginBottom: 0 }}>
                  <Select
                    id="report-settings-template"
                    value={templateId}
                    onChange={setTemplateId}
                    placeholder="选择一份已启用且校验通过的模板"
                    allowClear
                    options={templates
                      .filter((item) => item.is_enabled && item.validation_status === "valid")
                      .map((item) => ({
                        value: item.id,
                        label: item.is_default ? `${item.template_name}（默认）` : item.template_name,
                      }))}
                  />
                </Form.Item>
              </Col>
              <Col xs={24} md={12}>
                <Form.Item label="历史对比检查" htmlFor="report-settings-comparison" style={{ marginBottom: 0 }}>
                  <Select
                    id="report-settings-comparison"
                    value={comparisonId}
                    onChange={setComparisonId}
                    placeholder="不对比"
                    allowClear
                    options={candidates.map((item) => ({
                      value: item.inspection_year_id,
                      label: `${item.inspection_year} 年度 · ${item.status}${
                        item.overall_grade ? ` · ${item.overall_grade}` : ""
                      }`,
                    }))}
                  />
                </Form.Item>
              </Col>
            </Row>
          </Form>

          {missingRoles.length > 0 ? (
            <Alert
              type="warning"
              showIcon
              title={`模板还要求配置：${missingRoles.map(personnelRoleLabel).join("、")}`}
            />
          ) : null}

          <Flex vertical gap={8}>
            <Flex align="center" justify="space-between">
              <Typography.Title level={5} style={{ margin: 0 }}>签字人员</Typography.Title>
              <Button
                size="small"
                onClick={() =>
                  setPeople([...people, { personnel_id: "", role_code: requiredRoles[0] ?? "compiler" }])
                }
              >
                添加一行
              </Button>
            </Flex>
            {people.length === 0 ? (
              <Typography.Text type="secondary">还没有配置签字人员。</Typography.Text>
            ) : (
              people.map((row, index) => (
                <Flex gap={8} align="center" key={`person-${index}`}>
                  <Select
                    value={row.personnel_id || undefined}
                    placeholder="选择人员"
                    showSearch
                    optionFilterProp="label"
                    style={{ flex: 1, minWidth: 0 }}
                    onChange={(value) => {
                      const next = [...people];
                      next[index] = { ...row, personnel_id: value };
                      setPeople(next);
                    }}
                    options={personnelPool.map((person) => ({
                      value: person.id,
                      label: person.organization
                        ? `${person.full_name}（${person.organization}）`
                        : person.full_name,
                    }))}
                  />
                  <Select
                    value={row.role_code}
                    style={{ width: 180 }}
                    onChange={(value) => {
                      const next = [...people];
                      next[index] = { ...row, role_code: value };
                      setPeople(next);
                    }}
                    options={PERSONNEL_ROLE_OPTIONS}
                  />
                  <Button danger type="link" onClick={() => setPeople(people.filter((_, i) => i !== index))}>
                    移除
                  </Button>
                </Flex>
              ))
            )}
          </Flex>

          <Flex vertical gap={8}>
            <Flex align="center" justify="space-between">
              <Typography.Title level={5} style={{ margin: 0 }}>检测设备</Typography.Title>
              <Button size="small" onClick={() => setGear([...gear, { equipment_id: "", purpose: "" }])}>
                添加一行
              </Button>
            </Flex>
            {gear.length === 0 ? (
              <Typography.Text type="secondary">还没有配置检测设备。</Typography.Text>
            ) : (
              gear.map((row, index) => (
                <Flex gap={8} align="center" key={`gear-${index}`}>
                  <Select
                    value={row.equipment_id || undefined}
                    placeholder="选择设备"
                    showSearch
                    optionFilterProp="label"
                    style={{ flex: 1, minWidth: 0 }}
                    onChange={(value) => {
                      const next = [...gear];
                      next[index] = { ...row, equipment_id: value };
                      setGear(next);
                    }}
                    options={equipmentPool.map((item) => ({
                      value: item.id,
                      label: item.model_spec
                        ? `${item.equipment_name}（${item.model_spec}）`
                        : item.equipment_name,
                    }))}
                  />
                  <Input
                    value={row.purpose}
                    placeholder="用途，如裂缝宽度"
                    style={{ width: 220 }}
                    onChange={(event) => {
                      const next = [...gear];
                      next[index] = { ...row, purpose: event.target.value };
                      setGear(next);
                    }}
                  />
                  <Button danger type="link" onClick={() => setGear(gear.filter((_, i) => i !== index))}>
                    移除
                  </Button>
                </Flex>
              ))
            )}
          </Flex>

          {settings.configured_at ? (
            <Typography.Text type="secondary">
              上次保存：{settings.configured_at.slice(0, 19)}
              {settings.configured_by_display_name ? ` · ${settings.configured_by_display_name}` : ""}
            </Typography.Text>
          ) : null}
        </Flex>
      )}
    </Card>
  );
}

// ---------------------------------------------------------------------------
// 三、内容摘要
// ---------------------------------------------------------------------------

function SummaryCard({
  preflight,
  settings,
}: {
  preflight: ReportPreflight | null;
  settings: InspectionReportSettings | null;
}) {
  if (preflight === null) {
    return (
      <Card title="内容摘要">
        <Typography.Text type="secondary">正在统计…</Typography.Text>
      </Card>
    );
  }
  const summary = preflight.summary;
  // 覆盖清单来自模板的校验结论。清单为空说明这份模板还没有记录锚点顺序（后端据此
  // 跳过 §16.10 的检查），这时"覆盖了哪些部位"是未知，不是"一个都没覆盖"——把数据
  // 涉及的部位全标红是在报一个并不存在的问题。
  const coverageKnown = summary.template_covered_parts.length > 0;
  const covered = new Set(summary.template_covered_parts);
  const uncovered = coverageKnown
    ? summary.structure_parts_with_data.filter((part) => !covered.has(part))
    : [];

  return (
    <Card title="内容摘要">
      <Flex vertical gap={16}>
        <Row gutter={[16, 16]}>
          <Col xs={12} md={8} xl={4}><Statistic title="有病害的构件" value={summary.defect_component_count} /></Col>
          <Col xs={12} md={8} xl={5}><Statistic title="病害记录" value={summary.defect_observation_count} suffix="条" /></Col>
          <Col xs={12} md={8} xl={5}><Statistic title="去重来源病害" value={summary.source_defect_count} suffix="条" /></Col>
          <Col xs={12} md={8} xl={5}><Statistic title="照片" value={summary.photo_count} suffix="张" /></Col>
          <Col xs={12} md={8} xl={5}><Statistic title="综合评级" value={summary.overall_grade ?? "—"} /></Col>
        </Row>
        <Typography.Text type="secondary">
          病害记录与去重来源病害的差值，是构件范围拆分造成的：一条「1-5#板」会拆成五条记录，
          但与上次检查的对比按来源病害计一次。
        </Typography.Text>
        <Flex align="center" gap={6} wrap>
          <Typography.Text>本次数据涉及：</Typography.Text>
          {summary.structure_parts_with_data.length === 0 ? (
            <Tag>无正式病害</Tag>
          ) : (
            summary.structure_parts_with_data.map((part) => (
              <Tag key={part} color={!coverageKnown || covered.has(part) ? "blue" : "red"}>
                {structurePartLabel(part)}
              </Tag>
            ))
          )}
        </Flex>
        {uncovered.length > 0 ? (
          <Alert
            type="error"
            showIcon
            title={`所选模板没有覆盖：${uncovered.map(structurePartLabel).join("、")}`}
            description="这些部位的病害没有输出位置，生成会被阻断。请换一份覆盖该部位的模板。"
          />
        ) : null}
        {settings !== null ? (
          <Typography.Text type="secondary">
            第 3 章、各部位的「病害成因分析」小节，以及两张附录卡片里没有数据来源的格子，
            按首个标准模板保持空白，这是有意设计，不影响生成。
          </Typography.Text>
        ) : null}
      </Flex>
    </Card>
  );
}

// ---------------------------------------------------------------------------
// 四、当前报告
// ---------------------------------------------------------------------------

/** 体积写成人看的数。二十多兆的文件，点下载之前得知道要下多大的东西。 */
function formatBytes(bytes: number | undefined): string | null {
  if (!bytes) return null;
  const mb = bytes / 1024 / 1024;
  return mb >= 1 ? `${mb.toFixed(1)} MB` : `${Math.round(bytes / 1024)} KB`;
}

/**
 * 当前报告。
 *
 * **只有一份。** 系统不保存报告版本：临时文件到期即删，上一次生成的东西已经不在了。
 * 摆一张历史表会让人以为那些旧文件还能下，所以这里只呈现一个状态——没生成过、
 * 正在生成、可下载、失败、已过期。
 */
function CurrentReportCard({
  job,
  busy,
  canGenerate,
  onGenerate,
}: {
  job: ReportJob | null;
  busy: boolean;
  canGenerate: boolean;
  onGenerate: () => void;
}) {
  const running = job?.is_running ?? false;
  const action = (
    <Button
      type="primary"
      loading={busy}
      disabled={!canGenerate || running}
      onClick={onGenerate}
    >
      {running ? "正在生成…" : job ? "重新生成" : "生成报告"}
    </Button>
  );

  return (
    <Card title="当前报告" extra={action}>
      <Flex vertical gap={16}>
        {!canGenerate && !running ? (
          <Alert
            type="warning"
            showIcon
            title="生成条件还有未处理的阻断项，先处理完再生成。"
          />
        ) : null}

        {job === null ? (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description="这个年度还没有生成过报告"
          />
        ) : running ? (
          <RunningState job={job} />
        ) : job.can_download ? (
          <ReadyState job={job} />
        ) : job.status === "expired" ? (
          <ExpiredState job={job} />
        ) : (
          <FailedState job={job} />
        )}
      </Flex>
    </Card>
  );
}

/** 报告状态图标：语义色的方块，和指标卡的图标块同一种形态。 */
function StateIcon({ icon, tone }: { icon: JSX.Element; tone: "success" | "error" | "neutral" }) {
  const { token } = theme.useToken();
  const palette = {
    success: { color: token.colorSuccess, backgroundColor: token.colorSuccessBg },
    error: { color: token.colorError, backgroundColor: token.colorErrorBg },
    neutral: { color: token.colorTextSecondary, backgroundColor: token.colorFillTertiary },
  }[tone];
  return <Avatar shape="square" size={56} icon={icon} style={{ flex: "none", ...palette }} />;
}

function RunningState({ job }: { job: ReportJob }) {
  return (
    <Flex vertical gap={8}>
      <Progress
        percent={STAGE_PERCENT[job.status] ?? 0}
        status="active"
        format={() => REPORT_JOB_STAGE_LABELS[job.status]}
      />
      <Typography.Text type="secondary">
        {job.status === "updating_fields"
          ? "正在交给 Word 或 WPS 算目录页码和总页数。本机同时只允许一个更新在跑，排队是正常的。"
          : "装配一份真实规模的报告要一分多钟，进度会自动刷新。"}
      </Typography.Text>
    </Flex>
  );
}

function ReadyState({ job }: { job: ReportJob }) {
  const [downloading, setDownloading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const facts = [
    job.progress.page_count ? `${job.progress.page_count} 页` : null,
    job.progress.image_count ? `${job.progress.image_count} 张图` : null,
    formatBytes(job.progress.file_bytes),
  ].filter(Boolean);

  return (
    <Flex vertical gap={12}>
      <Flex align="center" gap={16} wrap>
        <StateIcon icon={<FileWordOutlined />} tone="success" />
        <Flex vertical gap={2} style={{ flex: 1, minWidth: 240 }}>
          <Typography.Text strong>{job.download_filename}</Typography.Text>
          <Typography.Text type="secondary">{facts.join(" · ")}</Typography.Text>
          <Typography.Text type="secondary">
            生成于 {job.finished_at?.slice(0, 19) ?? job.created_at.slice(0, 19)}
            {job.expires_at ? `，有效期至 ${job.expires_at.slice(0, 19)}` : ""}
            。过期后临时文件自动清理，重新生成即可。
          </Typography.Text>
        </Flex>
        <Button
          type="primary"
          size="large"
          icon={<DownloadOutlined />}
          loading={downloading}
          onClick={async () => {
            setDownloading(true);
            setError(null);
            try {
              await downloadReportJob(job.id, job.download_filename ?? "报告.docx");
            } catch (caught) {
              setError(reportErrorMessage(caught));
            } finally {
              setDownloading(false);
            }
          }}
        >
          下载报告
        </Button>
      </Flex>
      {error ? <Alert type="error" showIcon title={error} /> : null}
    </Flex>
  );
}

function FailedState({ job }: { job: ReportJob }) {
  const hint = reportJobFailureHint(job.error_code);
  return (
    <Flex align="center" gap={16}>
      <StateIcon icon={<CloseCircleFilled />} tone="error" />
      <Flex vertical gap={2}>
        <Typography.Text strong>生成失败</Typography.Text>
        <Typography.Text code>{job.error_code ?? "unknown"}</Typography.Text>
        <Typography.Text>{job.error_message ?? "失败原因未记录。"}</Typography.Text>
        {hint ? <Typography.Text type="secondary">{hint}</Typography.Text> : null}
      </Flex>
    </Flex>
  );
}

function ExpiredState({ job }: { job: ReportJob }) {
  return (
    <Flex align="center" gap={16}>
      <StateIcon icon={<ClockCircleFilled />} tone="neutral" />
      <Flex vertical gap={2}>
        <Typography.Text strong>报告已过期</Typography.Text>
        <Typography.Text type="secondary">
          上次生成于 {job.finished_at?.slice(0, 19) ?? job.created_at.slice(0, 19)}，
          临时文件已清理。系统不保存报告版本，重新生成即可拿到同样的内容。
        </Typography.Text>
      </Flex>
    </Flex>
  );
}
