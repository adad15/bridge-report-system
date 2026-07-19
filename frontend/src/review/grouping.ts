import type {
  BridgeAnnualInspectionData,
  ComponentRatingCandidate,
  DefectCandidate,
  EvaluationPartRating,
  OverallRating,
  PhotoCandidate,
  Severity,
  StructurePartRating,
} from "../contracts/annualInspection";
import { defectFieldForWarning, type DefectTargetField } from "./reviewNavigation";

// 只读分类 / 统计逻辑，镜像模块 05 规格 §9.1（需要处理）与 §9.2（普通候选）。
// 本文件不修改任何数据，只从 BridgeAnnualInspectionData 派生只读视图。

export interface AttentionItem {
  kind: "defect" | "photo" | "rating" | "import";
  candidateId: string;
  message: string;
  severity: Severity;
  warningCode?: string;
  targetField?: DefectTargetField;
}

// 字段刻意用 snake_case：与后端 ReviewStatistics 的 JSON 线格式（wire shape）逐字段对应，
// 便于和 GET /review 返回的 statistics 直接比对；AttentionItem 是前端专属视图，用 camelCase。
export interface ReviewCounts {
  defect_count: number;
  photo_count: number;
  rating_item_count: number;
  pending_count: number;
  confirmed_count: number;
  modified_count: number;
  ignored_count: number;
  object_warning_count: number;
  needs_attention_count: number;
}

// §9.1 规则 3：尺寸原文里出现的数值/单位线索。与 measurementParser.ts 的解析正则族相互独立——
// 这里只是一个粗粒度启发式，用来判断“看起来像尺寸表达但没能结构化”，不追求精确匹配。
const MEASUREMENT_HINT_PATTERN = /\d+(\.\d+)?\s*(m|mm|cm|m2|m²|处|条)/;

