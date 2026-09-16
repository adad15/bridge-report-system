import { Alert, Flex, Form, InputNumber, Modal, Select } from "antd";
import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  createInspectionYear,
  fetchRatingTreeVersions,
  workspaceErrorMessage,
  type RatingTreeVersionSummary,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  bridgeId: string;
  onClose: () => void;
  onCreated: (inspectionYearId: string, reused: boolean) => void;
}

export function CreateInspectionDialog({ bridgeId, onClose, onCreated }: Props) {
  const [year, setYear] = useState<number | null>(new Date().getFullYear());
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [trees, setTrees] = useState<RatingTreeVersionSummary[] | null>(null);
  const [ratingTreeVersionId, setRatingTreeVersionId] = useState("");

  useEffect(() => {
    let cancelled = false;
    fetchRatingTreeVersions(backendBaseUrl)
      .then((items) => {
        if (cancelled) return;
        setTrees(items);
        const defaultTree = items.find((item) => item.is_default) ??
          (items.length === 1 ? items[0] : undefined);
        if (defaultTree) setRatingTreeVersionId(defaultTree.id);
      })
      .catch((caught) => setError(workspaceErrorMessage(caught)));
    return () => { cancelled = true; };
  }, []);

  async function submit() {
    if (year === null || !Number.isInteger(year) || year < 1900 || year > 2200) {
      setError("检测年度必须是 1900 至 2200 的整数。");
      return;
    }
    if (!ratingTreeVersionId) {
      setError("请选择桥梁评定树。");
      return;
    }
    setSubmitting(true);
    setError(null);
    try {
      const created = await createInspectionYear(backendBaseUrl, bridgeId, {
        inspection_year: year,
        rating_tree_version_id: ratingTreeVersionId,
      });
      onCreated(created.id, false);
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "inspection_year_already_exists") {
        const details = caught.details as { existing_inspection_year_id?: unknown } | undefined;
        if (typeof details?.existing_inspection_year_id === "string") {
          onCreated(details.existing_inspection_year_id, true);
          return;
        }
      }
      setError(workspaceErrorMessage(caught));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <Modal
      open
      centered
      title="新建年度检测"
      okText="创建年度"
      cancelText="取消"
      confirmLoading={submitting}
      okButtonProps={{ disabled: trees === null }}
      cancelButtonProps={{ disabled: submitting }}
      mask={{ closable: false }}
      onOk={() => void submit()}
      onCancel={onClose}
    >
      <Form layout="vertical" requiredMark={false} onFinish={() => void submit()}>
        <Form.Item label="检测年度" htmlFor="create-inspection-year">
          {/* 不设 min/max：越界时 InputNumber 会在失焦时悄悄改成边界值，用户看不到自己填错了。 */}
          <InputNumber
            id="create-inspection-year"
            style={{ width: "100%" }}
            precision={0}
            value={year}
            onChange={setYear}
          />
        </Form.Item>
        <Form.Item label="桥梁评定树" htmlFor="create-inspection-tree">
          <Select
            id="create-inspection-tree"
            placeholder="请选择桥梁评定树"
            value={ratingTreeVersionId || undefined}
            disabled={trees === null || submitting}
            onChange={setRatingTreeVersionId}
            options={trees?.map((item) => ({
              value: item.id,
              label: `${item.tree_name} · 树 ${item.package_version} · H21 ${item.h21_package_version} · JTG 5120 ${item.maintenance_package_version}`,
            }))}
          />
        </Form.Item>
      </Form>
      <Flex vertical gap={8}>
        {trees !== null && trees.length === 0 ? (
          <Alert type="warning" showIcon role="note" title="缺少已发布的桥梁评定树，请检查后端规则同步状态。" />
        ) : null}
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
