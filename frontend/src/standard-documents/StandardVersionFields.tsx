import { InboxOutlined } from "@ant-design/icons";
import { DatePicker, Flex, Form, Input, Select, Typography, Upload, theme, type UploadFile } from "antd";
import dayjs, { type Dayjs } from "dayjs";

import {
  STANDARD_PDF_MAX_BYTES,
  type StandardVersion,
  type StandardVersionInput,
  type StandardVersionKind,
} from "../api/standardDocumentsApi";
import { VERSION_KIND_LABELS } from "./versionStatus";

export const DEFAULT_ISSUER = "中华人民共和国交通运输部";

export interface VersionFormValues {
  label: string;
  code: string;
  kind: StandardVersionKind;
  nature?: string;
  issuer?: string;
  editor?: string;
  publish_date?: Dayjs | null;
  effective_date?: Dayjs | null;
  withdrawn_date?: Dayjs | null;
  announcement?: string;
  source?: string;
  note?: string;
  file?: UploadFile[];
}

function textOrNull(value: string | undefined): string | null {
  const trimmed = value?.trim() ?? "";
  return trimmed === "" ? null : trimmed;
}

function dateOrNull(value: Dayjs | null | undefined): string | null {
  return value ? value.format("YYYY-MM-DD") : null;
}

/** 表单值 → 接口入参。稿件不带实施、废止日期（设计 §5.7）。 */
export function toVersionInput(values: VersionFormValues): StandardVersionInput {
  const draft = values.kind !== "official";
  return {
    label: values.label.trim(),
    code: values.code.trim(),
    kind: values.kind,
    nature: textOrNull(values.nature),
    issuer: textOrNull(values.issuer),
    editor: textOrNull(values.editor),
    publish_date: dateOrNull(values.publish_date),
    effective_date: draft ? null : dateOrNull(values.effective_date),
    withdrawn_date: draft ? null : dateOrNull(values.withdrawn_date),
    announcement: textOrNull(values.announcement),
    source: textOrNull(values.source),
    note: textOrNull(values.note),
  };
}

/** 版本 → 编辑表单的初始值。 */
export function versionFormValues(version: StandardVersion): VersionFormValues {
  const parse = (value: string | null) => (value ? dayjs(value) : null);
  return {
    label: version.label,
    code: version.code,
    kind: version.kind,
    nature: version.nature ?? undefined,
    issuer: version.issuer ?? undefined,
    editor: version.editor ?? undefined,
    publish_date: parse(version.publish_date),
    effective_date: parse(version.effective_date),
    // 只回填手工填的废止日期。被新版本替代推算出来的那个一旦回填再保存，就成了手工值，
    // 以后删掉新版本，旧版本也恢复不了现行（设计 §6.5）。
    withdrawn_date: version.withdrawn_date_source === "manual" ? parse(version.withdrawn_date) : null,
    announcement: version.announcement ?? undefined,
    source: version.source ?? undefined,
    note: version.note ?? undefined,
  };
}

export function pdfFromFileList(fileList: UploadFile[] | undefined): File | null {
  return (fileList?.[0]?.originFileObj as File | undefined) ?? null;
}

/** 选 PDF：只在本地选中，随表单一起提交。前端先查扩展名和大小，后端再查文件头。 */
export function PdfFileField() {
  const { token } = theme.useToken();
  return (
    <Form.Item
      label="原文 PDF"
      name="file"
      required
      valuePropName="fileList"
      getValueFromEvent={(event: { fileList: UploadFile[] }) => event.fileList.slice(-1)}
      rules={[{
        validator: async (_, fileList: UploadFile[] | undefined) => {
          const file = pdfFromFileList(fileList);
          if (!file) throw new Error("请选择 PDF 文件");
          if (!file.name.toLowerCase().endsWith(".pdf")) throw new Error("只能上传 PDF 文件");
          if (file.size === 0) throw new Error("文件是空的");
          if (file.size > STANDARD_PDF_MAX_BYTES) throw new Error("文件超过 256 MB 上限");
        },
      }]}
    >
      <Upload.Dragger accept=".pdf" maxCount={1} beforeUpload={() => false}>
        <Flex vertical align="center" gap={4}>
          <InboxOutlined style={{ fontSize: token.fontSizeHeading2, color: token.colorPrimary }} />
          <Typography.Text>点击或拖入 PDF 文件</Typography.Text>
          <Typography.Text type="secondary">不超过 256 MB</Typography.Text>
        </Flex>
      </Upload.Dragger>
    </Form.Item>
  );
}

