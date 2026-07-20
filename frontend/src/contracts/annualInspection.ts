export type ReviewStatus = "待确认" | "已确认" | "已修改" | "已忽略";
export type DefectGroupReviewStatus = "待确认" | "已确认";
export type ComparisonConfirmationStatus = "待确认" | "已确认" | "已修改" | "已拒绝";
export type Severity = "info" | "warning" | "error";
export type StructurePart = "全桥" | "上部结构" | "下部结构" | "桥面系" | "其他";
export type SourceType = "软件导出Word" | "正式Word" | "Excel病害表" | "图片包" | "接口同步" | "JSON导入";
export type FileRole = "当前年度检测资料" | "历史正式报告" | "历史基线资料" | "修订资料";
export type DataRole = "当前年度" | "历史基线" | "修订版";
export type BridgeMatchStatus = "匹配" | "不匹配" | "待人工确认";
export type PhotoMatchStatus = "高置信候选" | "待校对" | "已确认" | "未关联" | "已忽略";
export type ComparisonType =
  | "原病害无明显变化"
  | "原病害发展"
  | "原病害减轻"
  | "原病害修复"
  | "新增病害"
  | "原病害未见"
  | "无法判断";

// 以下接口与 Python Pydantic 模型保持同名字段，作为前端校对页面的候选数据边界。
export interface WarningItem {
  code: string;
  message: string;
  severity: Severity;
  target_candidate_id?: string | null;
}

export interface SourceRef {
  source_type?: "word" | "manual";
  chapter?: string | null;
  table_title?: string | null;
  table_index?: number | null;
  row_index?: number | null;
  column_name?: string | null;
  raw_row_text?: string | null;
  photo_area_caption?: string | null;
  file_role?: FileRole | null;
  paragraph_index?: number | null;
}

export interface ContractInfo {
  name: "BridgeAnnualInspectionData";
  version: "2.0";
  generated_at: string;
  producer: string;
  parser_name: string;
  parser_version: string;
}

export interface ImportContext {
  source_type: SourceType;
  file_role: FileRole;
  archived_file_system_number: string;
  import_record_system_number: string;
}

export interface BridgeCheck {
  selected_bridge_system_number: string;
  extracted_bridge_name?: string | null;
  match_status: BridgeMatchStatus;
  warnings: WarningItem[];
}

export interface InspectionInfo {
  inspection_year: number;
  inspection_date: string;
  report_number: string;
  project_name: string;
  data_role: DataRole;
}

export interface Measurement {
  dimension_type: string;
  value_type: "single" | "range";
  value: number | null;
  minimum_value: number | null;
  maximum_value: number | null;
  unit: string;
  is_approximate: boolean;
  source_text: string;
}

export interface DefectCandidate {
  candidate_id: string;
  source_structure_part?: StructurePart | null;
  component_name: string;
  component_number?: string | null;
  bridge_component_id?: string | null;
  standard_component_category_id?: string | null;
  resolved_structure_part?: StructurePart | null;
  component_inventory_revision_id?: string | null;
  component_match_candidate_ids?: string[];
  component_match_method?: "exact" | "confirmed_alias" | "normalized_candidate" | "manual" | null;
  component_match_confirmed_by?: string | null;
  defect_type: string;
  defect_location: string;
  defect_scale?: number | null;
  defect_description: string;
  quantity_text?: string | null;
  measurement_text?: string | null;
  measurements: Measurement[];
  photo_numbers: string[];
  group_review_status: DefectGroupReviewStatus;
  confirmed_missing_photo_numbers: string[];
  severity?: Severity | null;
  remark?: string | null;
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
  review_note?: string | null;
  warnings: WarningItem[];
}

export interface ExtractedPhotoFile {
  temporary_file_name: string;
  original_caption?: string | null;
  archive_relative_path?: string | null;
}

export interface PhotoCandidate {
  candidate_id: string;
  photo_number: string;
  linked_defect_candidate_id?: string | null;
  extracted_file: ExtractedPhotoFile;
  match_status: PhotoMatchStatus;
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
  warnings: WarningItem[];
}

// 对比候选来自已确认事实之间的匹配，不是 Word 解析器直接抽取的内容。
export interface ComparisonMatchBasis {
  same_component: boolean;
  same_defect_type: boolean;
  location_similarity: number;
  measurement_change_detected: boolean;
  photo_number_related: boolean;
}

