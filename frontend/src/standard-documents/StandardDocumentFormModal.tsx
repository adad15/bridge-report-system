import { Alert, AutoComplete, Button, Divider, Form, Input, Modal } from "antd";
import { useState } from "react";

import { type StandardDocument, createStandardDocument, standardDocumentsError, updateStandardDocument } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import {
  DEFAULT_ISSUER,
  PdfFileField,
  StandardVersionFields,
  type VersionFormValues,
  pdfFromFileList,
  toVersionInput,
} from "./StandardVersionFields";

export type StandardDocumentFormMode =
  | { type: "create"; categories: string[] }
  | { type: "edit"; document: StandardDocument; categories: string[] };

interface DocumentFormValues extends Partial<VersionFormValues> {
  name: string;
  english_name?: string;
  category: string;
}

interface Props {
  mode: StandardDocumentFormMode;
  /** 库里已有的规范，用来查重名。 */
  documents?: StandardDocument[];
  onClose: () => void;
  onSaved: (document: StandardDocument) => void;
  /** 新增时撞上已有规范：转去给它上传新版本，带上已填的版本信息和 PDF。 */
  onSwitchToAddVersion?: (document: StandardDocument, values: Partial<VersionFormValues>) => void;
}

/** 新增规范（连同首个版本和 PDF 一次提交）与编辑规范信息（设计 §5.7）。 */
export function StandardDocumentFormModal({ mode, documents = [], onClose, onSaved, onSwitchToAddVersion }: Props) {
  const [form] = Form.useForm<DocumentFormValues>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const initialValues: Partial<DocumentFormValues> = mode.type === "create"
    ? { kind: "official", issuer: DEFAULT_ISSUER }
    : { name: mode.document.name, english_name: mode.document.english_name ?? undefined, category: mode.document.category };
  // 前端先查重名：撞了就不提交，免得把几十 MB 的 PDF 传上去才被退回来。
  const sameNameAs = (name: string | undefined) => {
    const trimmed = name?.trim();
    if (!trimmed) return undefined;
    return documents.find((item) => item.name === trimmed && (mode.type === "create" || item.id !== mode.document.id));
  };
  const typedName = Form.useWatch("name", form);
  const existing = mode.type === "create" ? sameNameAs(typedName) : undefined;

  function switchToAddVersion(target: StandardDocument) {
    const { name, english_name, category, ...version } = form.getFieldsValue(true) as DocumentFormValues;
    onSwitchToAddVersion?.(target, version);
  }

  async function submit() {
    let values: DocumentFormValues;
    try {
      values = await form.validateFields();
    } catch {
      return;
    }
    const document = {
      name: values.name.trim(),
      english_name: values.english_name?.trim() ? values.english_name.trim() : null,
      category: values.category.trim(),
    };
    setBusy(true);
    setError(null);
    try {
      const saved = mode.type === "create"
        ? await createStandardDocument(backendBaseUrl, document, toVersionInput(values as VersionFormValues), pdfFromFileList(values.file) as File)
        : await updateStandardDocument(backendBaseUrl, mode.document.id, document);
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
      title={mode.type === "create" ? "新增规范" : "编辑规范信息"}
      mask={{ closable: false }}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText={mode.type === "create" ? "上传" : "保存"}
      cancelText="取消"
      okButtonProps={{ loading: busy }}
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void submit()}
      destroyOnHidden
      styles={{ body: { maxHeight: "calc(100vh - 240px)", overflowY: "auto", overflowX: "hidden" } }}
    >
      <Form form={form} layout="vertical" initialValues={initialValues}>
        {error ? <Alert type="error" showIcon title={error} style={{ marginBottom: 16 }} /> : null}
        <Form.Item
          label="规范名称"
          name="name"
          rules={[
            { required: true, whitespace: true, message: "请填写规范名称" },
            { max: 100, message: "不超过 100 个字" },
            { validator: async (_, value: string | undefined) => { if (sameNameAs(value)) throw new Error("已有同名规范"); } },
          ]}
        >
          <Input placeholder="如：公路桥梁技术状况评定标准" />
        </Form.Item>
        {existing && onSwitchToAddVersion ? (
          <Alert
            type="info"
            showIcon
            style={{ marginBottom: 16 }}
            title={`「${existing.name}」已在库里。`}
            description="新版本请上传到这部规范下，已填的版本信息和选好的 PDF 会一并带过去。"
            action={<Button size="small" type="primary" onClick={() => switchToAddVersion(existing)}>改为上传新版本</Button>}
          />
        ) : null}
        <Form.Item label="英文名称" name="english_name" rules={[{ max: 200, message: "不超过 200 个字" }]}>
          <Input />
        </Form.Item>
        <Form.Item
          label="分类"
          name="category"
          rules={[{ required: true, whitespace: true, message: "请填写分类" }, { max: 30, message: "不超过 30 个字" }]}
        >
          <AutoComplete options={mode.categories.map((value) => ({ value }))} placeholder="如：技术状况评定" />
        </Form.Item>
        {mode.type === "create" ? (
          <>
            <Divider titlePlacement="start" plain>首个版本</Divider>
            <StandardVersionFields />
            <PdfFileField />
          </>
        ) : null}
      </Form>
    </Modal>
  );
}
