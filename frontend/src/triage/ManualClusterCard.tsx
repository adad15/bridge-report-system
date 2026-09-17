import { Button, Card, Checkbox, Flex, Image, Input, Radio, Select, Table, Tag, Typography, theme, type TableColumnsType } from "antd";
import { useState } from "react";
import { Link } from "react-router-dom";

import type {
  TriageManualCluster,
  TriageManualGroup,
  TriageManualObservation,
  TriageResolvePayload,
} from "../api/threadTriageApi";
import { needsInexactMergeConfirmation } from "../api/threadTriageApi";
import { defectPhotoContentUrl } from "../api/componentArchiveApi";
import { backendBaseUrl } from "../config";

/**
 * 异常簇卡片：**同屏**展示一簇里全部的组、位置与历年观测。
 *
 * 这里的界面形状是被问题本身逼出来的。系统之所以不敢自动合并"梁底"和"梁底部"，正是
 * 因为它没法判断这两处是不是同一道裂缝；而人要作出判断，就得同时看见这两组各自哪几年
 * 有记录、量测怎么变。一旦退化成"一观测一张卡"，判断所需的上下文恰好被拆没了——旧
 * 整理页就是这么失败的。
 *
 * 每格给的是**证据**而不只是标签：标度看恶化趋势、尺寸看连续性、照片是最终判据。只摆
 * 位置写法和病害类型，等于把系统已经判不了的那个信号原样还给人再看一遍。
 *
 * 因此这张卡不问"这条观测归哪儿"，而是让人**勾选哪些观测属于同一处病害**，再一次性
 * 落成一条线索。合并（全勾）与拆分（分次勾）用的是同一个动作，因为服务端 resolve 接口
 * 本来就是这个形状：给一组观测 + 人选定的类型与位置。
 */
interface ManualClusterCardProps {
  cluster: TriageManualCluster;
  bridgeId: string;
  busy: boolean;
  onResolve: (payload: TriageResolvePayload) => void;
  onSkip: () => void;
}

const REASON_LABELS: Record<string, string> = {
  location_overlap: "位置写法可能指同一处",
  multiple_in_year: "同一年有多条记录",
  ambiguous_thread: "同时命中多条已有线索",
};

function reasonLabel(code: string): string {
  return REASON_LABELS[code] ?? code;
}

function locationText(location: string | null): string {
  // 空位置是合法取值而非缺漏：整座构件的通病本来就没有具体位置。
  return location && location.trim().length > 0 ? location : "（无位置）";
}

function observationKey(observation: TriageManualObservation): string {
  return observation.id;
}

interface MajorityPick {
  value: string;
  count: number;
}

/**
 * 按多数决预填类型与位置，同票取最新年度。
 *
 * 原来取的是"最新年度排序后的第一条"：5 条里 4 条横向裂缝、2026 年多出一条纵向裂缝时，
 * 它会填出"纵向裂缝"，照着提交就建出一条名不副实的线索。
 */
function majorityPick(
  observations: TriageManualObservation[],
  read: (observation: TriageManualObservation) => string,
): MajorityPick {
  const tally = new Map<string, number>();
  for (const observation of observations) {
    const value = read(observation);
    tally.set(value, (tally.get(value) ?? 0) + 1);
  }
  // 从最新年度往回扫，配合严格大于号，同票时留下的就是最新那一条的写法。
  const newestFirst = [...observations].sort(
    (left, right) => right.inspection_year - left.inspection_year);
  let picked: MajorityPick = { value: "", count: 0 };
  for (const observation of newestFirst) {
    const value = read(observation);
    const count = tally.get(value) ?? 0;
    if (count > picked.count) picked = { value, count };
  }
  return picked;
}

/** 勾选集合里出现的写法及其条数，用于把"哪里不一致"讲具体。 */
function wordingBreakdown(
  observations: TriageManualObservation[],
  read: (observation: TriageManualObservation) => string,
): Array<{ value: string; count: number }> {
  const tally = new Map<string, number>();
  for (const observation of observations) {
    const value = read(observation);
    tally.set(value, (tally.get(value) ?? 0) + 1);
  }
  return [...tally.entries()]
    .map(([value, count]) => ({ value, count }))
    .sort((left, right) => right.count - left.count);
}

