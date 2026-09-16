import { InfoCircleOutlined } from "@ant-design/icons";
import { Alert, Card, Checkbox, Flex, Input, InputNumber, Tag, Typography, theme } from "antd";
import { useEffect, useMemo, useState } from "react";

import {
  componentInventoryErrorMessage,
  fetchPartCatalog,
  type CatalogPart,
  type GenerateComponentInventoryInput,
  type PartSelection,
} from "../api/componentInventoryApi";
import { backendBaseUrl } from "../config";
import { countTemplate, expandTemplate, firstNumber } from "./inventoryNumbering";
import { structurePartLabel, structurePartOrder } from "./structureParts";

export function validSpanCount(raw: string): boolean {
  const value = Number(raw);
  return raw !== "" && Number.isInteger(value) && value >= 0 && value <= 1000;
}

// 把某部件的数量维字符串解析为整数数组；任一维空或越界返回 null。
function parseCounts(raw: string[]): number[] | null {
  const parsed: number[] = [];
  for (const item of raw) {
    const value = Number(item);
    if (item === "" || !Number.isInteger(value) || value < 0 || value > 10000) return null;
    parsed.push(value);
  }
  return parsed;
}

// 勾选状态由弹窗持有而不是向导自己。向导在第一步时是卸载的，状态留在这里，
// 按"上一步"回去改个桩号才不会把已经勾好的二十个部件一起清掉。
export interface InventorySelection {
  enabled: Record<string, boolean>;
  names: Record<string, string>;
  counts: Record<string, string[]>;
  // 逐实例复选记的是展开顺序里的下标，不是编号：改现场名或改跨数后编号会变，
  // 下标仍指向同一个位置（如"0#台左侧"），用户的取舍不会丢。
  excludedIndexes: Record<string, number[]>;
}

export const emptyInventorySelection: InventorySelection = {
  enabled: {},
  names: {},
  counts: {},
  excludedIndexes: {},
};

// 报给弹窗底部常驻的那行状态：能生成多少、还差哪几个部件没填完。
export interface InventorySummary {
  total: number;
  partCount: number;
  missing: string[];
}

export const emptyInventorySummary: InventorySummary = { total: 0, partCount: 0, missing: [] };

// 单个部件生成量过千就标出来：支座是"孔 × 2 支承 × 每墩个数"，
// 数量维填错一位就是几千条，后端到 50000 才拦，中间这段只能靠这里提醒。
const LARGE_PART_TOTAL = 1000;

