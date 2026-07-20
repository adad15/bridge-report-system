import { useEffect, useMemo, useState } from "react";

import type {
  GenerateComponentInventoryInput,
  InventoryGenerationGroup,
  NumberingMode,
} from "../api/componentInventoryApi";
import {
  fetchStandardCatalog,
  fetchStandardPackages,
  standardsErrorMessage,
  type StandardCatalog,
  type StandardComponentCategory,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";

interface KindDraft {
  name: string;
  quantity: string;
  numberingMode: NumberingMode;
  prefix: string;
  suffix: string;
}

interface CardDraft {
  categoryId: string;
  kinds: KindDraft[];
}

export interface InventoryPreviewItem {
  quantityKey: string;
  siteType: string;
  count: number;
  numbers: string[];
}

const quantityLabels: Record<string, string> = {
  span_count: "跨数",
  upper_bearing_members_per_span: "每跨上部承重构件数",
  upper_general_members_per_span: "每跨上部一般构件数",
  bearings_per_support_line: "支座数量",
  pier_count: "桥墩数量",
  abutment_count: "桥台数量",
  main_arch_ring_count: "主拱圈数量",
  spandrel_structure_count: "拱上结构数量",
  deck_slab_count: "桥面板数量",
  arch_segment_count: "拱片数量",
  transverse_link_count: "横向联结系数量",
  arch_rib_count: "拱肋数量",
  column_count: "立柱数量",
  hanger_count: "吊杆／吊索数量",
  tie_rod_count: "系杆数量",
  deck_slab_or_beam_count: "桥面板（梁）数量",
  main_cable_count: "主缆数量",
  cable_clamp_count: "索夹数量",
  stiffening_girder_count: "加劲梁数量",
  tower_count: "索塔数量",
  anchorage_count: "锚碇数量",
  stay_cable_count: "斜拉索系统数量",
  main_girder_count: "主梁数量",
};

export function quantityLabel(key: string): string {
  return quantityLabels[key] ?? key;
}

export function previewInventoryNumbers(
  groups: InventoryGenerationGroup[],
  spanCount: number,
  limit = 8
): InventoryPreviewItem[] {
  return groups.map((group) => {
    const numbers: string[] = [];
    const outerCount = group.numbering_mode === "span_member" ? spanCount : 1;
    const total = outerCount * group.quantity;
    for (let outer = 1; outer <= outerCount && numbers.length < limit; outer += 1) {
      for (let inner = 1; inner <= group.quantity && numbers.length < limit; inner += 1) {
        numbers.push(
          `${group.number_prefix ?? ""}${
            group.numbering_mode === "span_member" ? `${outer}-${inner}` : inner
          }${group.number_suffix ?? "#"}`
        );
      }
    }
    return { quantityKey: group.quantity_key, siteType: group.site_component_type, count: total, numbers };
  });
}

function compatibleCategories(catalog: StandardCatalog, bridgeTypeId: string, references: string[]) {
  return catalog.component_categories.filter(
    (category) =>
      category.generatable &&
      category.bridge_type_ids.includes(bridgeTypeId) &&
      (references.length === 0 || references.includes(category.id))
  );
}

function templateNumberingMode(key: string): NumberingMode {
  return key.includes("per_span") ? "span_member" : "sequential";
}

function newKind(name: string, numberingMode: NumberingMode, quantity: string): KindDraft {
  return { name, quantity, numberingMode, prefix: "", suffix: "#" };
}

function defaultTemplateCard(key: string): CardDraft {
  return { categoryId: "", kinds: [newKind("", templateNumberingMode(key), "")] };
}

function defaultExtraCard(category: StandardComponentCategory): CardDraft {
  return { categoryId: category.id, kinds: [newKind(category.name, "sequential", "0")] };
}

function kindTotal(card: CardDraft): number {
  return card.kinds.reduce((total, kind) => {
    const value = Number(kind.quantity);
    return Number.isInteger(value) && value > 0 ? total + value : total;
  }, 0);
}

export function BridgeInventoryWizard({
  onPlanChange,
}: {
  onPlanChange: (plan: GenerateComponentInventoryInput | null) => void;
}) {
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>([]);
  const [packageId, setPackageId] = useState("");
  const [bridgeTypeId, setBridgeTypeId] = useState("");
  const [spanCount, setSpanCount] = useState("");
  const [cards, setCards] = useState<Record<string, CardDraft>>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    fetchStandardPackages(backendBaseUrl)
      .then(async (packages) => {
        const available = packages.filter(
          (item) => item.family === "technical_condition" && item.is_enabled && item.sync_status === "正常"
        );
        const loaded = await Promise.all(available.map((item) => fetchStandardCatalog(backendBaseUrl, item.id)));
        if (cancelled) return;
        setCatalogs(loaded);
        if (loaded.length === 1) setPackageId(loaded[0].package.id);
        setError(null);
      })
      .catch((caught) => {
        if (!cancelled) setError(standardsErrorMessage(caught));
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const catalog = catalogs.find((item) => item.package.id === packageId) ?? null;
  const template = catalog?.inventory_templates.find((item) => item.bridge_type_id === bridgeTypeId) ?? null;
  const categories = useMemo(
    () =>
      catalog && template
        ? compatibleCategories(catalog, bridgeTypeId, template.references ?? [])
        : [],
    [catalog, template, bridgeTypeId]
  );
  const extraCategories = useMemo(
    () =>
      catalog && template
        ? catalog.component_categories.filter(
            (category) =>
              category.generatable &&
              category.bridge_type_ids.includes(bridgeTypeId) &&
              !categories.some((item) => item.id === category.id)
          )
        : [],
    [catalog, template, bridgeTypeId, categories]
  );

  const derived = useMemo(() => {
    if (!catalog || !template) return null;
    const parsedQuantities: Record<string, number> = {};
    const groups: InventoryGenerationGroup[] = [];
    let valid = true;

    if (template.quantity_inputs.includes("span_count")) {
      const value = Number(spanCount);
      if (spanCount === "" || !Number.isInteger(value) || value < 0 || value > 1000) valid = false;
      parsedQuantities.span_count = spanCount === "" ? 0 : value;
    }

    const collect = (
      key: string,
      card: CardDraft,
      category: StandardComponentCategory | undefined,
      blankIsZero: boolean
    ): number => {
      let sum = 0;
      for (const kind of card.kinds) {
        const raw = kind.quantity === "" && blankIsZero ? "0" : kind.quantity;
        const value = Number(raw);
        if (raw === "" || !Number.isInteger(value) || value < 0 || value > 10000) {
          valid = false;
          continue;
        }
        sum += value;
        if (value === 0) continue;
        if (!category || !kind.name.trim()) {
          valid = false;
          continue;
        }
        if (kind.numberingMode === "span_member" && !(parsedQuantities.span_count > 0)) valid = false;
        groups.push({
          site_component_type: kind.name.trim(),
          site_name: kind.name.trim(),
          standard_component_category_id: category.id,
          structure_part: category.structure_part,
          numbering_mode: kind.numberingMode,
          quantity: value,
          quantity_key: key,
          number_prefix: kind.prefix,
          number_suffix: kind.suffix,
        });
      }
      if (sum > 10000) valid = false;
      return sum;
    };

    for (const key of template.quantity_inputs) {
      if (key === "span_count") continue;
      const card = cards[key] ?? defaultTemplateCard(key);
      const category = categories.find((item) => item.id === card.categoryId);
      parsedQuantities[key] = collect(key, card, category, false);
    }
    for (const category of extraCategories) {
      const card = cards[category.id] ?? defaultExtraCard(category);
      const sum = collect(category.id, card, category, true);
      if (sum > 0) parsedQuantities[category.id] = sum;
    }
    if (groups.length === 0) valid = false;
    return { parsedQuantities, groups, valid };
  }, [catalog, template, categories, extraCategories, cards, spanCount]);

  useEffect(() => {
    if (!derived || !template) {
      onPlanChange(null);
      return;
    }
    onPlanChange(
      derived.valid
        ? {
            standard_package_id: packageId,
            template_id: template.id,
            bridge_type_id: bridgeTypeId,
            span_count: derived.parsedQuantities.span_count ?? 0,
            input_quantities: derived.parsedQuantities,
            groups: derived.groups,
          }
        : null
    );
  }, [bridgeTypeId, derived, onPlanChange, packageId, template]);

  function resetForPackage(nextPackageId: string) {
    setPackageId(nextPackageId);
    setBridgeTypeId("");
    setSpanCount("");
    setCards({});
  }

  function resetForBridgeType(nextBridgeTypeId: string) {
    setBridgeTypeId(nextBridgeTypeId);
    setSpanCount("");
    setCards({});
  }

  function updateCard(key: string, fallback: () => CardDraft, mutate: (card: CardDraft) => CardDraft) {
    setCards((current) => ({ ...current, [key]: mutate(current[key] ?? fallback()) }));
  }

  function updateKind(key: string, fallback: () => CardDraft, index: number, patch: Partial<KindDraft>) {
    updateCard(key, fallback, (card) => ({
      ...card,
      kinds: card.kinds.map((kind, i) => (i === index ? { ...kind, ...patch } : kind)),
    }));
  }

  function addKind(key: string, fallback: () => CardDraft, quantity: string, numberingMode: NumberingMode) {
    updateCard(key, fallback, (card) => ({
      ...card,
      kinds: [...card.kinds, newKind("", numberingMode, quantity)],
    }));
  }

  function removeKind(key: string, fallback: () => CardDraft, index: number) {
    updateCard(key, fallback, (card) => ({
      ...card,
      kinds: card.kinds.filter((_, i) => i !== index),
    }));
  }

  function selectCategory(key: string, category: StandardComponentCategory | undefined) {
    updateCard(key, () => defaultTemplateCard(key), (card) => {
      const previous = categories.find((item) => item.id === card.categoryId);
      return {
        categoryId: category?.id ?? "",
        kinds: card.kinds.map((kind) =>
          kind.name.trim() === "" || (previous != null && kind.name === previous.name)
            ? { ...kind, name: category?.name ?? "" }
            : kind
        ),
      };
    });
  }

  function renderKindRow(
    labelBase: string,
    key: string,
    fallback: () => CardDraft,
    kind: KindDraft,
    index: number,
    removable: boolean
  ) {
    return (
      <div className="inventory-kind-row" key={index}>
        <label>
          构件名称
          <input
            aria-label={`${labelBase} 构件名称 ${index + 1}`}
            value={kind.name}
            onChange={(event) => updateKind(key, fallback, index, { name: event.target.value })}
          />
        </label>
        <label>
          数量
          <input
            aria-label={`${labelBase} 数量 ${index + 1}`}
            type="number"
            min={0}
            max={10000}
            step={1}
            value={kind.quantity}
            onChange={(event) => updateKind(key, fallback, index, { quantity: event.target.value })}
          />
        </label>
        <label>
          编号方式
          <select
            aria-label={`${labelBase} 编号方式 ${index + 1}`}
            value={kind.numberingMode}
            onChange={(event) => updateKind(key, fallback, index, { numberingMode: event.target.value as NumberingMode })}
          >
            <option value="sequential">连续（1#、2#…）</option>
            <option value="span_member">按跨（1-1#、1-2#…）</option>
          </select>
        </label>
        <details className="inventory-affix-details">
          <summary>编号前后缀</summary>
          <div className="inventory-affix-fields">
            <label>
              编号前缀
              <input
                aria-label={`${labelBase} 编号前缀 ${index + 1}`}
                value={kind.prefix}
                onChange={(event) => updateKind(key, fallback, index, { prefix: event.target.value })}
              />
            </label>
            <label>
              编号后缀
              <input
                aria-label={`${labelBase} 编号后缀 ${index + 1}`}
                value={kind.suffix}
                onChange={(event) => updateKind(key, fallback, index, { suffix: event.target.value })}
              />
            </label>
          </div>
        </details>
        {removable ? (
          <button
            type="button"
            className="inventory-kind-remove"
            aria-label={`${labelBase} 移除 ${index + 1}`}
            onClick={() => removeKind(key, fallback, index)}
          >
            移除
          </button>
        ) : null}
      </div>
    );
  }

  return (
    <section className="inventory-wizard" aria-labelledby="inventory-wizard-title">
      <div className="inventory-section-heading">
        <div>
          <p className="section-kicker">初始构件台账</p>
          <h3 id="inventory-wizard-title">按规范模板生成实际构件</h3>
        </div>
        <span className="inventory-status-badge">可稍后修改编号</span>
      </div>
      <p className="inventory-standard-notice">
        这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
      </p>
      {loading ? <p>正在加载规范模板…</p> : null}
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {!loading && !error && catalogs.length === 0 ? <p>当前没有可用的技术评定规范包。</p> : null}
      {catalogs.length > 0 ? (
        <div className="inventory-wizard-grid">
          <label>
            初始台账模板来源
            <select value={packageId} onChange={(event) => resetForPackage(event.target.value)}>
              <option value="">请选择规范</option>
              {catalogs.map((item) => (
                <option key={item.package.id} value={item.package.id}>
                  {item.package.standard_code} · {item.package.standard_name}
                </option>
              ))}
            </select>
          </label>
          <label>
            桥型
            <select
              value={bridgeTypeId}
              disabled={!catalog}
              onChange={(event) => resetForBridgeType(event.target.value)}
            >
              <option value="">请选择桥型</option>
              {catalog?.bridge_types.map((item) => (
                <option key={item.id} value={item.id}>{item.name}</option>
              ))}
            </select>
          </label>
        </div>
      ) : null}
      {bridgeTypeId && !template ? <p className="error-text">所选桥型没有可用的构件生成模板。</p> : null}
      {template ? (
        <div className="inventory-quantity-list">
          <h4>填写构件数量</h4>
          {template.quantity_inputs.includes("span_count") ? (
            <div className="inventory-quantity-card">
              <label>
                跨数
                <input
                  aria-label="跨数"
                  type="number"
                  min={0}
                  max={1000}
                  step={1}
                  value={spanCount}
                  onChange={(event) => setSpanCount(event.target.value)}
                />
              </label>
            </div>
          ) : null}
          {template.quantity_inputs
            .filter((key) => key !== "span_count")
            .map((key) => {
              const fallback = () => defaultTemplateCard(key);
              const card = cards[key] ?? fallback();
              const labelBase = quantityLabel(key);
              return (
                <div className="inventory-quantity-card" key={key}>
                  <div className="inventory-card-heading">
                    <strong>{labelBase}</strong>
                    <span className="inventory-card-total">合计 {kindTotal(card)}</span>
                  </div>
                  <label>
                    对应构件类别
                    <select
                      aria-label={`${labelBase} 对应构件类别`}
                      value={card.categoryId}
                      onChange={(event) => selectCategory(
                        key,
                        categories.find((item) => item.id === event.target.value)
                      )}
                    >
                      <option value="">请选择类别</option>
                      {categories.map((item) => <option key={item.id} value={item.id}>{item.name}</option>)}
                    </select>
                  </label>
                  {card.kinds.map((kind, index) =>
                    renderKindRow(labelBase, key, fallback, kind, index, card.kinds.length > 1))}
                  <button
                    type="button"
                    className="inventory-add-kind"
                    aria-label={`${labelBase} 添加一种构件`}
                    onClick={() => addKind(key, fallback, "", templateNumberingMode(key))}
                  >
                    ＋ 添加一种构件
                  </button>
                </div>
              );
            })}
          {extraCategories.length > 0 ? (
            <div className="inventory-extra-categories">
              <h4>其他部件（桥上没有的填 0）</h4>
              {extraCategories.map((category) => {
                const fallback = () => defaultExtraCard(category);
                const card = cards[category.id] ?? fallback();
                return (
                  <div className="inventory-quantity-card" key={category.id}>
                    <div className="inventory-card-heading">
                      <strong>{category.name}</strong>
                      <span className="inventory-card-total">合计 {kindTotal(card)}</span>
                    </div>
                    {card.kinds.map((kind, index) =>
                      renderKindRow(category.name, category.id, fallback, kind, index, card.kinds.length > 1))}
                    <button
                      type="button"
                      className="inventory-add-kind"
                      aria-label={`${category.name} 添加一种构件`}
                      onClick={() => addKind(category.id, fallback, "0", "sequential")}
                    >
                      ＋ 添加一种构件
                    </button>
                  </div>
                );
              })}
            </div>
          ) : null}
        </div>
      ) : null}
      {derived && derived.groups.length > 0 ? (
        <div className="inventory-number-preview">
          <h4>编号预览</h4>
          {previewInventoryNumbers(derived.groups, Number(spanCount) || 0).map((item) => (
            <p key={`${item.quantityKey}:${item.siteType}`}>
              <strong>{item.siteType}</strong>：共 {item.count} 个；{item.numbers.join("、")}
              {item.count > item.numbers.length ? "…" : ""}
            </p>
          ))}
        </div>
      ) : null}
    </section>
  );
}