export function ManualClusterCard({
  cluster, bridgeId, busy, onResolve, onSkip,
}: ManualClusterCardProps) {
  const { token } = theme.useToken();
  const allObservations = cluster.groups.flatMap((group) => group.observations);
  // 簇是**按位置**聚的：同一年同一位置有多条时，类型不同的病害会被顺带卷进来。
  // 跨位置写法合并正是这张卡的用途（"梁底"和"梁底部"多半是同一道裂缝）；跨病害类型
  // 不是——横向裂缝和网状裂缝是两种损伤形态，默认全勾等于主动提议一次错误合并。
  // 所以默认只勾占多数的那一种类型，其余留到下一轮，人想合并仍可自己勾上。
  const dominantType = majorityPick(
    allObservations, (observation) => observation.defect_type).value;
  const [selectedIds, setSelectedIds] = useState<Set<string>>(
    () => new Set(allObservations
      .filter((observation) => observation.defect_type === dominantType)
      .map(observationKey)));
  const [mode, setMode] = useState<"create" | "bind">(
    cluster.related_threads.length > 0 ? "bind" : "create");
  const [targetThreadId, setTargetThreadId] = useState<string>(
    cluster.related_threads[0]?.id ?? "");
  const [typeDraft, setTypeDraft] = useState<string | null>(null);
  const [locationDraft, setLocationDraft] = useState<string | null>(null);
  const [confirmInexact, setConfirmInexact] = useState(false);

  const years = [...new Set(allObservations.map((item) => item.inspection_year))]
    .sort((left, right) => left - right);
  const selected = allObservations.filter(
    (observation) => selectedIds.has(observation.id));
  const unselected = allObservations.filter(
    (observation) => !selectedIds.has(observation.id));
  // 一簇里的组一定同属一个构件（聚簇本来就是按构件做的），线索也只能落在这个构件上。
  const bridgeComponentId = cluster.groups[0]?.bridge_component_id ?? "";

  // 预填跟着勾选走，人一旦改过就不再被覆盖——不用 effect 同步 state，直接算显示值。
  const typeMajority = majorityPick(selected, (observation) => observation.defect_type);
  const locationMajority = majorityPick(
    selected, (observation) => observation.defect_location ?? "");
  const defectType = typeDraft ?? typeMajority.value;
  const defectLocation = locationDraft ?? locationMajority.value;

  const inexact = needsInexactMergeConfirmation(selected);
  const locationSpread = wordingBreakdown(
    selected, (observation) => locationText(observation.defect_location));
  const typeSpread = wordingBreakdown(selected, (observation) => observation.defect_type);
  const blocked = selected.length === 0
    || (mode === "create" && defectType.trim().length === 0)
    || (mode === "create" && inexact && !confirmInexact)
    || (mode === "bind" && targetThreadId.length === 0);

  // 同一年出现多条，是这簇需要人看的直接原因之一，点名到年份比一句笼统标签有用。
  const crowdedYears = years.filter((year) => cluster.groups.some(
    (group) => group.observations.filter(
      (observation) => observation.inspection_year === year).length > 1));
  const clusterWordings = [...new Set(
    cluster.groups.map((group) => locationText(group.defect_location)))];
  // 与已有线索的重叠是另一回事：该线索已经占着这个位置，人得知道再建一条会重。
  const overlappingThreads = [...new Set(
    cluster.overlap_targets
      .filter((target) => target.kind === "thread")
      .map((target) => target.system_number ?? "")
      .filter((value) => value.length > 0))];
  const whyParts = [
    // 说"归一化后同指某处"是过度断言：它们只是互相重叠，归一化结果本来就可能各不相同
    // （"0#台顶,右侧行车道"覆盖了另外两个，而不等于它们）。
    clusterWordings.length > 1
      ? `${clusterWordings.length} 种位置写法归一化后互相重叠：`
        + clusterWordings.map((wording) => `「${wording}」`).join("")
      : "",
    overlappingThreads.length > 0
      ? `与已有线索 ${overlappingThreads.join("、")} 的位置重叠`
      : "",
    crowdedYears.length > 0 ? `${crowdedYears.join("、")} 年同一位置有多条记录` : "",
  ].filter((part) => part.length > 0);

  // 预览按表格顺序摊平：位置行 → 年度 → 该观测的多张照片。
  // 顺序必须与下面 <Image> 的渲染顺序完全一致，imageRender 的 current 才索引得对。
  const previewItems = cluster.groups.flatMap((group) => years.flatMap((year) =>
    group.observations
      .filter((observation) => observation.inspection_year === year)
      .flatMap((observation) => (observation.photos ?? []).map((photo) => ({
        photo,
        observation,
        location: locationText(group.defect_location),
      })))));

  function toggle(observationId: string): void {
    setSelectedIds((current) => {
      const next = new Set(current);
      if (next.has(observationId)) next.delete(observationId);
      else next.add(observationId);
      return next;
    });
  }

  function submit(): void {
    onResolve({
      action: mode,
      bridge_component_id: bridgeComponentId,
      ...(mode === "create"
        ? {
            defect_type: defectType.trim(),
            defect_location: defectLocation.trim(),
            ...(inexact ? { confirm_inexact_merge: true } : {}),
          }
        : { target_thread_id: targetThreadId }),
      observations: selected.map((observation) => ({
        id: observation.id,
        updated_at: observation.updated_at,
      })),
    });
  }

  const columns: TableColumnsType<TriageManualGroup> = [
    {
      title: "位置写法",
      key: "location",
      width: 180,
      render: (_value, group) => (
        <Flex vertical gap={2}>
          <Typography.Text strong>{locationText(group.defect_location)}</Typography.Text>
          <Typography.Text type="secondary">{group.defect_type ?? ""}</Typography.Text>
        </Flex>
      ),
    },
    ...years.map((year) => ({
      title: String(year),
      key: `year-${year}`,
      width: 220,
      render: (_value: unknown, group: TriageManualGroup) => {
        // 同一年可能有多条——并排放，让人直接比着判断是重复记录还是两处病害。
        const inYear = group.observations.filter((observation) => observation.inspection_year === year);
        if (inYear.length === 0) return <Typography.Text type="secondary">—</Typography.Text>;
        return (
          <Flex gap={8} wrap>
            {inYear.map((observation, index) => (
              <ObservationTile
                key={observation.id}
                observation={observation}
                checked={selectedIds.has(observation.id)}
                label={`${locationText(group.defect_location)} ${year} 第 ${index + 1} 条`}
                onToggle={() => toggle(observation.id)}
              />
            ))}
          </Flex>
        );
      },
    })),
  ];

  return (
    <Card
      role="region"
      aria-label={`异常簇 ${cluster.groups[0]?.business_component_code ?? cluster.cluster_id}`}
      title={
        <Flex align="center" gap={10} wrap>
          <Typography.Text strong>{cluster.groups[0]?.business_component_code ?? "未知构件"}</Typography.Text>
          {/* 组、观测两个口径分开说：10 组不等于 10 条，混着写会让人以为工作量小一半。 */}
          <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>
            {cluster.group_count} 组 · {cluster.observation_count} 条观测
          </Typography.Text>
        </Flex>
      }
      extra={
        <Flex gap={6} wrap>
          {cluster.reason_codes.map((code) => (
            <Tag key={code} color="warning" variant="filled">{reasonLabel(code)}</Tag>
          ))}
        </Flex>
      }
    >
      <Flex vertical gap={14}>
        {/* 笼统说一句"可能指同一处"帮不了判断，要说清是哪几种写法、哪一年挤了多条。 */}
        {whyParts.length > 0 ? <Typography.Text type="secondary">{whyParts.join("；")}</Typography.Text> : null}

        {/* 放大后左右翻页在**同一屏幕位置**切换年度：闪切比并排更容易看出是不是同一道裂缝。
            说明条保留年度/标度/尺寸/编号，否则放大了反而丢掉比对基准。 */}
        <Image.PreviewGroup
          preview={{
            imageRender: (originalNode, info) => {
              const item = previewItems[info.current];
              if (!item) return originalNode;
              const measurements = item.observation.measurements ?? [];
              return (
                <Flex vertical align="center" gap={12}>
                  {originalNode}
                  <Flex
                    align="center"
                    gap={12}
                    wrap
                    justify="center"
                    style={{
                      padding: "8px 16px",
                      borderRadius: token.borderRadiusLG,
                      background: "rgba(0, 0, 0, 0.55)",
                      color: "#ffffff",
                    }}
                  >
                    <strong>{item.observation.inspection_year} 年</strong>
                    <span>{item.observation.defect_type}</span>
                    <span>{item.location}</span>
                    <span>标度 {item.observation.scale ?? "-"}</span>
                    <span>{measurements.length > 0 ? measurements.join("；") : "无尺寸"}</span>
                    <span>{item.observation.system_number ?? ""}｜照片 {item.photo.photo_number}</span>
                  </Flex>
                </Flex>
              );
            },
          }}
        >
          {/* 证据块比原来的 chip 宽，窄窗口下让表格自己横向滚动，不把整页撑出横条。 */}
          <Table<TriageManualGroup>
            rowKey="group_id"
            size="small"
            bordered
            aria-label="簇内各位置历年观测"
            columns={columns}
            dataSource={cluster.groups}
            pagination={false}
            scroll={{ x: 180 + years.length * 220 }}
          />
        </Image.PreviewGroup>

        {cluster.related_threads.length > 0 ? (
          <Flex vertical gap={6}>
            <Typography.Text strong>该构件上已有的线索</Typography.Text>
            {cluster.related_threads.map((thread) => (
              <Flex key={thread.id} align="center" gap={8} wrap>
                {/* 编号是人在报告里引用线索的唯一凭据，必须显示。 */}
                <Typography.Text type="secondary">{thread.system_number}</Typography.Text>
                <Typography.Text>{thread.thread_name}</Typography.Text>
              </Flex>
            ))}
          </Flex>
        ) : null}

        <Flex vertical gap={10}>
          <Typography.Text strong>把勾选的 {selected.length} 条观测</Typography.Text>
          <Radio.Group value={mode} onChange={(event) => setMode(event.target.value)}>
            <Radio value="create">合并为一条新线索</Radio>
            <Radio value="bind" disabled={cluster.related_threads.length === 0}>绑定到已有线索</Radio>
          </Radio.Group>

          {mode === "create" ? (
            <Flex vertical gap={10}>
              <Flex gap={12} wrap>
                <Flex vertical gap={4} style={{ minWidth: 220 }}>
                  <Typography.Text type="secondary">病害类型</Typography.Text>
                  <Input
                    aria-label="病害类型"
                    value={defectType}
                    onChange={(event) => setTypeDraft(event.target.value)}
                  />
                </Flex>
                <Flex vertical gap={4} style={{ minWidth: 220 }}>
                  <Typography.Text type="secondary">位置（可留空）</Typography.Text>
                  <Input
                    aria-label="位置（可留空）"
                    value={defectLocation}
                    onChange={(event) => setLocationDraft(event.target.value)}
                  />
                </Flex>
              </Flex>
              {typeDraft === null && locationDraft === null && selected.length > 0 ? (
                <Typography.Text type="secondary">
                  按 {typeMajority.count}/{selected.length} 条多数预填
                </Typography.Text>
              ) : null}
              {inexact ? (
                // 勾选的写法不止一种，这一步就是人在替系统担下"它们是同一处"的判断。
                // 讲清差在哪：只说"写法不一致"，人还得自己回表里数一遍。
                <Checkbox checked={confirmInexact} onChange={(event) => setConfirmInexact(event.target.checked)}>
                  勾选的 {selected.length} 条
                  {locationSpread.length > 1
                    ? `跨 ${locationSpread.length} 种位置写法：${locationSpread.map(
                        (item) => `${item.value}（${item.count}）`).join(" · ")}`
                    : ""}
                  {locationSpread.length > 1 && typeSpread.length > 1 ? "；" : ""}
                  {typeSpread.length > 1
                    ? `跨 ${typeSpread.length} 种病害类型：${typeSpread.map(
                        (item) => `${item.value}（${item.count}）`).join(" · ")}`
                    : ""}
                  。我确认它们是同一处病害
                </Checkbox>
              ) : null}
            </Flex>
          ) : (
            <Flex vertical gap={4} style={{ maxWidth: 420 }}>
              <Typography.Text type="secondary">目标线索</Typography.Text>
              <Select
                aria-label="目标线索"
                value={targetThreadId || undefined}
                onChange={setTargetThreadId}
                options={cluster.related_threads.map((thread) => ({
                  value: thread.id,
                  label: `${thread.system_number}｜${thread.thread_name}`,
                }))}
              />
            </Flex>
          )}
        </Flex>

        <Flex align="center" gap={8} wrap>
          <Button type="primary" disabled={busy || blocked} onClick={submit}>
            {mode === "create" ? "建为一条线索" : "绑定到该线索"}
          </Button>
          <Button disabled={busy} onClick={onSkip}>暂不处理</Button>
          {bridgeComponentId ? (
            <Link to={`/bridges/${bridgeId}/components/${bridgeComponentId}`}>打开构件档案</Link>
          ) : null}
        </Flex>

        {selected.length === 0 ? (
          <Typography.Text type="secondary">先勾选属于同一处病害的观测。</Typography.Text>
        ) : null}
        {unselected.length > 0 ? (
          // 落选的是哪几条要点名，并说清为什么没勾——默认按类型分开时尤其不能让人以为是漏了。
          <Typography.Text type="secondary">
            余下 {unselected.length} 条这次不处理：
            {unselected.map((observation) => `${observation.inspection_year} 年 ${observation.defect_type}`
              + `${observation.system_number ? `（${observation.system_number}）` : ""}`).join("、")}
            。
            {unselected.every((observation) => observation.defect_type !== dominantType)
              ? "病害类型与上面那组不同，默认不并入；确属同一处病害可自行勾选。"
              : "可在本次落库后再来一轮。"}
          </Typography.Text>
        ) : null}
      </Flex>
    </Card>
  );
}

