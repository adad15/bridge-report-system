import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  EvaluationPartRating,
  OverallRating,
  PhotoCandidate,
  Ratings,
  RatingStructurePart,
  ReviewStatus,
  StructurePartRating,
} from "../contracts/annualInspection";
import { isNormalDefect, isNormalPhoto, isNormalRating } from "./grouping";
import { parseMeasurements } from "./measurementParser";

// useReducer 草稿 reducer：所有分支都必须返回新对象/新数组，绝不原地修改传入的 state
// （测试会断言原 state 引用未被改动）。只读字段 candidate_id/source_ref/confidence/
// warnings/original_caption（模块 05 §8.1/§8.2）没有对应的 action，本文件不提供修改它们的入口。

// §8.1 病害候选可编辑字段白名单（measurement_text 走它自己的 edit_measurement_text action）。
// review_status 既可以通过 set_defect_status 显式设置，也可以通过 edit_defect_field(field:
// 'review_status') 设置——reducer 的 "edit_defect_field" 分支对这个字段做了特判，两条路径
// 效果等价，都不套用下面 nextStatusAfterContentEdit 的自动流转规则。
export type DefectEditableField =
  | "structure_part"
  | "component_name"
  | "component_alias"
  | "defect_location"
  | "defect_type"
  | "defect_description"
  | "quantity_text"
  | "photo_numbers"
  | "review_status"
  | "review_note";

// §8.3 评分候选可编辑字段。
export type RatingEditableField = "total_score" | "overall_grade" | "structure_score" | "grade" | "part_score";

// 评分候选的目标定位：全桥 / 按 structure_part 匹配的结构分部 / 按数组下标匹配的评价部件
// （评价部件用下标而不是 category_no+evaluation_part 组合定位，理由：下标在一次草稿会话内
// 稳定且唯一，UI 表格本来就按下标渲染行，不需要再引入一套复合键匹配逻辑）。
export type RatingTarget = "overall" | { part: RatingStructurePart } | { evaluation: number };

export type ReviewDraftAction =
  | { type: "edit_defect_field"; candidateId: string; field: DefectEditableField; value: unknown }
  | { type: "edit_measurement_text"; candidateId: string; text: string | null }
  | { type: "set_defect_status"; candidateId: string; status: ReviewStatus }
  | { type: "photo_confirm_match"; candidateId: string }
  | { type: "photo_unlink"; candidateId: string }
  | { type: "photo_mark_unrelated"; candidateId: string }
  | { type: "photo_ignore"; candidateId: string }
  | { type: "edit_photo_number"; candidateId: string; photoNumber: string }
  | { type: "edit_photo_link"; candidateId: string; defectCandidateId: string | null }
  | { type: "edit_rating_field"; target: RatingTarget; field: RatingEditableField; value: unknown }
  | { type: "set_rating_status"; target: RatingTarget; status: ReviewStatus }
  | { type: "batch_confirm_normal" };

const NUMERIC_RATING_FIELDS: ReadonlySet<RatingEditableField> = new Set(["total_score", "structure_score", "part_score"]);

// 编辑内容字段后的状态流转规则（模块 05 §8.1）：处于 待确认/已确认 的候选，内容一改就自动
// 流转为 已修改；已忽略 的候选不会因为内容编辑被“复活”，仍留在 已忽略；已修改 编辑后还是 已修改。
// 这条规则只适用于“内容字段”编辑（edit_defect_field 的非 review_status 分支、
// edit_measurement_text）；直接调用 set_defect_status / edit_defect_field(field: 'review_status')
// 是显式状态操作，不走这条自动流转规则。
function nextStatusAfterContentEdit(current: ReviewStatus): ReviewStatus {
  return current === "已忽略" ? current : "已修改";
}

function updateDefect(
  defects: DefectCandidate[],
  candidateId: string,
  updater: (defect: DefectCandidate) => DefectCandidate
): DefectCandidate[] {
  return defects.map((defect) => (defect.candidate_id === candidateId ? updater(defect) : defect));
}

function updatePhoto(
  photos: PhotoCandidate[],
  candidateId: string,
  updater: (photo: PhotoCandidate) => PhotoCandidate
): PhotoCandidate[] {
  return photos.map((photo) => (photo.candidate_id === candidateId ? updater(photo) : photo));
}

function coerceNumberField(value: unknown): number {
  return typeof value === "string" ? Number(value) : (value as number);
}

// 判断 target 是不是结构分部目标；排除 "overall" 和这个之后，剩下的分支 TS 会自动把
// target 收窄成 { evaluation: number }，不需要再写一个对称的 isEvaluationTarget 守卫。
function isPartTarget(target: RatingTarget): target is { part: RatingStructurePart } {
  return typeof target === "object" && target !== null && "part" in target;
}

