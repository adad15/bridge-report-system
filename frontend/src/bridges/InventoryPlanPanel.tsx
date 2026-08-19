import { useCallback, useEffect, useState } from "react";

import type { GenerateComponentInventoryInput } from "../api/componentInventoryApi";
import {
  BridgeInventoryWizard,
  emptyInventorySelection,
  emptyInventorySummary,
  validSpanCount,
  type InventorySelection,
  type InventorySummary,
} from "./BridgeInventoryWizard";
import { useSelectedPackage, useStandardCatalogs } from "./useStandardCatalogs";

// 给"这座桥还没有台账"那条路径用：向导本身只管勾部件，生成参数由这层自带。
// 添加桥梁向导里这三个参数在第一步，不走这里。
export function InventoryPlanPanel({
  onPlanChange,
}: {
  onPlanChange: (plan: GenerateComponentInventoryInput | null) => void;
}) {
  const { catalogs, loading, error } = useStandardCatalogs();
  const [packageId, setPackageId] = useSelectedPackage(catalogs);
  const [bridgeTypeId, setBridgeTypeId] = useState("");
  const [spanCount, setSpanCount] = useState("");
  const [selection, setSelection] = useState<InventorySelection>(emptyInventorySelection);
  const [summary, setSummary] = useState<InventorySummary>(emptyInventorySummary);

  const catalog = catalogs.find((item) => item.package.id === packageId) ?? null;
  const ready = packageId !== "" && bridgeTypeId !== "" && validSpanCount(spanCount);

  const handlePlanChange = useCallback(
    (plan: GenerateComponentInventoryInput | null, next: InventorySummary) => {
      onPlanChange(plan);
      setSummary(next);
    },
    [onPlanChange]
  );

  // 参数不全时向导不渲染，也就没人报告计划——得自己把上一轮的计划撤掉。
  useEffect(() => {
    if (!ready) {
      onPlanChange(null);
      setSummary(emptyInventorySummary);
    }
  }, [ready, onPlanChange]);

  // 换规范或换桥型会换掉整份部件目录，已勾的选择接不上，只能清空。
  function discardSelection(what: string): boolean {
    const chosen = Object.values(selection.enabled).filter(Boolean).length;
    if (chosen > 0 && !window.confirm(`${what}会清空已经勾选的 ${chosen} 个部件，确定继续吗？`)) return false;
    setSelection(emptyInventorySelection);
    return true;
  }

  return (
    <div className="inventory-plan-panel">
      <div className="inventory-plan-fields">
        {catalogs.length > 1 ? (
          <label>
            <span className="field-label">初始台账规范来源</span>
            <select
              value={packageId}
              onChange={(event) => {
                if (!discardSelection("换规范")) return;
                setPackageId(event.target.value);
                setBridgeTypeId("");
              }}
            >
              <option value="">请选择规范</option>
              {catalogs.map((item) => (
                <option key={item.package.id} value={item.package.id}>
                  {item.package.standard_code} · {item.package.standard_name}
                </option>
              ))}
            </select>
          </label>
        ) : null}
        <label>
          <span className="field-label">桥型</span>
          <select
            value={bridgeTypeId}
            disabled={!catalog}
            onChange={(event) => {
              if (!discardSelection("换桥型")) return;
              setBridgeTypeId(event.target.value);
            }}
          >
            <option value="">请选择桥型</option>
            {catalog?.bridge_types.map((item) => (
              <option key={item.id} value={item.id}>{item.name}</option>
            ))}
          </select>
        </label>
        <label>
          <span className="field-label">跨数</span>
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

      {loading ? <p className="wizard-standard-note">正在加载规范…</p> : null}
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {!loading && !error && catalogs.length === 0 ? (
        <p className="wizard-standard-note">当前没有可用的技术评定规范包。</p>
      ) : null}
      {catalog ? (
        <p className="wizard-standard-note">
          初始台账规范 <b>{catalog.package.standard_code} · {catalog.package.standard_name}</b>
          ——这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
        </p>
      ) : null}

      {ready ? (
        <>
          <BridgeInventoryWizard
            packageId={packageId}
            bridgeTypeId={bridgeTypeId}
            spanCount={Number(spanCount)}
            selection={selection}
            onSelectionChange={setSelection}
            onPlanChange={handlePlanChange}
          />
          <p className="wizard-status">
            {summary.missing.length > 0
              ? `还有 ${summary.missing.length} 处未填完：${summary.missing.join("、")}`
              : summary.partCount === 0
                ? "勾选桥上有的部件。"
                : `将生成 ${summary.total} 个构件 · ${summary.partCount} 个部件`}
          </p>
        </>
      ) : null}
    </div>
  );
}
