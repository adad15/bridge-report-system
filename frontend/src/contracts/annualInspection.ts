export type ReviewStatus = "待确认" | "已确认" | "已修改" | "已忽略";
export type ComparisonConfirmationStatus = "待确认" | "已确认" | "已修改" | "已拒绝";
export type Severity = "info" | "warning" | "error";
export type StructurePart = "全桥" | "上部结构" | "下部结构" | "桥面系" | "其他";
export type RatingStructurePart = "上部结构" | "下部结构" | "桥面系";
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

export interface WarningItem {
  code: string;
  message: string;
  severity: Severity;
  target_candidate_id?: string | null;
}

export interface SourceRef {
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
  version: "1.0";
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
  value: number;
  unit: string;
  source_text: string;
}

export interface DefectCandidate {
  candidate_id: string;
  structure_part: StructurePart;
  component_name: string;
  component_alias?: string | null;
  defect_type: string;
  defect_location: string;
  defect_description: string;
  quantity_text?: string | null;
  measurement_text?: string | null;
  measurements: Measurement[];
  photo_numbers: string[];
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

export interface OverallRating {
  total_score: number;
  overall_grade: string;
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
}

export interface StructurePartRating {
  structure_part: RatingStructurePart;
  structure_score: number;
  weight: number;
  grade: string;
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
}

export interface EvaluationScoreRow {
  component_count: number;
  component_score: number;
}

export interface EvaluationPartRating {
  structure_part: RatingStructurePart;
  category_no: number;
  evaluation_part: string;
  part_score: number;
  score_rows: EvaluationScoreRow[];
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
}

export interface Ratings {
  overall: OverallRating;
  structure_parts: StructurePartRating[];
  evaluation_parts: EvaluationPartRating[];
  warnings: WarningItem[];
}

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
  ratings: Ratings;
  comparison_candidates: ComparisonCandidate[];
  report_text_candidates: ReportTextCandidate[];
  warnings: WarningItem[];
  errors: WarningItem[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasOwn(value: Record<string, unknown>, property: string): boolean {
  return Object.prototype.hasOwnProperty.call(value, property);
}

function isNumberFromZeroToOne(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1;
}

function getRequiredArray(value: Record<string, unknown>, property: string): unknown[] | null {
  const member = value[property];
  return hasOwn(value, property) && Array.isArray(member) ? member : null;
}

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

function isValidDefectCandidate(value: unknown): boolean {
  return (
    isRecord(value) &&
    hasValidConfidence(value) &&
    hasRequiredArrayMembers(value, ["measurements", "photo_numbers", "warnings"]) &&
    hasRequiredObjectMembers(value, ["source_ref"])
  );
}

function isValidPhotoCandidate(value: unknown): boolean {
  return (
    isRecord(value) &&
    hasValidConfidence(value) &&
    hasRequiredArrayMembers(value, ["warnings"]) &&
    hasRequiredObjectMembers(value, ["extracted_file", "source_ref"])
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

function isValidOverallRating(value: unknown): boolean {
  return isRecord(value) && hasValidConfidence(value) && getRequiredObject(value, "source_ref") !== null;
}

function isValidStructurePartRating(value: unknown): boolean {
  return isRecord(value) && hasValidConfidence(value) && getRequiredObject(value, "source_ref") !== null;
}

function isValidEvaluationPartRating(value: unknown): boolean {
  return (
    isRecord(value) &&
    !hasOwn(value, "grade") &&
    hasValidConfidence(value) &&
    getRequiredArray(value, "score_rows") !== null &&
    getRequiredObject(value, "source_ref") !== null
  );
}

export function isBridgeAnnualInspectionData(value: unknown): value is BridgeAnnualInspectionData {
  if (!isRecord(value)) {
    return false;
  }

  const contract = getRequiredObject(value, "contract");
  if (contract === null) {
    return false;
  }
  if (contract.name !== "BridgeAnnualInspectionData" || contract.version !== "1.0") {
    return false;
  }

  const importContext = getRequiredObject(value, "import_context");
  const bridgeCheck = getRequiredObject(value, "bridge_check");
  const inspection = getRequiredObject(value, "inspection");
  const ratings = getRequiredObject(value, "ratings");
  if (importContext === null || bridgeCheck === null || inspection === null || ratings === null) {
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

  const overall = getRequiredObject(ratings, "overall");
  const structureParts = getRequiredArray(ratings, "structure_parts");
  const evaluationParts = getRequiredArray(ratings, "evaluation_parts");
  const ratingWarnings = getRequiredArray(ratings, "warnings");
  if (
    overall === null ||
    structureParts === null ||
    evaluationParts === null ||
    ratingWarnings === null
  ) {
    return false;
  }

  return (
    defects.every(isValidDefectCandidate) &&
    photos.every(isValidPhotoCandidate) &&
    comparisonCandidates.every(isValidComparisonCandidate) &&
    isValidOverallRating(overall) &&
    structureParts.every(isValidStructurePartRating) &&
    evaluationParts.every(isValidEvaluationPartRating)
  );
}