const LENGTH_200 = { max: 200, message: "不超过 200 个字" };

/** 版本字段。稿件类型选征求意见稿或报批稿时，「发布日期」改称「稿件日期」，实施、废止日期隐藏。 */
export function StandardVersionFields() {
  const form = Form.useFormInstance<VersionFormValues>();
  const kind = Form.useWatch("kind", form);
  const draft = kind !== undefined && kind !== "official";
  const kinds = Object.keys(VERSION_KIND_LABELS) as StandardVersionKind[];
  return (
    <>
      <Flex gap={12} wrap>
        <Form.Item
          label="版本名称"
          name="label"
          style={{ flex: "1 1 200px" }}
          rules={[{ required: true, whitespace: true, message: "请填写版本名称" }, { max: 40, message: "不超过 40 个字" }]}
        >
          <Input placeholder="如：2011 版、2026 报批稿" />
        </Form.Item>
        <Form.Item
          label="标准编号"
          name="code"
          style={{ flex: "1 1 200px" }}
          rules={[{ required: true, whitespace: true, message: "请填写标准编号" }, { max: 60, message: "不超过 60 个字" }]}
        >
          <Input placeholder="如：JTG/T H21—2011" />
        </Form.Item>
        <Form.Item label="稿件类型" name="kind" style={{ flex: "0 0 150px" }} rules={[{ required: true, message: "请选择稿件类型" }]}>
          <Select options={kinds.map((value) => ({ value, label: VERSION_KIND_LABELS[value] }))} />
        </Form.Item>
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label={draft ? "稿件日期" : "发布日期"} name="publish_date" style={{ flex: "1 1 160px" }}>
          <DatePicker style={{ width: "100%" }} />
        </Form.Item>
        {draft ? null : (
          <>
            <Form.Item
              label="实施日期"
              name="effective_date"
              style={{ flex: "1 1 160px" }}
              rules={[{ required: true, message: "正式版必须填写实施日期" }]}
            >
              <DatePicker style={{ width: "100%" }} />
            </Form.Item>
            <Form.Item
              label="废止日期"
              name="withdrawn_date"
              style={{ flex: "1 1 160px" }}
              tooltip="被新版本替代的不用填，系统按新版本的实施日期推算"
              dependencies={["effective_date"]}
              rules={[({ getFieldValue }) => ({
                validator: async (_, value: Dayjs | null | undefined) => {
                  const effective = getFieldValue("effective_date") as Dayjs | null | undefined;
                  if (value && effective && value.isBefore(effective, "day")) throw new Error("废止日期不能早于实施日期");
                },
              })]}
            >
              <DatePicker style={{ width: "100%" }} />
            </Form.Item>
          </>
        )}
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label="标准性质" name="nature" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input placeholder="如：推荐性行业标准" />
        </Form.Item>
        <Form.Item label="发布部门" name="issuer" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input />
        </Form.Item>
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label="主编单位" name="editor" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input />
        </Form.Item>
        <Form.Item label="公告文号" name="announcement" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input placeholder="如：交通运输部公告2011年第41号" />
        </Form.Item>
      </Flex>
      <Form.Item label="原文来源" name="source" rules={[LENGTH_200]}>
        <Input placeholder="如：交通运输部政府信息公开" />
      </Form.Item>
      <Form.Item label="备注" name="note" rules={[{ max: 500, message: "不超过 500 个字" }]}>
        <Input.TextArea rows={2} />
      </Form.Item>
    </>
  );
}
