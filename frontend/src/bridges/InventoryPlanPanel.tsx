import { Alert, Col, Flex, Form, InputNumber, Row, Select, Typography } from "antd";
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
    <Flex vertical gap={12}>
      <Form layout="vertical">
        <Row gutter={14}>
          {catalogs.length > 1 ? (
            <Col xs={24} md={8}>
              <Form.Item label="初始台账规范来源" htmlFor="inventory-plan-package">
                <Select
                  id="inventory-plan-package"
                  placeholder="请选择规范"
                  value={packageId || undefined}
                  onChange={(next) => {
                    if (!discardSelection("换规范")) return;
                    setPackageId(next);
                    setBridgeTypeId("");
                  }}
                  options={catalogs.map((item) => ({
                    value: item.package.id,
                    label: `${item.package.standard_code} · ${item.package.standard_name}`,
                  }))}
                />
              </Form.Item>
            </Col>
          ) : null}
          <Col xs={24} md={8}>
            <Form.Item label="桥型" htmlFor="inventory-plan-bridge-type">
              <Select
                id="inventory-plan-bridge-type"
                placeholder="请选择桥型"
                value={bridgeTypeId || undefined}
                disabled={!catalog}
                onChange={(next) => {
                  if (!discardSelection("换桥型")) return;
                  setBridgeTypeId(next);
                }}
                options={catalog?.bridge_types.map((item) => ({ value: item.id, label: item.name }))}
              />
            </Form.Item>
          </Col>
          <Col xs={24} md={8}>
            <Form.Item label="跨数" htmlFor="inventory-plan-spans">
              {/* 不设 min/max：越界时向导不出现，不在失焦时悄悄改成边界值。 */}
              <InputNumber
                id="inventory-plan-spans"
                style={{ width: "100%" }}
                precision={0}
                value={spanCount === "" ? null : Number(spanCount)}
                onChange={(value) => setSpanCount(value === null ? "" : String(value))}
              />
            </Form.Item>
          </Col>
        </Row>
      </Form>

      {loading ? <Typography.Text type="secondary">正在加载规范…</Typography.Text> : null}
      {error ? <Alert type="error" showIcon title={error} /> : null}
      {!loading && !error && catalogs.length === 0 ? (
        <Typography.Text type="secondary">当前没有可用的技术评定规范包。</Typography.Text>
      ) : null}
      {catalog ? (
        <Typography.Text type="secondary">
          初始台账规范 <Typography.Text strong>{catalog.package.standard_code} · {catalog.package.standard_name}</Typography.Text>
          ——这里选择的规范只用于生成初始构件台账，不会绑定或限制以后检测项目采用的评分规范。
        </Typography.Text>
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
          <Typography.Text type="secondary">
            {summary.missing.length > 0
              ? `还有 ${summary.missing.length} 处未填完：${summary.missing.join("、")}`
              : summary.partCount === 0
                ? "勾选桥上有的部件。"
                : `将生成 ${summary.total} 个构件 · ${summary.partCount} 个部件`}
          </Typography.Text>
        </>
      ) : null}
    </Flex>
  );
}
