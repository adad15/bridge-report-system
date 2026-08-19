import { useCallback, useState, type FormEvent } from "react";

import { bridgeAdministrationError, createBridge, type BridgeAdminSummary } from "../api/bridgeAdministrationApi";
import {
  componentInventoryErrorMessage,
  generateComponentInventory,
  type GenerateComponentInventoryInput,
} from "../api/componentInventoryApi";
import { backendBaseUrl } from "../config";
import {
  BridgeInventoryWizard,
  emptyInventorySelection,
  emptyInventorySummary,
  validSpanCount,
  type InventorySelection,
  type InventorySummary,
} from "./BridgeInventoryWizard";
import { useSelectedPackage, useStandardCatalogs } from "./useStandardCatalogs";

export function CreateBridgeDialog({
  onClose,
  onCreated,
}: {
  onClose: () => void;
  onCreated: (bridge: BridgeAdminSummary) => void;
}) {
  const [step, setStep] = useState<"base" | "inventory">("base");
  const [name, setName] = useState("");
  const [routeNumber, setRouteNumber] = useState("");
  const [routeName, setRouteName] = useState("");
  const [region, setRegion] = useState("");
  const [station, setStation] = useState("");
  const [status, setStatus] = useState("在用");
  const [scale, setScale] = useState("");

  // 桥型与跨数是这座桥的物理事实，和桥梁规模同属第一步；它们只是恰好也被台账生成用到。
  const { catalogs, loading: standardsLoading, error: standardsError } = useStandardCatalogs();
  const [packageId, setPackageId] = useSelectedPackage(catalogs);
  const [bridgeTypeId, setBridgeTypeId] = useState("");
  const [spanCount, setSpanCount] = useState("");

  const [selection, setSelection] = useState<InventorySelection>(emptyInventorySelection);
  const [plan, setPlan] = useState<GenerateComponentInventoryInput | null>(null);
  const [summary, setSummary] = useState<InventorySummary>(emptyInventorySummary);

  const [createdBridge, setCreatedBridge] = useState<BridgeAdminSummary | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // 必须稳定：向导在 effect 里回调，句柄每次渲染都换会把它变成无限循环。
  const handlePlanChange = useCallback(
    (next: GenerateComponentInventoryInput | null, nextSummary: InventorySummary) => {
      setPlan(next);
      setSummary(nextSummary);
    },
    []
  );

  const catalog = catalogs.find((item) => item.package.id === packageId) ?? null;
  const chosenParts = Object.values(selection.enabled).filter(Boolean).length;

  // 换规范或换桥型会换掉整份部件目录，已勾的选择接不上，只能清空——所以先问一句。
  function discardSelection(what: string): boolean {
    if (chosenParts > 0 && !window.confirm(`${what}会清空已经勾选的 ${chosenParts} 个部件，确定继续吗？`))
      return false;
    setSelection(emptyInventorySelection);
    setPlan(null);
    setSummary(emptyInventorySummary);
    return true;
  }

  function changePackage(next: string) {
    if (next === packageId || !discardSelection("换规范")) return;
    setPackageId(next);
    setBridgeTypeId("");
  }

  function changeBridgeType(next: string) {
    if (next === bridgeTypeId || !discardSelection("换桥型")) return;
    setBridgeTypeId(next);
  }

  const baseReady =
    name.trim() !== "" && packageId !== "" && bridgeTypeId !== "" && validSpanCount(spanCount);

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (step === "base") {
      if (!baseReady) return;
      setStep("inventory");
      return;
    }
    if (!plan) return;
    setBusy(true);
    setError(null);
    let bridge = createdBridge;
    try {
      if (!bridge) {
        bridge = await createBridge(backendBaseUrl, {
          bridge_name: name.trim(),
          route_number: routeNumber.trim(),
          route_name: routeName.trim(),
          administrative_region: region.trim(),
          station_mark: station.trim(),
          status,
          bridge_scale: scale || undefined,
        });
        setCreatedBridge(bridge);
      }
      await generateComponentInventory(backendBaseUrl, bridge.id, plan);
      onCreated(bridge);
    } catch (caught) {
      setError(
        bridge
          ? `桥梁已经创建，但初始构件台账尚未生成：${componentInventoryErrorMessage(caught)} 可在本窗口重试。`
          : bridgeAdministrationError(caught)
      );
    } finally {
      setBusy(false);
    }
  }

  function statusText(): string {
    if (step === "base") return "下一步按规范生成初始构件台账，编号可稍后修改。";
    if (summary.missing.length > 0)
      return `还有 ${summary.missing.length} 处未填完：${summary.missing.join("、")}`;
    if (summary.partCount === 0) return "勾选桥上有的部件。";
    return `将生成 ${summary.total} 个构件 · ${summary.partCount} 个部件`;
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form
        className="workspace-dialog create-bridge-wizard-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="create-bridge-title"
        onSubmit={(event) => void submit(event)}
      >
        <div className="wizard-head">
          <h2 id="create-bridge-title">添加桥梁</h2>
          <ol className="wizard-steps">
            <li className={step === "base" ? "is-current" : ""}>
              <span className="wizard-step-dot">1</span>基本信息
            </li>
            <li className={step === "inventory" ? "is-current" : ""}>
              <span className="wizard-step-dot">2</span>构件台账
            </li>
          </ol>
        </div>

        <div className="wizard-body">
          {step === "base" ? (
            <>
              <div className="bridge-form-grid">
                {/* 有多个规范包时才出现，且必须排在桥型前面——桥型的可选项由它决定。 */}
                {catalogs.length > 1 ? (
                  <label className="span-2">
                    <span className="field-label">初始台账规范来源</span>
                    <select value={packageId} onChange={(event) => changePackage(event.target.value)}>
                      <option value="">请选择规范</option>
                      {catalogs.map((item) => (
                        <option key={item.package.id} value={item.package.id}>
                          {item.package.standard_code} · {item.package.standard_name} · v{item.package.package_version}
                        </option>
                      ))}
                    </select>
                  </label>
                ) : null}
                <label className="is-required">
                  <span className="field-label">桥梁名称</span>
                  <input required maxLength={200} value={name} onChange={(event) => setName(event.target.value)} />
                </label>
                <label>
                  <span className="field-label">桥梁规模</span>
                  <select aria-label="桥梁规模" value={scale} onChange={(event) => setScale(event.target.value)}>
                    <option value="">未填写</option>
                    <option value="大桥">大桥</option>
                    <option value="中桥">中桥</option>
                    <option value="小桥">小桥</option>
                  </select>
                </label>
                <label>
                  <span className="field-label">路线编号</span>
                  <input maxLength={100} value={routeNumber} onChange={(event) => setRouteNumber(event.target.value)} />
                </label>
                <label>
                  <span className="field-label">路线名称</span>
                  <input maxLength={200} value={routeName} onChange={(event) => setRouteName(event.target.value)} />
                </label>
                <label>
                  <span className="field-label">行政区划</span>
                  <input maxLength={200} value={region} onChange={(event) => setRegion(event.target.value)} />
                </label>
                <label>
                  <span className="field-label">桩号</span>
                  <input maxLength={100} value={station} onChange={(event) => setStation(event.target.value)} />
                </label>
                <label className="is-required">
                  <span className="field-label">桥型</span>
                  <select
                    value={bridgeTypeId}
                    disabled={!catalog}
                    onChange={(event) => changeBridgeType(event.target.value)}
                  >
                    <option value="">请选择桥型</option>
                    {catalog?.bridge_types.map((item) => (
                      <option key={item.id} value={item.id}>{item.name}</option>
                    ))}
                  </select>
                </label>
                <label className="is-required">
                  <span className="field-label">跨数</span>
                  <input
                    type="number"
                    min={0}
                    max={1000}
                    step={1}
                    value={spanCount}
                    onChange={(event) => setSpanCount(event.target.value)}
                  />
                </label>
                <label>
                  <span className="field-label">状态</span>
                  <select value={status} onChange={(event) => setStatus(event.target.value)}>
                    <option>在用</option><option>停用</option><option>拆除</option>
                  </select>
                </label>
              </div>

              {standardsLoading ? <p className="wizard-standard-note">正在加载规范…</p> : null}
              {standardsError ? <p className="error-text" role="alert">{standardsError}</p> : null}
              {!standardsLoading && !standardsError && catalogs.length === 0 ? (
                <p className="wizard-standard-note">当前没有可用的技术评定规范包。</p>
              ) : null}
              {catalog ? (
                <p className="wizard-standard-note">
                  初始台账规范 <b>{catalog.package.standard_code} · {catalog.package.standard_name}</b>
                  ——这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
                </p>
              ) : null}
            </>
          ) : (
            <BridgeInventoryWizard
              packageId={packageId}
              bridgeTypeId={bridgeTypeId}
              spanCount={Number(spanCount)}
              selection={selection}
              onSelectionChange={setSelection}
              onPlanChange={handlePlanChange}
            />
          )}
        </div>

        <div className="wizard-foot">
          {error ? <p className="error-text" role="alert">{error}</p> : null}
          <div className="wizard-foot-row">
            <p className="wizard-status">{statusText()}</p>
            <div className="dialog-actions">
              <button type="button" disabled={busy} onClick={onClose}>取消</button>
              {step === "inventory" && !createdBridge ? (
                <button type="button" disabled={busy} onClick={() => setStep("base")}>上一步</button>
              ) : null}
              <button
                type="submit"
                className="primary-button"
                disabled={busy || (step === "base" ? !baseReady : !plan)}
              >
                {step === "base" ? "下一步：构件台账" : busy ? "正在创建…" : createdBridge ? "重试生成台账" : "创建桥梁并生成台账"}
              </button>
            </div>
          </div>
        </div>
      </form>
    </div>
  );
}