function applyRatingFieldEdit(ratings: Ratings, target: RatingTarget, field: RatingEditableField, value: unknown): Ratings {
  const coercedValue = NUMERIC_RATING_FIELDS.has(field) ? coerceNumberField(value) : value;

  if (target === "overall") {
    return { ...ratings, overall: { ...ratings.overall, [field]: coercedValue } as OverallRating };
  }
  if (isPartTarget(target)) {
    return {
      ...ratings,
      structure_parts: ratings.structure_parts.map((part) =>
        part.structure_part === target.part ? ({ ...part, [field]: coercedValue } as StructurePartRating) : part
      ),
    };
  }
  const evaluationIndex = target.evaluation;
  return {
    ...ratings,
    evaluation_parts: ratings.evaluation_parts.map((part, index) =>
      index === evaluationIndex ? ({ ...part, [field]: coercedValue } as EvaluationPartRating) : part
    ),
  };
}

function applyRatingStatus(ratings: Ratings, target: RatingTarget, status: ReviewStatus): Ratings {
  if (target === "overall") {
    return { ...ratings, overall: { ...ratings.overall, review_status: status } };
  }
  if (isPartTarget(target)) {
    return {
      ...ratings,
      structure_parts: ratings.structure_parts.map((part) =>
        part.structure_part === target.part ? { ...part, review_status: status } : part
      ),
    };
  }
  const evaluationIndex = target.evaluation;
  return {
    ...ratings,
    evaluation_parts: ratings.evaluation_parts.map((part, index) =>
      index === evaluationIndex ? { ...part, review_status: status } : part
    ),
  };
}

function batchConfirmNormalRatings(ratings: Ratings): Ratings {
  return {
    ...ratings,
    overall: isNormalRating(ratings.overall) ? { ...ratings.overall, review_status: "已确认" } : ratings.overall,
    structure_parts: ratings.structure_parts.map((part) =>
      isNormalRating(part) ? { ...part, review_status: "已确认" } : part
    ),
    evaluation_parts: ratings.evaluation_parts.map((part) =>
      isNormalRating(part) ? { ...part, review_status: "已确认" } : part
    ),
  };
}

export function reviewDraftReducer(state: BridgeAnnualInspectionData, action: ReviewDraftAction): BridgeAnnualInspectionData {
  switch (action.type) {
    case "edit_defect_field": {
      const { candidateId, field, value } = action;
      return {
        ...state,
        defects: updateDefect(state.defects, candidateId, (defect) => {
          // edit_defect_field 对 review_status 字段的编辑等价于 set_defect_status：
          // 直接把状态设成调用方给的值，不套用“内容编辑自动流转为已修改”的规则。
          if (field === "review_status") {
            return { ...defect, review_status: value as ReviewStatus };
          }
          return {
            ...defect,
            [field]: value,
            review_status: nextStatusAfterContentEdit(defect.review_status),
          } as DefectCandidate;
        }),
      };
    }

    case "edit_measurement_text": {
      const { candidateId, text } = action;
      return {
        ...state,
        defects: updateDefect(state.defects, candidateId, (defect) => ({
          ...defect,
          measurement_text: text,
          measurements: parseMeasurements(text),
          review_status: nextStatusAfterContentEdit(defect.review_status),
        })),
      };
    }

    case "set_defect_status": {
      const { candidateId, status } = action;
      return {
        ...state,
        defects: updateDefect(state.defects, candidateId, (defect) => ({ ...defect, review_status: status })),
      };
    }

    case "photo_confirm_match": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => {
          // §8.2：确认匹配要求 linked_defect_candidate_id 非空；没有关联时是空操作。
          if (!photo.linked_defect_candidate_id) {
            return photo;
          }
          return { ...photo, match_status: "已确认" };
        }),
      };
    }

    case "photo_unlink": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({
          ...photo,
          linked_defect_candidate_id: null,
          match_status: "待校对",
        })),
      };
    }

    case "photo_mark_unrelated": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({
          ...photo,
          linked_defect_candidate_id: null,
          match_status: "未关联",
        })),
      };
    }

    case "photo_ignore": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({ ...photo, review_status: "已忽略" })),
      };
    }

    case "edit_photo_number": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({ ...photo, photo_number: action.photoNumber })),
      };
    }

    case "edit_photo_link": {
      return {
        ...state,
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({
          ...photo,
          linked_defect_candidate_id: action.defectCandidateId,
        })),
      };
    }

    case "edit_rating_field": {
      return { ...state, ratings: applyRatingFieldEdit(state.ratings, action.target, action.field, action.value) };
    }

    case "set_rating_status": {
      return { ...state, ratings: applyRatingStatus(state.ratings, action.target, action.status) };
    }

    case "batch_confirm_normal": {
      return {
        ...state,
        defects: state.defects.map((defect) =>
          isNormalDefect(defect, state) ? { ...defect, review_status: "已确认" } : defect
        ),
        photos: state.photos.map((photo) => (isNormalPhoto(photo) ? { ...photo, review_status: "已确认" } : photo)),
        ratings: batchConfirmNormalRatings(state.ratings),
      };
    }

    default:
      return state;
  }
}
