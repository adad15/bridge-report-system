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

interface GroupDraft {
  categoryId: string;
  siteName: string;
  siteType: string;
  numberingMode: NumberingMode;
  prefix: string;
  suffix: string;
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

function defaultGroup(key: string): GroupDraft {
  return {
    categoryId: "",
    siteName: "",
    siteType: "",
    numberingMode: key.includes("per_span") ? "span_member" : "sequential",
    prefix: "",
    suffix: "#",
  };
}

export function BridgeInventoryWizard({
  onPlanChange,
}: {
  onPlanChange: (plan: GenerateComponentInventoryInput | null) => void;
}) {
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>([]);
  const [packageId, setPackageId] = useState("");
  const [bridgeTypeId, setBridgeTypeId] = useState("");
  const [quantities, setQuantities] = useState<Record<string, string>>({});
  const [groups, setGroups] = useState<Record<string, GroupDraft>>({});
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

  useEffect(() => {
    if (!template || !catalog) {
      onPlanChange(null);
      return;
    }
    const parsedQuantities: Record<string, number> = {};
    let valid = true;
    for (const key of template.quantity_inputs) {
      const raw = quantities[key] ?? "";
      const value = Number(raw);
      if (raw === "" || !Number.isInteger(value) || value < 0 || value > 10000) valid = false;
      parsedQuantities[key] = value;
    }
    const generationGroups: InventoryGenerationGroup[] = [];
    for (const key of template.quantity_inputs) {
      if (key === "span_count" || !(parsedQuantities[key] > 0)) continue;
      const draft = groups[key];
      const category = categories.find((item) => item.id === draft?.categoryId);
      if (!draft || !category || !draft.siteName.trim() || !draft.siteType.trim()) {
        valid = false;
        continue;
      }
      if (draft.numberingMode === "span_member" && !(parsedQuantities.span_count > 0)) valid = false;
      generationGroups.push({
        site_component_type: draft.siteType.trim(),
        site_name: draft.siteName.trim(),
        standard_component_category_id: category.id,
        structure_part: category.structure_part,
        numbering_mode: draft.numberingMode,
        quantity: parsedQuantities[key],
        quantity_key: key,
        number_prefix: draft.prefix,
        number_suffix: draft.suffix,
      });
    }
    if (generationGroups.length === 0) valid = false;
    onPlanChange(
      valid
        ? {
            standard_package_id: packageId,
            template_id: template.id,
            bridge_type_id: bridgeTypeId,
            span_count: parsedQuantities.span_count ?? 0,
            input_quantities: parsedQuantities,
            groups: generationGroups,
          }
        : null
    );
  }, [bridgeTypeId, catalog, categories, groups, onPlanChange, packageId, quantities, template]);

  function resetForPackage(nextPackageId: string) {
    setPackageId(nextPackageId);
    setBridgeTypeId("");
    setQuantities({});
    setGroups({});
  }

  function resetForBridgeType(nextBridgeTypeId: string) {
    setBridgeTypeId(nextBridgeTypeId);
    setQuantities({});
    setGroups({});
  }

  function setQuantity(key: string, value: string) {
    setQuantities((current) => ({ ...current, [key]: value }));
    if (key !== "span_count" && Number(value) > 0) {
      setGroups((current) => ({ ...current, [key]: current[key] ?? defaultGroup(key) }));
    }
  }

  function updateGroup(key: string, update: Partial<GroupDraft>) {
    setGroups((current) => ({ ...current, [key]: { ...(current[key] ?? defaultGroup(key)), ...update } }));
  }

  function selectCategory(key: string, category: StandardComponentCategory | undefined) {
    updateGroup(
      key,
      category
        ? { categoryId: category.id, siteName: category.name, siteType: category.name }
        : { categoryId: "", siteName: "", siteType: "" }
    );
  }

  const readyGroups = template
    ? template.quantity_inputs
        .filter((key) => key !== "span_count" && Number(quantities[key]) > 0)
        .flatMap((key) => {
          const draft = groups[key];
          const category = categories.find((item) => item.id === draft?.categoryId);
          if (!draft || !category) return [];
          return [{
            site_component_type: draft.siteType,
            site_name: draft.siteName,
            standard_component_category_id: category.id,
            structure_part: category.structure_part,
            numbering_mode: draft.numberingMode,
            quantity: Number(quantities[key]),
            quantity_key: key,
            number_prefix: draft.prefix,
            number_suffix: draft.suffix,
          } satisfies InventoryGenerationGroup];
        })
    : [];
  const preview = previewInventoryNumbers(readyGroups, Number(quantities.span_count) || 0);

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
          {template.quantity_inputs.map((key) => {
            const value = quantities[key] ?? "";
            const positive = key !== "span_count" && Number(value) > 0;
            const draft = groups[key] ?? defaultGroup(key);
            return (
              <div className="inventory-quantity-card" key={key}>
                <label>
                  {quantityLabel(key)}
                  <input
                    aria-label={quantityLabel(key)}
                    type="number"
                    min={0}
                    max={key === "span_count" ? 1000 : 10000}
                    step={1}
                    value={value}
                    onChange={(event) => setQuantity(key, event.target.value)}
                  />
                </label>
                {positive ? (
                  <div className="inventory-group-fields">
                    <label>
                      对应构件类别
                      <select
                        value={draft.categoryId}
                        onChange={(event) => selectCategory(
                          key,
                          categories.find((item) => item.id === event.target.value)
                        )}
                      >
                        <option value="">请选择类别</option>
                        {categories.map((item) => <option key={item.id} value={item.id}>{item.name}</option>)}
                      </select>
                    </label>
                    <label>
                      现场构件名称
                      <input value={draft.siteName} onChange={(event) => updateGroup(key, { siteName: event.target.value })} />
                    </label>
                    <label>
                      构件类型
                      <input value={draft.siteType} onChange={(event) => updateGroup(key, { siteType: event.target.value })} />
                    </label>
                    <label>
                      编号方式
                      <select value={draft.numberingMode} onChange={(event) => updateGroup(key, { numberingMode: event.target.value as NumberingMode })}>
                        <option value="sequential">连续编号（1#、2#…）</option>
                        <option value="span_member">按跨编号（1-1#、1-2#…）</option>
                      </select>
                    </label>
                    <label>
                      编号前缀
                      <input value={draft.prefix} onChange={(event) => updateGroup(key, { prefix: event.target.value })} />
                    </label>
                    <label>
                      编号后缀
                      <input value={draft.suffix} onChange={(event) => updateGroup(key, { suffix: event.target.value })} />
                    </label>
                  </div>
                ) : null}
              </div>
            );
          })}
        </div>
      ) : null}
      {preview.length > 0 ? (
        <div className="inventory-number-preview">
          <h4>编号预览</h4>
          {preview.map((item) => (
            <p key={item.quantityKey}>
              <strong>{item.siteType}</strong>：共 {item.count} 个；{item.numbers.join("、")}
              {item.count > item.numbers.length ? "…" : ""}
            </p>
          ))}
        </div>
      ) : null}
    </section>
  );
}
