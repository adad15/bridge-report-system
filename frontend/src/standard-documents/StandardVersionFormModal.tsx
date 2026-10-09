import { Alert, Form, Modal } from "antd";
import { useState } from "react";

import {
  type StandardDocument,
  type StandardVersion,
  addStandardVersion,
  replaceStandardVersionFile,
  standardDocumentsError,
  updateStandardVersion,
} from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import {
  DEFAULT_ISSUER,
  PdfFileField,
  StandardVersionFields,
  type VersionFormValues,
  pdfFromFileList,
  toVersionInput,
  versionFormValues,
} from "./StandardVersionFields";

export type StandardVersionFormMode =
  | { type: "add"; document: StandardDocument }
  | { type: "edit"; document: StandardDocument; version: StandardVersion }
  | { type: "replace"; document: StandardDocument; version: StandardVersion };

interface Props {
  mode: StandardVersionFormMode;
  onClose: () => void;
  onSaved: (document: StandardDocument) => void;
}

function initialValuesFor(mode: StandardVersionFormMode): Partial<VersionFormValues> {
  if (mode.type === "edit") return versionFormValues(mode.version);
  if (mode.type === "replace") return {};
  // 新版本多半和上一版同一个发布部门、同一种性质，先替管理员填上。
  const latest = mode.document.versions[0];
  return {
    kind: "official",
    nature: latest?.nature ?? undefined,
    issuer: latest?.issuer ?? DEFAULT_ISSUER,
    editor: latest?.editor ?? undefined,
  };
}

function titleFor(mode: StandardVersionFormMode): string {
  if (mode.type === "add") return `上传新版本 · ${mode.document.name}`;
  if (mode.type === "edit") return `编辑版本 · ${mode.version.label}`;
  return `替换原文 · ${mode.version.label}`;
}

/** 上传新版本、编辑版本、替换原文（设计 §5.7）。 */
export function StandardVersionFormModal({ mode, onClose, onSaved }: Props) {
  const [form] = Form.useForm<VersionFormValues>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit() {
    let values: VersionFormValues;
    try {
      values = await form.validateFields();
    } catch {
      return;
    }
    setBusy(true);
    setError(null);
    try {
      let saved: StandardDocument;
      if (mode.type === "add") {
        saved = await addStandardVersion(backendBaseUrl, mode.document.id, toVersionInput(values), pdfFromFileList(values.file) as File);
      } else if (mode.type === "edit") {
        saved = await updateStandardVersion(backendBaseUrl, mode.version.id, toVersionInput(values));
      } else {
        saved = await replaceStandardVersionFile(backendBaseUrl, mode.version.id, pdfFromFileList(values.file) as File);
      }
      onSaved(saved);
    } catch (caught) {
      setError(standardDocumentsError(caught));
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={720}
      title={titleFor(mode)}
      mask={{ closable: false }}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText={mode.type === "edit" ? "保存" : "上传"}
      cancelText="取消"
      okButtonProps={{ loading: busy }}
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void submit()}
      destroyOnHidden
      styles={{ body: { maxHeight: "calc(100vh - 240px)", overflowY: "auto", overflowX: "hidden" } }}
    >
      <Form form={form} layout="vertical" initialValues={initialValuesFor(mode)}>
        {error ? <Alert type="error" showIcon title={error} style={{ marginBottom: 16 }} /> : null}
        {mode.type === "replace" ? null : <StandardVersionFields />}
        {mode.type === "edit" ? null : <PdfFileField />}
      </Form>
    </Modal>
  );
}