function isNonEmptyString(value: string | null | undefined): value is string {
  return typeof value === "string" && value.trim() !== "";
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

// 目标候选按 candidate_id 归类：先在 defects/photos 里找，找不到再按 "ratings." 前缀识别评分候选
// （与后端 PreflightReport.cpp 生成的 "ratings.overall" / "ratings.structure_parts[i]" /
// "ratings.evaluation_parts[i]" 格式一致）；都不匹配时归为 import 级（无法定位到具体候选对象）。
function classifyCandidateId(data: BridgeAnnualInspectionData, candidateId: string): AttentionItem["kind"] {
  if (data.defects.some((defect) => defect.candidate_id === candidateId)) {
    return "defect";
  }
  if (data.photos.some((photo) => photo.candidate_id === candidateId)) {
    return "photo";
  }
  if (candidateId.startsWith("ratings.") || candidateId.startsWith("component_rating_")) {
    return "rating";
  }
  return "import";
}

// 病害引用的某个照片编号，是否存在一张“关联到这条病害”的照片候选。
function defectPhotoNumberIsLinked(data: BridgeAnnualInspectionData, defectId: string, photoNumber: string): boolean {
  return data.photos.some((photo) => photo.photo_number === photoNumber && photo.linked_defect_candidate_id === defectId);
}

/**
 * §9.1 需要处理：
 *   1. 病害/照片对象级 warnings[] 非空 —— 每条 warning 生成一条 AttentionItem。
 *   2. 导入级 warnings[]/errors[] 中带 target_candidate_id 的条目，按目标对象归类
 *      （没有 target_candidate_id 的导入级条目不进入这份候选清单，只在顶部导入概览展示）。
 *   3. 病害尺寸原文有数值/单位线索，但 measurements[] 未能结构化。
 *   4. 病害引用的照片编号，没有任何照片候选关联回它。
 *   5. 照片 match_status=未关联，或者未关联病害且未被忽略。
 */
export function needsAttention(data: BridgeAnnualInspectionData): AttentionItem[] {
  const items: AttentionItem[] = [];

  for (const defect of data.defects) {
    for (const warning of defect.warnings) {
      items.push({ kind: "defect", candidateId: defect.candidate_id, message: warning.message, severity: warning.severity, warningCode: warning.code, targetField: defectFieldForWarning(warning.code) });
    }
  }
  for (const photo of data.photos) {
    for (const warning of photo.warnings) {
      items.push({ kind: "photo", candidateId: photo.candidate_id, message: warning.message, severity: warning.severity, warningCode: warning.code });
    }
  }

  for (const item of [...data.warnings, ...data.errors]) {
    if (!item.target_candidate_id) {
      continue;
    }
    items.push({
      kind: classifyCandidateId(data, item.target_candidate_id),
      candidateId: item.target_candidate_id,
      message: item.message,
      severity: item.severity,
      warningCode: item.code,
      targetField: defectFieldForWarning(item.code),
    });
  }

  for (const defect of data.defects) {
    const hasHint = isNonEmptyString(defect.measurement_text) && MEASUREMENT_HINT_PATTERN.test(defect.measurement_text);
    const alreadyWarned = defect.warnings.some((warning) => warning.code === "measurement_parse_low_confidence") ||
      data.warnings.some((warning) =>
        warning.code === "measurement_parse_low_confidence" && warning.target_candidate_id === defect.candidate_id
      ) ||
      data.errors.some((warning) =>
        warning.code === "measurement_parse_low_confidence" && warning.target_candidate_id === defect.candidate_id
      );
    if (hasHint && defect.measurements.length === 0 && !alreadyWarned) {
      items.push({
        kind: "defect",
        candidateId: defect.candidate_id,
        message: "尺寸表达存在数值线索但未能结构化，请人工确认。",
        severity: "warning",
        warningCode: "measurement_parse_low_confidence",
        targetField: "measurement_text",
      });
    }
  }

  for (const defect of data.defects) {
    for (const photoNumber of defect.photo_numbers) {
      const missingAcknowledged = defect.confirmed_missing_photo_numbers.includes(photoNumber);
      if (!missingAcknowledged && !defectPhotoNumberIsLinked(data, defect.candidate_id, photoNumber)) {
        items.push({
          kind: "defect",
          candidateId: defect.candidate_id,
          message: `照片编号 ${photoNumber} 未匹配到关联图片。`,
          severity: "warning",
          warningCode: "photo_number_unmatched",
          targetField: "photo_numbers",
        });
      }
    }
  }

  for (const photo of data.photos) {
    const linkedEmpty = !isNonEmptyString(photo.linked_defect_candidate_id);
    const handledAsUnrelated = linkedEmpty && photo.match_status === "未关联" && photo.review_status === "已确认";
    if (!handledAsUnrelated && (photo.match_status === "未关联" || (linkedEmpty && photo.review_status !== "已忽略"))) {
      items.push({
        kind: "photo",
        candidateId: photo.candidate_id,
        message: "照片未关联到任何病害，请人工确认。",
        severity: "warning",
        warningCode: "photo_not_linked",
      });
    }

    if (
      isNonEmptyString(photo.linked_defect_candidate_id) &&
      photo.match_status === "已确认" &&
      (photo.review_status === "已确认" || photo.review_status === "已修改") &&
      !isNonEmptyString(photo.extracted_file.archive_relative_path)
    ) {
      items.push({
        kind: "photo",
        candidateId: photo.candidate_id,
        message: "照片归档文件缺失，无法确认入库。",
        severity: "error",
        warningCode: "photo_archive_missing",
      });
    }
  }

  for (const defect of data.defects) {
    if (defect.group_review_status === "待确认") {
      items.push({
        kind: "defect",
        candidateId: defect.candidate_id,
        message: "病害及照片尚未完成联合确认。",
        severity: "warning",
        warningCode: "defect_group_pending",
      });
    }
  }

  // 合同 1.2：构件评分对象级 warning + 未解决的评分差异（不一致/无法复算必须人工显式处理）。
  for (const rating of data.ratings.component_ratings) {
    for (const warning of rating.warnings) {
      items.push({ kind: "rating", candidateId: rating.candidate_id, message: warning.message, severity: warning.severity, warningCode: warning.code });
    }
    if (rating.review_status === "已忽略") {
      continue;
    }
    if (rating.score_validation_status === "不一致" || rating.score_validation_status === "无法复算") {
      const componentLabel = rating.component_ref.component_alias ?? rating.component_ref.component_name;
      items.push({
        kind: "rating",
        candidateId: rating.candidate_id,
        message: `构件 ${componentLabel} 的评分校验状态为「${rating.score_validation_status}」，请显式选择最终分并填写原因。`,
        severity: "warning",
      });
    }
  }

  return items;
}

/**
 * §9.2 普通病害：待确认 + 无对象级 warning + 没有导入级 error 指向它 + 核心字段完整 +
 * 每个引用的照片编号都有照片候选关联回它（photo_numbers 为空则视为满足）。
 */
export function isNormalDefect(defect: DefectCandidate, data: BridgeAnnualInspectionData): boolean {
  if (defect.review_status !== "待确认") {
    return false;
  }
  if (defect.warnings.length > 0) {
    return false;
  }
  const hasBlockingError = data.errors.some((error) => error.target_candidate_id === defect.candidate_id);
  if (hasBlockingError) {
    return false;
  }
  if (
    !isNonEmptyString(defect.structure_part) ||
    !isNonEmptyString(defect.component_name) ||
    !isNonEmptyString(defect.defect_type) ||
    !isNonEmptyString(defect.defect_description)
  ) {
    return false;
  }
  return defect.photo_numbers.every((photoNumber) => defectPhotoNumberIsLinked(data, defect.candidate_id, photoNumber));
}

/**
 * §9.2 普通照片：待确认 + 无对象级 warning + match_status=高置信候选 + 已关联病害。
 */
export function isNormalPhoto(photo: PhotoCandidate): boolean {
  return (
    photo.review_status === "待确认" &&
    photo.warnings.length === 0 &&
    photo.match_status === "高置信候选" &&
    isNonEmptyString(photo.linked_defect_candidate_id)
  );
}

function isOverallRating(
  item: OverallRating | StructurePartRating | EvaluationPartRating
): item is OverallRating {
  return "total_score" in item;
}

function isStructurePartRating(
  item: OverallRating | StructurePartRating | EvaluationPartRating
): item is StructurePartRating {
  return "structure_score" in item;
}

/**
 * §9.2 普通评分项：待确认 + 分数已填写；全桥评分额外要求等级非空
 * （结构分部/评价部件的等级、权重等其余字段第一版不在批量确认门槛内，用户仍可在
 * “技术状况评定”分组里单独编辑或确认它们）。
 */
export function isNormalRating(item: OverallRating | StructurePartRating | EvaluationPartRating): boolean {
  if (item.review_status !== "待确认") {
    return false;
  }
  if (isOverallRating(item)) {
    return isFiniteNumber(item.total_score) && isNonEmptyString(item.overall_grade);
  }
  if (isStructurePartRating(item)) {
    return isFiniteNumber(item.structure_score);
  }
  return isFiniteNumber(item.part_score);
}

/**
 * §9.2 普通构件评分：待确认 + 无对象级 warning + 校验状态自动「一致」。
 * 「不一致/无法复算」必须人工显式处理，人工已解决的候选 review_status 已是「已修改」，
 * 两类都不进入批量确认。
 */
export function isNormalComponentRating(rating: ComponentRatingCandidate): boolean {
  return (
    rating.review_status === "待确认" &&
    rating.warnings.length === 0 &&
    rating.score_validation_status === "一致"
  );
}

function tallyReviewStatus(
  status: DefectCandidate["review_status"],
  counts: Pick<ReviewCounts, "pending_count" | "confirmed_count" | "modified_count" | "ignored_count">
): void {
  if (status === "待确认") {
    counts.pending_count += 1;
  } else if (status === "已确认") {
    counts.confirmed_count += 1;
  } else if (status === "已修改") {
    counts.modified_count += 1;
  } else if (status === "已忽略") {
    counts.ignored_count += 1;
  }
}

/**
 * 统计视图，与后端 ReviewStatistics::build_review_statistics（backend-cpp/src/review/ReviewStatistics.cpp）
 * 的分桶规则保持一致，另外加上前端专属的 needs_attention_count。
 */
export function buildStatistics(
  data: BridgeAnnualInspectionData,
  includeImportedRatings = true,
): ReviewCounts {
  const counts: ReviewCounts = {
    defect_count: data.defects.length,
    photo_count: data.photos.length,
    rating_item_count: includeImportedRatings
      ? 1 +
        data.ratings.structure_parts.length +
        data.ratings.evaluation_parts.length +
        data.ratings.component_ratings.length
      : 0,
    pending_count: 0,
    confirmed_count: 0,
    modified_count: 0,
    ignored_count: 0,
    object_warning_count: 0,
    needs_attention_count: 0,
  };

  for (const defect of data.defects) {
    tallyReviewStatus(defect.review_status, counts);
    if (defect.warnings.length > 0) {
      counts.object_warning_count += 1;
    }
  }
  for (const photo of data.photos) {
    tallyReviewStatus(photo.review_status, counts);
    if (photo.warnings.length > 0) {
      counts.object_warning_count += 1;
    }
  }

  if (includeImportedRatings) {
    tallyReviewStatus(data.ratings.overall.review_status, counts);
    for (const part of data.ratings.structure_parts) {
      tallyReviewStatus(part.review_status, counts);
    }
    for (const part of data.ratings.evaluation_parts) {
      tallyReviewStatus(part.review_status, counts);
    }
    for (const rating of data.ratings.component_ratings) {
      tallyReviewStatus(rating.review_status, counts);
      if (rating.warnings.length > 0) {
        counts.object_warning_count += 1;
      }
    }
  }

  counts.needs_attention_count = needsAttention(data).length;

  return counts;
}