export interface ComparisonCandidate {
  candidate_id: string;
  previous_defect_observation_system_number?: string | null;
  current_defect_observation_system_number?: string | null;
  comparison_type: ComparisonType;
  match_basis?: ComparisonMatchBasis | null;
  change_summary?: string | null;
  confidence: number;
  confirmation_status: ComparisonConfirmationStatus;
  review_note?: string | null;
  warnings: WarningItem[];
}

export interface ReportTextCandidate {
  candidate_id: string;
  section_key: string;
  section_title: string;
  text: string;
  usage?: string | null;
  source_ref: SourceRef;
  review_status: ReviewStatus;
}

export interface BridgeAnnualInspectionData {
  contract: ContractInfo;
  import_context: ImportContext;
  bridge_check: BridgeCheck;
  inspection: InspectionInfo;
  defects: DefectCandidate[];
  photos: PhotoCandidate[];
  comparison_candidates: ComparisonCandidate[];
  report_text_candidates: ReportTextCandidate[];
  warnings: WarningItem[];
  errors: WarningItem[];
}

// 运行时校验只做前端入口防线，完整契约仍以 JSON Schema 和后端校验为准。
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasOwn(value: Record<string, unknown>, property: string): boolean {
  return Object.prototype.hasOwnProperty.call(value, property);
}

// 置信度用于校对排序和风险提示，必须是闭区间 0 到 1 的数值。
function isNumberFromZeroToOne(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1;
}

// 必填数组必须显式存在，空数组表示“确认没有”，缺字段表示契约不完整。
function getRequiredArray(value: Record<string, unknown>, property: string): unknown[] | null {
  const member = value[property];
  return hasOwn(value, property) && Array.isArray(member) ? member : null;
}

// 必填对象必须显式存在，保证前端拿到的是稳定的候选 JSON 骨架。
function getRequiredObject(value: Record<string, unknown>, property: string): Record<string, unknown> | null {
  const member = value[property];
  return hasOwn(value, property) && isRecord(member) ? member : null;
}

function hasValidConfidence(value: unknown): boolean {
  return isRecord(value) && isNumberFromZeroToOne(value.confidence);
}

function hasRequiredArrayMembers(value: Record<string, unknown>, members: string[]): boolean {
  return members.every((member) => getRequiredArray(value, member) !== null);
}

function hasRequiredObjectMembers(value: Record<string, unknown>, members: string[]): boolean {
  return members.every((member) => getRequiredObject(value, member) !== null);
}

function isValidSourceRef(value: unknown): boolean {
  return (
    isRecord(value) &&
    (value.source_type === undefined || value.source_type === "word" || value.source_type === "manual")
  );
}

// 病害标度是规范意义的正整数，与提示级别 severity 完全分离。
function isNullablePositiveInteger(value: unknown): boolean {
  return (
    value === undefined ||
    value === null ||
    (typeof value === "number" && Number.isInteger(value) && value > 0)
  );
}

function isValidMeasurement(value: unknown): value is Measurement {
  if (
    !isRecord(value) ||
    typeof value.dimension_type !== "string" ||
    value.dimension_type.length === 0 ||
    typeof value.unit !== "string" ||
    value.unit.length === 0 ||
    typeof value.source_text !== "string" ||
    value.source_text.length === 0 ||
    typeof value.is_approximate !== "boolean"
  ) {
    return false;
  }
  if (value.value_type === "single") {
    return typeof value.value === "number" && Number.isFinite(value.value) &&
      value.minimum_value === null && value.maximum_value === null;
  }
  if (value.value_type === "range") {
    return value.value === null &&
      typeof value.minimum_value === "number" && Number.isFinite(value.minimum_value) &&
      typeof value.maximum_value === "number" && Number.isFinite(value.maximum_value) &&
      value.minimum_value <= value.maximum_value;
  }
  return false;
}

