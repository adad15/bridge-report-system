import {
  Alert,
  Button,
  Col,
  Flex,
  Form,
  Input,
  InputNumber,
  Modal,
  Row,
  Select,
  Steps,
  Typography,
} from "antd";
import { useCallback, useState } from "react";

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

  async function submit() {
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

  const primaryText = step === "base"
    ? "下一步：构件台账"
    : createdBridge ? "重试生成台账" : "创建桥梁并生成台账";

  return (
    <Modal
      open
      centered
      width={980}
      mask={{ closable: false }}
      // 右上角的叉会和标题栏右侧的步骤条挤在一起；底部已有「取消」，Esc 也能关。
      closable={false}
      onCancel={onClose}
      title={
        <Flex align="center" justify="space-between" gap={16} wrap>
          添加桥梁
          <Steps
            size="small"
            current={step === "base" ? 0 : 1}
            items={[{ title: "基本信息" }, { title: "构件台账" }]}
            style={{ width: 320 }}
          />
        </Flex>
      }
      styles={{ body: { maxHeight: "calc(100vh - 240px)", overflowY: "auto", overflowX: "hidden" } }}
      footer={
        <Flex vertical gap={8}>
          {error ? <Alert type="error" showIcon title={error} /> : null}
          <Flex align="center" justify="space-between" gap={14}>
            <Typography.Text type="secondary">{statusText()}</Typography.Text>
            <Flex gap={8}>
              <Button disabled={busy} onClick={onClose}>取消</Button>
              {step === "inventory" && !createdBridge ? (
                <Button disabled={busy} onClick={() => setStep("base")}>上一步</Button>
              ) : null}
              <Button
                type="primary"
                loading={busy}
                disabled={step === "base" ? !baseReady : !plan}
                onClick={() => void submit()}
              >
                {primaryText}
              </Button>
            </Flex>
          </Flex>
        </Flex>
      }
    >
      {step === "base" ? (
        <Form layout="vertical" onFinish={() => void submit()}>
          <Row gutter={14}>
            {/* 有多个规范包时才出现，且必须排在桥型前面——桥型的可选项由它决定。 */}
            {catalogs.length > 1 ? (
              <Col span={16}>
                <Form.Item label="初始台账规范来源" htmlFor="create-bridge-package">
                  <Select
                    id="create-bridge-package"
                    placeholder="请选择规范"
                    value={packageId || undefined}
                    onChange={changePackage}
                    options={catalogs.map((item) => ({
                      value: item.package.id,
                      label: `${item.package.standard_code} · ${item.package.standard_name} · v${item.package.package_version}`,
                    }))}
                  />
                </Form.Item>
              </Col>
            ) : null}
            <Col span={8}>
              <Form.Item label="桥梁名称" htmlFor="create-bridge-name" required>
                <Input id="create-bridge-name" maxLength={200} value={name} onChange={(event) => setName(event.target.value)} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="桥梁规模" htmlFor="create-bridge-scale">
                <Select
                  id="create-bridge-scale"
                  placeholder="未填写"
                  allowClear
                  value={scale || undefined}
                  onChange={(value) => setScale(value ?? "")}
                  options={["大桥", "中桥", "小桥"].map((value) => ({ value, label: value }))}
                />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="路线编号" htmlFor="create-bridge-route-number">
                <Input id="create-bridge-route-number" maxLength={100} value={routeNumber} onChange={(event) => setRouteNumber(event.target.value)} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="路线名称" htmlFor="create-bridge-route-name">
                <Input id="create-bridge-route-name" maxLength={200} value={routeName} onChange={(event) => setRouteName(event.target.value)} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="行政区划" htmlFor="create-bridge-region">
                <Input id="create-bridge-region" maxLength={200} value={region} onChange={(event) => setRegion(event.target.value)} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="桩号" htmlFor="create-bridge-station">
                <Input id="create-bridge-station" maxLength={100} value={station} onChange={(event) => setStation(event.target.value)} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="桥型" htmlFor="create-bridge-type" required>
                <Select
                  id="create-bridge-type"
                  placeholder="请选择桥型"
                  value={bridgeTypeId || undefined}
                  disabled={!catalog}
                  onChange={changeBridgeType}
                  options={catalog?.bridge_types.map((item) => ({ value: item.id, label: item.name }))}
                />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="跨数" htmlFor="create-bridge-spans" required>
                {/* 不设 min/max：越界时由「下一步」保持禁用，不在失焦时悄悄改成边界值。 */}
                <InputNumber
                  id="create-bridge-spans"
                  style={{ width: "100%" }}
                  precision={0}
                  value={spanCount === "" ? null : Number(spanCount)}
                  onChange={(value) => setSpanCount(value === null ? "" : String(value))}
                />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item label="状态" htmlFor="create-bridge-status">
                <Select
                  id="create-bridge-status"
                  value={status}
                  onChange={setStatus}
                  options={["在用", "停用", "拆除"].map((value) => ({ value, label: value }))}
                />
              </Form.Item>
            </Col>
          </Row>

          {standardsLoading ? <Typography.Text type="secondary">正在加载规范…</Typography.Text> : null}
          {standardsError ? <Alert type="error" showIcon title={standardsError} /> : null}
          {!standardsLoading && !standardsError && catalogs.length === 0 ? (
            <Typography.Text type="secondary">当前没有可用的技术评定规范包。</Typography.Text>
          ) : null}
          {catalog ? (
            <Typography.Text type="secondary">
              初始台账规范 <Typography.Text strong>{catalog.package.standard_code} · {catalog.package.standard_name}</Typography.Text>
              ——这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
            </Typography.Text>
          ) : null}
        </Form>
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
    </Modal>
  );
}
