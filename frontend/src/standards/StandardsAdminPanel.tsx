import { Alert, Button, Card, Flex, Modal, Tag, Typography } from "antd";
import { useCallback, useEffect, useState } from "react";

import {
  fetchStandardPackages,
  setStandardPackageEnabled,
  standardsErrorMessage,
  type StandardPackageSummary,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";

interface Props {
  onClose: () => void;
}

const familyName = (family: StandardPackageSummary["family"]) =>
  family === "technical_condition" ? "公路桥梁技术状况评定标准" : "公路桥涵养护规范";

export function StandardsAdminPanel({ onClose }: Props) {
  const [packages, setPackages] = useState<StandardPackageSummary[] | null>(null);
  const [busyId, setBusyId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    setError(null);
    fetchStandardPackages(backendBaseUrl)
      .then(setPackages)
      .catch((caught) => setError(standardsErrorMessage(caught)));
  }, []);

  useEffect(load, [load]);

  async function toggle(item: StandardPackageSummary) {
    setBusyId(item.id);
    setError(null);
    try {
      const updated = await setStandardPackageEnabled(backendBaseUrl, item.id, !item.is_enabled);
      setPackages((current) => current?.map((value) => value.id === updated.id ? updated : value) ?? null);
    } catch (caught) {
      setError(standardsErrorMessage(caught));
    } finally {
      setBusyId(null);
    }
  }

  const statusTag = (item: StandardPackageSummary) => {
    if (item.sync_status !== "正常") return <Tag color="error">故障</Tag>;
    return item.is_enabled ? <Tag color="success">已启用</Tag> : <Tag>已停用</Tag>;
  };

  return (
    <Modal
      open
      centered
      width={640}
      title="规范管理"
      closable={{ "aria-label": "关闭规范管理", disabled: busyId !== null }}
      onCancel={onClose}
      styles={{ body: { maxHeight: "calc(100vh - 220px)", overflowY: "auto", overflowX: "hidden" } }}
      footer={<Button onClick={onClose} disabled={busyId !== null}>关闭</Button>}
    >
      <Flex vertical gap={12}>
        <Typography.Text type="secondary">停用只影响新建年度；历史年度仍保留原规范版本。</Typography.Text>
        {error ? <Alert type="error" showIcon title={error} /> : null}
        {packages === null && !error ? <Typography.Text type="secondary">正在加载规范目录…</Typography.Text> : null}
        {packages?.map((item) => (
          <Card key={item.id} size="small">
            <Flex align="center" justify="space-between" gap={12}>
              <Flex vertical gap={4}>
                <Typography.Text strong>{item.standard_code} · {item.official_edition}</Typography.Text>
                <Typography.Text type="secondary">
                  {familyName(item.family)} · 规则包 {item.package_version}
                </Typography.Text>
                <div>{statusTag(item)}</div>
              </Flex>
              <Button
                loading={busyId === item.id}
                disabled={(busyId !== null && busyId !== item.id) || item.sync_status === "故障"}
                onClick={() => void toggle(item)}
              >
                {item.is_enabled ? "停用" : "启用"}
              </Button>
            </Flex>
          </Card>
        ))}
      </Flex>
    </Modal>
  );
}