function isValidDefectCandidate(value: unknown): boolean {
  if (!isRecord(value) || !hasValidConfidence(value)) {
    return false;
  }
  const missingPhotoNumbers = getRequiredArray(value, "confirmed_missing_photo_numbers");
  const measurements = getRequiredArray(value, "measurements");
  const matchCandidateIds = value.component_match_candidate_ids;
  const matchMethod = value.component_match_method;
  return (
    hasRequiredArrayMembers(value, ["measurements", "photo_numbers", "warnings"]) &&
    measurements !== null && measurements.every(isValidMeasurement) &&
    hasRequiredObjectMembers(value, ["source_ref"]) &&
    isValidSourceRef(value.source_ref) &&
    typeof value.component_name === "string" &&
    value.component_name.length > 0 &&
    typeof value.defect_type === "string" &&
    value.defect_type.length > 0 &&
    typeof value.defect_location === "string" &&
    value.defect_location.length > 0 &&
    typeof value.defect_description === "string" &&
    value.defect_description.length > 0 &&
    !hasOwn(value, "structure_part") &&
    !hasOwn(value, "component_alias") &&
    !hasOwn(value, "defect_deduction") &&
    (value.group_review_status === "待确认" || value.group_review_status === "已确认") &&
    isNullablePositiveInteger(value.defect_scale) &&
    (matchCandidateIds === undefined ||
      (Array.isArray(matchCandidateIds) &&
        matchCandidateIds.every((item) => typeof item === "string") &&
        new Set(matchCandidateIds).size === matchCandidateIds.length)) &&
    (matchMethod === undefined ||
      matchMethod === null ||
      matchMethod === "exact" ||
      matchMethod === "confirmed_alias" ||
      matchMethod === "normalized_candidate" ||
      matchMethod === "manual") &&
    (value.component_inventory_revision_id === undefined ||
      value.component_inventory_revision_id === null ||
      typeof value.component_inventory_revision_id === "string") &&
    (value.component_match_confirmed_by === undefined ||
      value.component_match_confirmed_by === null ||
      typeof value.component_match_confirmed_by === "string") &&
    missingPhotoNumbers !== null &&
    missingPhotoNumbers.every((item) => typeof item === "string") &&
    new Set(missingPhotoNumbers).size === missingPhotoNumbers.length
  );
}

function isValidPhotoCandidate(value: unknown): boolean {
  return (
    isRecord(value) &&
    hasValidConfidence(value) &&
    hasRequiredArrayMembers(value, ["warnings"]) &&
    hasRequiredObjectMembers(value, ["extracted_file", "source_ref"]) &&
    isValidSourceRef(value.source_ref)
  );
}

function isValidComparisonCandidate(value: unknown): boolean {
  if (!isRecord(value) || !hasValidConfidence(value) || getRequiredArray(value, "warnings") === null) {
    return false;
  }
  if (value.match_basis === undefined || value.match_basis === null) {
    return true;
  }
  return isRecord(value.match_basis) && isNumberFromZeroToOne(value.match_basis.location_similarity);
}

export function isBridgeAnnualInspectionData(value: unknown): value is BridgeAnnualInspectionData {
  if (!isRecord(value)) {
    return false;
  }

  // 契约名和版本先过关，避免旧格式数据进入新校对流程。
  const contract = getRequiredObject(value, "contract");
  if (contract === null) {
    return false;
  }
  if (contract.name !== "BridgeAnnualInspectionData" || contract.version !== "2.0") {
    return false;
  }

  if (hasOwn(value, "ratings")) {
    return false;
  }

  const importContext = getRequiredObject(value, "import_context");
  const bridgeCheck = getRequiredObject(value, "bridge_check");
  const inspection = getRequiredObject(value, "inspection");
  if (importContext === null || bridgeCheck === null || inspection === null) {
    return false;
  }

  const defects = getRequiredArray(value, "defects");
  const photos = getRequiredArray(value, "photos");
  const comparisonCandidates = getRequiredArray(value, "comparison_candidates");
  const reportTextCandidates = getRequiredArray(value, "report_text_candidates");
  const warnings = getRequiredArray(value, "warnings");
  const errors = getRequiredArray(value, "errors");
  if (
    defects === null ||
    photos === null ||
    comparisonCandidates === null ||
    reportTextCandidates === null ||
    warnings === null ||
    errors === null
  ) {
    return false;
  }

  if (getRequiredArray(bridgeCheck, "warnings") === null) {
    return false;
  }

  return (
    defects.every(isValidDefectCandidate) &&
    photos.every(isValidPhotoCandidate) &&
    comparisonCandidates.every(isValidComparisonCandidate)
  );
}
