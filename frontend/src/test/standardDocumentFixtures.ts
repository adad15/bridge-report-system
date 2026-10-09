import type { StandardDocument, StandardVersion } from "../api/standardDocumentsApi";

/** 规范原文库测试共用的示例数据：H21 三个版本、养护规范新旧两版。 */
export function standardVersion(overrides: Partial<StandardVersion> & { id: string }): StandardVersion {
  return {
    label: "版本",
    code: "CODE",
    kind: "official",
    status: "current",
    nature: "推荐性行业标准",
    issuer: "中华人民共和国交通运输部",
    editor: null,
    publish_date: null,
    effective_date: null,
    withdrawn_date: null,
    withdrawn_date_source: null,
    announcement: null,
    source: "交通运输部政府信息公开",
    note: null,
    replaced_by_version_id: null,
    replaces_version_id: null,
    file: { original_name: "standard.pdf", size_bytes: 3_456_789 },
    content_path: `/api/standard-document-versions/${overrides.id}/content`,
    uploaded_by: "系统管理员",
    uploaded_at: "2026-10-09 10:00:00",
    updated_at: "2026-10-09 10:00:00",
    ...overrides,
  };
}

export const h21Document: StandardDocument = {
  id: "h21",
  name: "公路桥梁技术状况评定标准",
  english_name: "Standards for Technical Condition Evaluation of Highway Bridges",
  category: "技术状况评定",
  default_version_id: "h21-2011",
  versions: [
    standardVersion({
      id: "h21-2026", label: "2026 报批稿", code: "JTG/T 52XX—2026", kind: "draft_approval",
      status: "draft_approval", publish_date: "2026-02-11", source: "第三方渠道（待核实）",
    }),
    standardVersion({
      id: "h21-2017", label: "2017 征求意见稿", code: "JTG/T H21—2017", kind: "draft_comment", status: "draft_comment",
    }),
    standardVersion({
      id: "h21-2011", label: "2011 版", code: "JTG/T H21—2011", publish_date: "2011-07-19",
      effective_date: "2011-09-01", announcement: "交通运输部公告2011年第41号", editor: "交通运输部公路科学研究院",
    }),
  ],
};

export const maintenanceDocument: StandardDocument = {
  id: "maintenance",
  name: "公路桥涵养护规范",
  english_name: null,
  category: "养护与检查",
  default_version_id: "jtg-5120",
  versions: [
    standardVersion({
      id: "jtg-5120", label: "2021 版", code: "JTG 5120—2021", nature: "行业标准", publish_date: "2021-08-09",
      effective_date: "2021-11-01", replaces_version_id: "h11",
    }),
    standardVersion({
      id: "h11", label: "2004 版", code: "JTG H11—2004", nature: "行业标准", status: "withdrawn",
      publish_date: "2004-06-28", effective_date: "2004-10-01", withdrawn_date: "2021-11-01",
      withdrawn_date_source: "replacement", replaced_by_version_id: "jtg-5120", source: "本地上传",
    }),
  ],
};

export const allStandardDocuments = [maintenanceDocument, h21Document];
