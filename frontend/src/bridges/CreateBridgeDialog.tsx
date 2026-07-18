import { useState, type FormEvent } from "react";

import { bridgeAdministrationError, createBridge, type BridgeAdminSummary } from "../api/bridgeAdministrationApi";
import {
  componentInventoryErrorMessage,
  generateComponentInventory,
  type GenerateComponentInventoryInput,
} from "../api/componentInventoryApi";
import { backendBaseUrl } from "../config";
import { BridgeInventoryWizard } from "./BridgeInventoryWizard";

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
  const [plan, setPlan] = useState<GenerateComponentInventoryInput | null>(null);
  const [createdBridge, setCreatedBridge] = useState<BridgeAdminSummary | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (step === "base") {
      if (!name.trim()) return;
      setPlan(null);
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

  return (
    <div className="dialog-backdrop" role="presentation">
      <form
        className="workspace-dialog create-bridge-dialog create-bridge-wizard-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="create-bridge-title"
        onSubmit={(event) => void submit(event)}
      >
        <div className="inventory-section-heading">
          <div>
            <p className="section-kicker">步骤 {step === "base" ? "1 / 2" : "2 / 2"}</p>
            <h2 id="create-bridge-title">添加桥梁</h2>
          </div>
          <span className="inventory-status-badge">{step === "base" ? "基本信息" : "构件台账"}</span>
        </div>

        {step === "base" ? (
          <>
            <label>
              桥梁名称
              <input required maxLength={200} value={name} onChange={(event) => setName(event.target.value)} />
            </label>
            <div className="bridge-form-grid">
              <label>路线编号<input maxLength={100} value={routeNumber} onChange={(event) => setRouteNumber(event.target.value)} /></label>
              <label>路线名称<input maxLength={200} value={routeName} onChange={(event) => setRouteName(event.target.value)} /></label>
              <label>行政区划<input maxLength={200} value={region} onChange={(event) => setRegion(event.target.value)} /></label>
              <label>桩号<input maxLength={100} value={station} onChange={(event) => setStation(event.target.value)} /></label>
              <label>
                状态
                <select value={status} onChange={(event) => setStatus(event.target.value)}>
                  <option>在用</option><option>停用</option><option>拆除</option>
                </select>
              </label>
            </div>
          </>
        ) : (
          <BridgeInventoryWizard onPlanChange={setPlan} />
        )}

        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions">
          <button type="button" disabled={busy} onClick={onClose}>取消</button>
          {step === "inventory" && !createdBridge ? (
            <button type="button" disabled={busy} onClick={() => { setPlan(null); setStep("base"); }}>上一步</button>
          ) : null}
          <button type="submit" disabled={busy || (step === "base" ? !name.trim() : !plan)}>
            {step === "base" ? "下一步：构件台账" : busy ? "正在创建…" : createdBridge ? "重试生成台账" : "创建桥梁并生成台账"}
          </button>
        </div>
      </form>
    </div>
  );
}