export function BridgeInventoryWizard({
  packageId,
  bridgeTypeId,
  spanCount,
  selection,
  onSelectionChange,
  onPlanChange,
}: {
  packageId: string;
  bridgeTypeId: string;
  spanCount: number;
  selection: InventorySelection;
  onSelectionChange: (next: InventorySelection) => void;
  onPlanChange: (plan: GenerateComponentInventoryInput | null, summary: InventorySummary) => void;
}) {
  const [parts, setParts] = useState<CatalogPart[]>([]);
  const [partsError, setPartsError] = useState<string | null>(null);
  const { token } = theme.useToken();
  const [filter, setFilter] = useState("");
  const [onlySelected, setOnlySelected] = useState(false);

  const { enabled, names, counts, excludedIndexes } = selection;

  useEffect(() => {
    if (!packageId || !bridgeTypeId) {
      setParts([]);
      setPartsError(null);
      return;
    }
    let cancelled = false;
    fetchPartCatalog(backendBaseUrl, packageId, bridgeTypeId)
      .then((loaded) => {
        if (cancelled) return;
        setParts(loaded);
        setPartsError(null);
      })
      .catch((caught) => {
        if (cancelled) return;
        setParts([]);
        setPartsError(componentInventoryErrorMessage(caught));
      });
    return () => {
      cancelled = true;
    };
  }, [packageId, bridgeTypeId]);

  const partName = (part: CatalogPart) => names[part.part_key] ?? part.default_name;
  const partCounts = (part: CatalogPart) => counts[part.part_key] ?? part.count_inputs.map(() => "");
  const resolvedName = (part: CatalogPart) => partName(part).trim() || part.default_name;

  // 逐实例可选的部件（翼墙/锥坡/护坡）几何上只有 2~4 个位置，展开开销可以忽略；
  // 其余部件一律只算基数，不展开。
  const partNumbers = (part: CatalogPart) => {
    const parsed = parseCounts(partCounts(part));
    if (parsed === null) return [];
    return expandTemplate(part.number_template, resolvedName(part), parsed, spanCount);
  };

  const partTotal = (part: CatalogPart): number | null => {
    const parsed = parseCounts(partCounts(part));
    if (parsed === null) return null;
    if (part.instance_selectable) {
      const numbers = partNumbers(part);
      return numbers.length - (excludedIndexes[part.part_key] ?? []).length;
    }
    return countTemplate(part.number_template, resolvedName(part), parsed, spanCount);
  };

  const derived = useMemo(() => {
    const selections: PartSelection[] = [];
    const missing: string[] = [];
    let total = 0;
    let partCount = 0;
    for (const part of parts) {
      if (!enabled[part.part_key]) continue;
      partCount += 1;
      const name = partName(part).trim();
      const parsed = parseCounts(partCounts(part));
      if (!name || parsed === null) {
        missing.push(part.default_name);
        continue;
      }
      const entry: PartSelection = { part_key: part.part_key, site_name: name, counts: parsed };
      if (part.instance_selectable) {
        const numbers = partNumbers(part);
        const dropped = excludedIndexes[part.part_key] ?? [];
        // 下标转成编号再回传，后端按自己展开的结果校验，前后端不一致会明确报错。
        entry.excluded_numbers = dropped
          .map((index) => numbers[index]?.number)
          .filter((number): number is string => !!number);
        // 全部去掉等于这个部件不存在，不必生成。
        if (entry.excluded_numbers.length === numbers.length) {
          partCount -= 1;
          continue;
        }
        total += numbers.length - entry.excluded_numbers.length;
      } else {
        total += countTemplate(part.number_template, name, parsed, spanCount);
      }
      selections.push(entry);
    }
    const valid = missing.length === 0 && selections.length > 0;
    return { selections, valid, total, partCount, missing };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [parts, enabled, names, counts, spanCount, excludedIndexes]);

  useEffect(() => {
    onPlanChange(
      derived.valid
        ? {
            standard_package_id: packageId,
            bridge_type_id: bridgeTypeId,
            span_count: spanCount,
            part_selections: derived.selections,
          }
        : null,
      { total: derived.total, partCount: derived.partCount, missing: derived.missing }
    );
  }, [derived, onPlanChange, packageId, bridgeTypeId, spanCount]);

  function update(patch: Partial<InventorySelection>) {
    onSelectionChange({ ...selection, ...patch });
  }

  function toggle(part: CatalogPart, on: boolean) {
    update({ enabled: { ...enabled, [part.part_key]: on } });
  }

  function setName(part: CatalogPart, value: string) {
    update({ names: { ...names, [part.part_key]: value } });
  }

  function setCount(part: CatalogPart, index: number, value: string) {
    const existing = counts[part.part_key] ?? part.count_inputs.map(() => "");
    const next = existing.slice();
    next[index] = value;
    update({ counts: { ...counts, [part.part_key]: next } });
  }

  function toggleInstance(part: CatalogPart, index: number, on: boolean) {
    const dropped = excludedIndexes[part.part_key] ?? [];
    const next = on
      ? dropped.filter((item) => item !== index)
      : dropped.includes(index)
        ? dropped
        : [...dropped, index];
    update({ excludedIndexes: { ...excludedIndexes, [part.part_key]: next } });
  }

  // 回车在数量框里是录入时的本能动作，而向导整体是一个 form——不拦就直接触发创建。
  function blockEnter(event: { key: string; preventDefault: () => void }) {
    if (event.key === "Enter") event.preventDefault();
  }

  const needle = filter.trim().toLocaleLowerCase();
  const visible = parts.filter((part) => {
    if (onlySelected && !enabled[part.part_key]) return false;
    if (!needle) return true;
    return [part.default_name, part.standard_component_category_name]
      .some((value) => value.toLocaleLowerCase().includes(needle));
  });

  // 只按结构分部分组。部件类别（16 个规范类别）降成行内灰字：20 个部件配 12 个类别标题，
  // 标题比内容还占地方。
  const groups = structurePartOrder
    .map((key) => ({ key, parts: visible.filter((part) => part.structure_part === key) }))
    .filter((group) => group.parts.length > 0);

  function renderInstances(part: CatalogPart) {
    const numbers = partNumbers(part);
    if (numbers.length === 0) return null;
    const dropped = excludedIndexes[part.part_key] ?? [];
    return (
      <Flex wrap gap={6} role="group" aria-label={`${part.default_name} 位置`}>
        {numbers.map((item, index) => (
          <Checkbox
            key={item.number}
            checked={!dropped.includes(index)}
            onChange={(event) => toggleInstance(part, index, event.target.checked)}
          >
            {item.number}
          </Checkbox>
        ))}
      </Flex>
    );
  }

  function renderCounts(part: CatalogPart) {
    return part.count_inputs.map((countInput, index) => (
      <Flex align="center" gap={6} key={countInput.key}>
        <Typography.Text type="secondary">
          {countInput.label}
          {/* 口径说明（"只数一个孔落在这个墩上的支座"）挂成 title，行内不再多占一行。 */}
          {countInput.hint ? (
            <abbr title={countInput.hint} style={{ cursor: "help", textDecoration: "none" }}>
              {" "}<InfoCircleOutlined />
            </abbr>
          ) : null}
        </Typography.Text>
        {/* 不设 min/max：越界由「请填数量」提示，不在失焦时悄悄改成边界值。 */}
        <InputNumber
          size="small"
          precision={0}
          aria-label={`${part.default_name} ${countInput.label}`}
          value={partCounts(part)[index] ? Number(partCounts(part)[index]) : null}
          onChange={(value) => setCount(part, index, value === null ? "" : String(value))}
          onKeyDown={blockEnter}
        />
      </Flex>
    ));
  }

  function renderOutput(part: CatalogPart, total: number | null) {
    // 生成数靠右排成一列，扫一眼就知道哪个部件撑爆了。
    if (total === null) {
      return (
        <Typography.Text type="warning" strong style={{ marginLeft: "auto" }}>请填数量</Typography.Text>
      );
    }
    const parsed = parseCounts(partCounts(part)) ?? [];
    const sample = part.instance_selectable
      ? null
      : firstNumber(part.number_template, resolvedName(part), parsed, spanCount);
    return (
      <Flex align="baseline" gap={6} style={{ marginLeft: "auto", whiteSpace: "nowrap" }}>
        <Typography.Text strong type={total >= LARGE_PART_TOTAL ? "warning" : undefined}>共 {total} 个</Typography.Text>
        {sample ? <Typography.Text type="secondary">{sample}…</Typography.Text> : null}
      </Flex>
    );
  }

  function renderRow(part: CatalogPart) {
    const on = !!enabled[part.part_key];
    const total = on ? partTotal(part) : null;
    const large = total !== null && total >= LARGE_PART_TOTAL;
    return (
      <Flex
        key={part.part_key}
        align="center"
        gap="small"
        wrap
        style={{
          minHeight: 40,
          padding: "3px 8px",
          borderBottom: `1px solid ${token.colorSplit}`,
          background: large ? token.colorWarningBg : undefined,
        }}
      >
        {/* 定宽的头一格：勾没勾都占同样宽度，后面的类别、数量、生成数才对得成列。
            未勾时整格是可点的标签，点部件名就能勾上；勾上后名字换成可改的输入框。 */}
        <Flex align="center" gap={8} style={{ flex: "none", width: "9rem" }}>
          <Checkbox
            aria-label={`启用 ${part.default_name}`}
            checked={on}
            onChange={(event) => toggle(part, event.target.checked)}
          >
            {on ? null : part.default_name}
          </Checkbox>
          {on ? (
            <Input
              size="small"
              aria-label={`${part.default_name} 名称`}
              value={partName(part)}
              onChange={(event) => setName(part, event.target.value)}
              onKeyDown={blockEnter}
            />
          ) : null}
        </Flex>
        <Typography.Text type="secondary" style={{ flex: "none", width: "8rem" }}>
          {part.standard_component_category_name}
        </Typography.Text>
        {part.provisional ? <Tag color="gold">临时编号（待校准）</Tag> : null}
        {on ? (part.instance_selectable ? renderInstances(part) : renderCounts(part)) : null}
        {on ? renderOutput(part, total) : null}
      </Flex>
    );
  }

  const chosen = parts.filter((part) => enabled[part.part_key]).length;

  return (
    <Flex component="section" vertical gap={8} aria-labelledby="inventory-wizard-title">
      {/* 工具栏钉在滚动区顶部：部件清单二十行起步，筛选框不能跟着滚走。 */}
      <Card size="small" style={{ position: "sticky", top: 0, zIndex: 1 }}>
        <Flex align="center" gap={12}>
          <Typography.Text strong id="inventory-wizard-title" style={{ whiteSpace: "nowrap" }}>
            勾选桥上有的部件
          </Typography.Text>
          <Input
            size="small"
            allowClear
            aria-label="筛选部件"
            placeholder="筛选部件"
            style={{ flex: 1, minWidth: 0 }}
            value={filter}
            onChange={(event) => setFilter(event.target.value)}
            onKeyDown={blockEnter}
          />
          <Checkbox checked={onlySelected} onChange={(event) => setOnlySelected(event.target.checked)}>
            只看已选
          </Checkbox>
          <Typography.Text type="secondary" style={{ whiteSpace: "nowrap" }}>
            已选 {chosen} / {parts.length}
          </Typography.Text>
        </Flex>
      </Card>
      {partsError ? <Alert type="error" showIcon title={partsError} /> : null}
      {parts.length > 0 && groups.length === 0 ? <Typography.Text type="secondary">没有匹配的部件。</Typography.Text> : null}
      {groups.map((group) => (
        <Flex vertical key={group.key}>
          <Flex align="baseline" gap={8} style={{ paddingTop: 8 }}>
            <Typography.Title level={5} style={{ margin: 0 }}>{structurePartLabel(group.key)}</Typography.Title>
            <Typography.Text type="secondary">
              {group.parts.filter((part) => enabled[part.part_key]).length} / {group.parts.length}
            </Typography.Text>
          </Flex>
          {group.parts.map((part) => renderRow(part))}
        </Flex>
      ))}
    </Flex>
  );
}
