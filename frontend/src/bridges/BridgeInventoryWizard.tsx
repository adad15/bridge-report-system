import { useEffect, useMemo, useState } from "react";

import {
  componentInventoryErrorMessage,
  fetchPartCatalog,
  type CatalogPart,
  type GenerateComponentInventoryInput,
  type PartSelection,
} from "../api/componentInventoryApi";
import {
  fetchStandardCatalog,
  fetchStandardPackages,
  standardsErrorMessage,
  type StandardCatalog,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";
import { expandTemplate } from "./inventoryNumbering";
import { structurePartLabel, structurePartOrder } from "./structureParts";


function validSpanCount(raw: string): boolean {
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

export function BridgeInventoryWizard({
  onPlanChange,
}: {
  onPlanChange: (plan: GenerateComponentInventoryInput | null) => void;
}) {
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>([]);
  const [packageId, setPackageId] = useState("");
  const [bridgeTypeId, setBridgeTypeId] = useState("");
  const [spanCount, setSpanCount] = useState("");
  const [parts, setParts] = useState<CatalogPart[]>([]);
  const [enabled, setEnabled] = useState<Record<string, boolean>>({});
  const [names, setNames] = useState<Record<string, string>>({});
  const [counts, setCounts] = useState<Record<string, string[]>>({});
  // 逐实例复选记的是展开顺序里的下标，不是编号：改现场名或改跨数后编号会变，
  // 下标仍指向同一个位置（如"0#台左侧"），用户的取舍不会丢。
  const [excludedIndexes, setExcludedIndexes] = useState<Record<string, number[]>>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [partsError, setPartsError] = useState<string | null>(null);

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

  // 选定规范包 + 桥型后拉取该桥型可用部件目录。
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
  const partCounts = (part: CatalogPart) =>
    counts[part.part_key] ?? part.count_inputs.map(() => "");

  // 该部件按当前跨数/数量展开出的全部编号；跨数或数量不合法时为空。
  const partNumbers = (part: CatalogPart) => {
    if (!validSpanCount(spanCount)) return [];
    const parsed = parseCounts(partCounts(part));
    if (parsed === null) return [];
    return expandTemplate(
      part.number_template,
      partName(part).trim() || part.default_name,
      parsed,
      Number(spanCount)
    );
  };

  const derived = useMemo(() => {
    const spanValid = validSpanCount(spanCount);
    const span = spanValid ? Number(spanCount) : 0;
    const selections: PartSelection[] = [];
    let valid = spanValid;
    for (const part of parts) {
      if (!enabled[part.part_key]) continue;
      const name = partName(part).trim();
      const parsed = parseCounts(partCounts(part));
      if (!name || parsed === null) {
        valid = false;
        continue;
      }
      const selection: PartSelection = { part_key: part.part_key, site_name: name, counts: parsed };
      if (part.instance_selectable) {
        const numbers = partNumbers(part);
        const dropped = excludedIndexes[part.part_key] ?? [];
        // 下标转成编号再回传，后端按自己展开的结果校验，前后端不一致会明确报错。
        selection.excluded_numbers = dropped
          .map((index) => numbers[index]?.number)
          .filter((number): number is string => !!number);
        // 全部去掉等于这个部件不存在，不必生成。
        if (selection.excluded_numbers.length === numbers.length) continue;
      }
      selections.push(selection);
    }
    if (selections.length === 0) valid = false;
    return { selections, valid, span };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [parts, enabled, names, counts, spanCount, excludedIndexes]);

  useEffect(() => {
    onPlanChange(
      derived.valid
        ? {
            standard_package_id: packageId,
            bridge_type_id: bridgeTypeId,
            span_count: derived.span,
            part_selections: derived.selections,
          }
        : null
    );
  }, [derived, onPlanChange, packageId, bridgeTypeId]);

  function resetForPackage(nextPackageId: string) {
    setPackageId(nextPackageId);
    setBridgeTypeId("");
    setSpanCount("");
    setEnabled({});
    setNames({});
    setCounts({});
    setExcludedIndexes({});
  }

  function resetForBridgeType(nextBridgeTypeId: string) {
    setBridgeTypeId(nextBridgeTypeId);
    setSpanCount("");
    setEnabled({});
    setNames({});
    setCounts({});
    setExcludedIndexes({});
  }

  function toggleInstance(part: CatalogPart, index: number, on: boolean) {
    setExcludedIndexes((current) => {
      const dropped = current[part.part_key] ?? [];
      const next = on
        ? dropped.filter((item) => item !== index)
        : dropped.includes(index)
          ? dropped
          : [...dropped, index];
      return { ...current, [part.part_key]: next };
    });
  }

  function toggle(part: CatalogPart, on: boolean) {
    setEnabled((current) => ({ ...current, [part.part_key]: on }));
  }

  function setName(part: CatalogPart, value: string) {
    setNames((current) => ({ ...current, [part.part_key]: value }));
  }

  function setCount(part: CatalogPart, index: number, value: string) {
    setCounts((current) => {
      const existing = current[part.part_key] ?? part.count_inputs.map(() => "");
      const next = existing.slice();
      next[index] = value;
      return { ...current, [part.part_key]: next };
    });
  }

  // 三层结构：结构分部 → 部件类别（16 个规范类别）→ 部件（细分），均按目录顺序。
  const groups = useMemo(
    () =>
      structurePartOrder
        .map((key) => {
          const sectionParts = parts.filter((part) => part.structure_part === key);
          const order: string[] = [];
          const byCategory = new Map<string, { name: string; parts: CatalogPart[] }>();
          for (const part of sectionParts) {
            const categoryId = part.standard_component_category_id;
            let bucket = byCategory.get(categoryId);
            if (!bucket) {
              bucket = { name: part.standard_component_category_name, parts: [] };
              byCategory.set(categoryId, bucket);
              order.push(categoryId);
            }
            bucket.parts.push(part);
          }
          return { key, categories: order.map((id) => ({ id, ...byCategory.get(id)! })) };
        })
        .filter((group) => group.categories.length > 0),
    [parts]
  );

  // 展开出的位置逐个可勾选：真实桥常缺其中几处（如只有一侧有翼墙）。
  function renderInstances(part: CatalogPart) {
    const numbers = partNumbers(part);
    if (numbers.length === 0) return null;
    const dropped = excludedIndexes[part.part_key] ?? [];
    return (
      <fieldset className="inventory-instance-list">
        <legend>桥上实际有哪些（去掉没有的）</legend>
        {numbers.map((item, index) => (
          <label className="inventory-instance-option" key={item.number}>
            <input
              type="checkbox"
              aria-label={item.number}
              checked={!dropped.includes(index)}
              onChange={(event) => toggleInstance(part, index, event.target.checked)}
            />
            <span>{item.number}</span>
          </label>
        ))}
        <p className="inventory-part-preview">
          共 {numbers.length - dropped.length} 个
        </p>
      </fieldset>
    );
  }

  function renderPreview(part: CatalogPart) {
    const numbers = partNumbers(part);
    if (numbers.length === 0) return null;
    const shown = numbers.slice(0, 6).map((item) => item.number);
    return (
      <p className="inventory-part-preview">
        共 {numbers.length} 个：{shown.join("、")}
        {numbers.length > shown.length ? "…" : ""}
      </p>
    );
  }

  return (
    <section className="inventory-wizard" aria-labelledby="inventory-wizard-title">
      <div className="inventory-section-heading">
        <div>
          <p className="section-kicker">初始构件台账</p>
          <h3 id="inventory-wizard-title">按规范生成实际构件</h3>
        </div>
        <span className="inventory-status-badge">可稍后修改编号</span>
      </div>
      <p className="inventory-standard-notice">
        这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
      </p>
      {loading ? <p>正在加载规范…</p> : null}
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {!loading && !error && catalogs.length === 0 ? <p>当前没有可用的技术评定规范包。</p> : null}
      {catalogs.length > 0 ? (
        <div className="inventory-wizard-grid">
          <label>
            初始台账规范来源
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
      {partsError ? <p className="error-text" role="alert">{partsError}</p> : null}
      {bridgeTypeId ? (
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
      {bridgeTypeId && parts.length > 0 ? (
        <div className="inventory-part-list">
          <h4>勾选桥上有的部件并填数量</h4>
          {groups.map((group) => (
            <div className="inventory-part-group" key={group.key}>
              <h4 className="inventory-structure-heading">{structurePartLabel(group.key)}</h4>
              {group.categories.map((category) => (
                <div className="inventory-category-group" key={category.id}>
                  <h5 className="inventory-category-heading">{category.name}</h5>
                  {category.parts.map((part) => {
                    const on = !!enabled[part.part_key];
                    return (
                      <div className="inventory-part-card" key={part.part_key}>
                        <label className="inventory-part-enable">
                          <input
                            type="checkbox"
                            aria-label={`启用 ${part.default_name}`}
                            checked={on}
                            onChange={(event) => toggle(part, event.target.checked)}
                          />
                          <strong>{part.default_name}</strong>
                          {part.provisional ? (
                            <span className="inventory-provisional-badge">临时编号（待校准）</span>
                          ) : null}
                        </label>
                        {on ? (
                          <div className="inventory-part-body">
                            <label>
                              现场名称
                              <input
                                aria-label={`${part.default_name} 名称`}
                                value={partName(part)}
                                onChange={(event) => setName(part, event.target.value)}
                              />
                            </label>
                            {part.count_inputs.map((countInput, index) => (
                              <label key={countInput.key}>
                                {countInput.label}
                                <input
                                  aria-label={`${part.default_name} ${countInput.label}`}
                                  type="number"
                                  min={0}
                                  max={10000}
                                  step={1}
                                  value={partCounts(part)[index] ?? ""}
                                  onChange={(event) => setCount(part, index, event.target.value)}
                                />
                                {countInput.hint ? (
                                  <span className="inventory-count-hint">{countInput.hint}</span>
                                ) : null}
                              </label>
                            ))}
                            {part.instance_selectable ? renderInstances(part) : renderPreview(part)}
                          </div>
                        ) : null}
                      </div>
                    );
                  })}
                </div>
              ))}
            </div>
          ))}
        </div>
      ) : null}
    </section>
  );
}