interface ObservationTileProps {
  observation: TriageManualObservation;
  checked: boolean;
  label: string;
  onToggle: () => void;
}

/** 一条年度观测的证据块：勾选框 + 类型 + 照片 + 标度 + 尺寸 + 观测编号。 */
function ObservationTile({ observation, checked, label, onToggle }: ObservationTileProps) {
  const { token } = theme.useToken();
  const photos = observation.photos ?? [];
  const measurements = observation.measurements ?? [];
  return (
    <Flex
      vertical
      gap={6}
      style={{
        padding: 8,
        borderRadius: token.borderRadiusLG,
        border: `1px solid ${checked ? token.colorPrimary : token.colorBorderSecondary}`,
        background: checked ? token.colorPrimaryBg : token.colorBgContainer,
      }}
    >
      {/* 勾选框只管勾选：点照片是放大，不该顺手改变勾选。 */}
      <Checkbox checked={checked} aria-label={label} onChange={onToggle}>
        <Typography.Text>{observation.defect_type}</Typography.Text>
      </Checkbox>
      <Flex align="center" gap={8}>
        {photos.length > 0 ? (
          <Flex align="center" gap={4}>
            <Image
              src={defectPhotoContentUrl(backendBaseUrl, photos[0].id)}
              alt={`${observation.inspection_year} 年 ${observation.defect_type} 照片 ${photos[0].photo_number}`}
              width={64}
              height={48}
              style={{ objectFit: "cover", borderRadius: token.borderRadius }}
            />
            {photos.length > 1 ? <Typography.Text type="secondary">+{photos.length - 1}</Typography.Text> : null}
            {/* 其余照片不占位置，但要留在预览组里，放大后翻得到。 */}
            {photos.slice(1).map((photo) => (
              <Image
                key={photo.id}
                src={defectPhotoContentUrl(backendBaseUrl, photo.id)}
                alt={`${observation.inspection_year} 年 ${observation.defect_type} 照片 ${photo.photo_number}`}
                style={{ display: "none" }}
              />
            ))}
          </Flex>
        ) : null}
        <Flex vertical>
          <Typography.Text type="secondary">标度 {observation.scale ?? "-"}</Typography.Text>
          <Typography.Text type="secondary" ellipsis title={measurements.join("；")} style={{ maxWidth: 120 }}>
            {measurements.length > 0 ? measurements.join("；") : "无尺寸"}
          </Typography.Text>
        </Flex>
      </Flex>
      {observation.system_number ? (
        <Typography.Text type="secondary">{observation.system_number}</Typography.Text>
      ) : null}
    </Flex>
  );
}
