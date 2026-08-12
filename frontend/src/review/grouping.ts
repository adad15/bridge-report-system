import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
} from "../contracts/annualInspection";

// 只读统计逻辑，镜像模块 05 规格 §9.2。本文件不修改任何数据。
// 逐条"需要处理"的派生已经并入 defectPhotoReviewModel：一条病害有哪些问题只有一个来源。

// 字段刻意用 snake_case：与后端 ReviewStatistics 的 JSON 线格式（wire shape）逐字段对应，
// 便于和 GET /review 返回的 statistics 直接比对。
export interface ReviewCounts {
  defect_count: number;
  photo_count: number;
  rating_item_count: number;
  pending_count: number;
  confirmed_count: number;
  modified_count: number;
  ignored_count: number;
  object_warning_count: number;
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
 * 统计视图，与后端 ReviewStatistics::build_review_statistics
 * （backend-cpp/src/review/ReviewStatistics.cpp）的分桶规则保持一致。
 */
export function buildStatistics(data: BridgeAnnualInspectionData): ReviewCounts {
  const counts: ReviewCounts = {
    defect_count: data.defects.length,
    photo_count: data.photos.length,
    rating_item_count: 0,
    pending_count: 0,
    confirmed_count: 0,
    modified_count: 0,
    ignored_count: 0,
    object_warning_count: 0,
  };

  for (const defect of data.defects) {
    tallyReviewStatus(defect.review_status, counts);
    if (defect.warnings.length > 0) {
      counts.object_warning_count += 1;
    }
  }
  for (const photo of data.photos) {
    if (photo.warnings.length > 0) {
      counts.object_warning_count += 1;
    }
  }

  return counts;
}
