import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  EvaluationPartRating,
  PhotoCandidate,
  Ratings,
  RatingStructurePart,
  ReviewStatus,
  StructurePart,
  StructurePartRating,
} from "../contracts/annualInspection";
import { isNormalRating } from "./grouping";
import { canConfirmDefectPhotoGroup } from "./defectPhotoGroups";
import { parseMeasurements } from "./measurementParser";

// useReducer 草稿 reducer：所有分支都必须返回新对象/新数组，绝不原地修改传入的 state
// （测试会断言原 state 引用未被改动）。只读字段 candidate_id/source_ref/confidence/
// warnings/original_caption（模块 05 §8.1/§8.2）没有对应的 action，本文件不提供修改它们的入口。

// 评分候选的目标定位：全桥 / 按 structure_part 匹配的结构分部 / 按数组下标匹配的评价部件
// （评价部件用下标而不是 category_no+evaluation_part 组合定位，理由：下标在一次草稿会话内
// 稳定且唯一，UI 表格本来就按下标渲染行，不需要再引入一套复合键匹配逻辑）。set_rating_status
// 对任意目标都适用，因此保留这个统一的目标类型；edit_rating_field 因为要把 field↔value 类型
// 绑死（见下方 union），改为在每个变体里内联 target 形状。
export type RatingTarget = "overall" | { part: RatingStructurePart } | { evaluation: number };

// 数值字段的 value 一律要求 number，字符串字段一律要求 string——把 field↔value 的合法组合
// 编译期锁死（模块 05 §8.1/§8.3 可编辑字段白名单）。HTML input 拿到的是字符串，由调用方
// （UI 组件，Task 13/14）在 dispatch 前用 Number(...) 转好再传进来，reducer 不再做运行时兜底转换。
export type ReviewDraftAction =
  // §8.1 病害候选可编辑字段（measurement_text 走它自己的 edit_measurement_text action）。
  | { type: "edit_defect_field"; candidateId: string; field: "structure_part"; value: StructurePart }
  | { type: "edit_defect_field"; candidateId: string; field: "component_name"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "component_alias"; value: string | null }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_location"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_type"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_description"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "quantity_text"; value: string | null }
  | { type: "edit_defect_field"; candidateId: string; field: "photo_numbers"; value: string[] }
  // review_status 既可通过 set_defect_status 设置，也可通过 edit_defect_field 设置——两条路径
  // 等价，都不套用下面 nextStatusAfterContentEdit 的“内容编辑自动流转为已修改”规则。
  | { type: "edit_defect_field"; candidateId: string; field: "review_status"; value: ReviewStatus }
  | { type: "edit_defect_field"; candidateId: string; field: "review_note"; value: string | null }
  | { type: "edit_measurement_text"; candidateId: string; text: string | null }
  | { type: "set_defect_status"; candidateId: string; status: ReviewStatus }
  | { type: "photo_confirm_match"; candidateId: string }
  | { type: "photo_relink"; candidateId: string; defectCandidateId: string }
  | { type: "photo_mark_unrelated"; candidateId: string; note: string }
  | { type: "photo_ignore"; candidateId: string }
  | { type: "photo_reset"; candidateId: string }
  | { type: "edit_photo_number"; candidateId: string; photoNumber: string }
  | { type: "confirm_missing_photo"; defectCandidateId: string; photoNumber: string }
  | { type: "unconfirm_missing_photo"; defectCandidateId: string; photoNumber: string }
  | { type: "confirm_defect_group"; defectCandidateId: string }
  // §8.3 评分候选可编辑字段：target↔field↔value 三者绑死。数值字段（total_score/
  // structure_score/part_score）只收 number，等级字段（overall_grade/grade）只收 string。
  | { type: "edit_rating_field"; target: "overall"; field: "total_score"; value: number }
  | { type: "edit_rating_field"; target: "overall"; field: "overall_grade"; value: string }
  | { type: "edit_rating_field"; target: { part: RatingStructurePart }; field: "structure_score"; value: number }
  | { type: "edit_rating_field"; target: { part: RatingStructurePart }; field: "grade"; value: string }
  | { type: "edit_rating_field"; target: { evaluation: number }; field: "part_score"; value: number }
  | { type: "set_rating_status"; target: RatingTarget; status: ReviewStatus }
  | { type: "batch_confirm_normal_ratings" };

// 从 union 里抽出各分组，供 reducer 内部 helper 使用（Extract/Exclude 保证与上面的 union 单一真源同步）。
type EditDefectFieldAction = Extract<ReviewDraftAction, { type: "edit_defect_field" }>;
type EditDefectContentFieldAction = Exclude<EditDefectFieldAction, { field: "review_status" }>;
type EditRatingFieldAction = Extract<ReviewDraftAction, { type: "edit_rating_field" }>;

// 编辑内容字段后的状态流转规则（模块 05 §8.1）：处于 待确认/已确认 的候选，内容一改就自动
// 流转为 已修改；已忽略 的候选不会因为内容编辑被“复活”，仍留在 已忽略；已修改 编辑后还是 已修改。
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

function invalidateDefectGroups(defects: DefectCandidate[], candidateIds: Array<string | null | undefined>): DefectCandidate[] {
  const ids = new Set(candidateIds.filter((value): value is string => typeof value === "string" && value.length > 0));
  if (ids.size === 0) return defects;
  return defects.map((defect) =>
    ids.has(defect.candidate_id) ? { ...defect, group_review_status: "待确认" } : defect
  );
}

// 内容字段编辑：按 field 逐分支处理，让每条 { ...defect, <具体字段>: action.value } 都在
// field 被收窄后拿到正确的 value 类型，从而不需要任何 `as` 断言。缺任何一个 case 都会因
// “函数并非所有路径都返回 DefectCandidate” 而编译失败，等价于一次穷尽性检查。
function applyDefectContentEdit(defect: DefectCandidate, action: EditDefectContentFieldAction): DefectCandidate {
  const review_status = nextStatusAfterContentEdit(defect.review_status);
  switch (action.field) {
    case "structure_part":
      return { ...defect, structure_part: action.value, review_status, group_review_status: "待确认" };
    case "component_name":
      return { ...defect, component_name: action.value, review_status, group_review_status: "待确认" };
    case "component_alias":
      return { ...defect, component_alias: action.value, review_status, group_review_status: "待确认" };
    case "defect_location":
      return { ...defect, defect_location: action.value, review_status, group_review_status: "待确认" };
    case "defect_type":
      return { ...defect, defect_type: action.value, review_status, group_review_status: "待确认" };
    case "defect_description":
      return { ...defect, defect_description: action.value, review_status, group_review_status: "待确认" };
    case "quantity_text":
      return { ...defect, quantity_text: action.value, review_status, group_review_status: "待确认" };
    case "photo_numbers":
      return {
        ...defect,
        photo_numbers: action.value,
        confirmed_missing_photo_numbers: defect.confirmed_missing_photo_numbers.filter((number) => action.value.includes(number)),
        review_status,
        group_review_status: "待确认",
      };
    case "review_note":
      return { ...defect, review_note: action.value, review_status, group_review_status: "待确认" };
  }
}

function mapStructurePart(
  parts: StructurePartRating[],
  target: RatingStructurePart,
  updater: (part: StructurePartRating) => StructurePartRating
): StructurePartRating[] {
  return parts.map((part) => (part.structure_part === target ? updater(part) : part));
}

function mapEvaluationPart(
  parts: EvaluationPartRating[],
  index: number,
  updater: (part: EvaluationPartRating) => EvaluationPartRating
): EvaluationPartRating[] {
  return parts.map((part, currentIndex) => (currentIndex === index ? updater(part) : part));
}

// 按 field 收窄整个 action（每个 field 字面量只属于一个 union 成员），从而同时拿到正确的
// target 形状和 value 类型，同样无需 `as`。
function applyRatingFieldEdit(ratings: Ratings, action: EditRatingFieldAction): Ratings {
  switch (action.field) {
    case "total_score":
      return { ...ratings, overall: { ...ratings.overall, total_score: action.value } };
    case "overall_grade":
      return { ...ratings, overall: { ...ratings.overall, overall_grade: action.value } };
    case "structure_score":
      return {
        ...ratings,
        structure_parts: mapStructurePart(ratings.structure_parts, action.target.part, (part) => ({
          ...part,
          structure_score: action.value,
        })),
      };
    case "grade":
      return {
        ...ratings,
        structure_parts: mapStructurePart(ratings.structure_parts, action.target.part, (part) => ({
          ...part,
          grade: action.value,
        })),
      };
    case "part_score":
      return {
        ...ratings,
        evaluation_parts: mapEvaluationPart(ratings.evaluation_parts, action.target.evaluation, (part) => ({
          ...part,
          part_score: action.value,
        })),
      };
  }
}

function applyRatingStatus(ratings: Ratings, target: RatingTarget, status: ReviewStatus): Ratings {
  if (target === "overall") {
    return { ...ratings, overall: { ...ratings.overall, review_status: status } };
  }
  if ("part" in target) {
    return {
      ...ratings,
      structure_parts: mapStructurePart(ratings.structure_parts, target.part, (part) => ({ ...part, review_status: status })),
    };
  }
  return {
    ...ratings,
    evaluation_parts: mapEvaluationPart(ratings.evaluation_parts, target.evaluation, (part) => ({ ...part, review_status: status })),
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
      const { candidateId } = action;
      if (action.field === "review_status") {
        // 显式状态设置，等价于 set_defect_status，不走内容编辑的自动流转。
        const status = action.value;
        return {
          ...state,
          defects: updateDefect(state.defects, candidateId, (defect) => ({
            ...defect,
            review_status: status,
            group_review_status: "待确认",
          })),
        };
      }
      return { ...state, defects: updateDefect(state.defects, candidateId, (defect) => applyDefectContentEdit(defect, action)) };
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
          group_review_status: "待确认",
        })),
      };
    }

    case "set_defect_status": {
      const { candidateId, status } = action;
      return {
        ...state,
        defects: updateDefect(state.defects, candidateId, (defect) => ({
          ...defect,
          review_status: status,
          group_review_status: "待确认",
        })),
      };
    }

    case "photo_confirm_match": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo?.linked_defect_candidate_id) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: updatePhoto(state.photos, action.candidateId, (item) => ({
          ...item,
          match_status: "已确认",
          review_status: "已确认",
        })),
      };
    }

    case "photo_relink": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo || !state.defects.some((item) => item.candidate_id === action.defectCandidateId)) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id, action.defectCandidateId]),
        photos: updatePhoto(state.photos, action.candidateId, (item) => ({
          ...item,
          linked_defect_candidate_id: action.defectCandidateId,
          match_status: "已确认",
          review_status: "已修改",
        })),
      };
    }

    case "photo_mark_unrelated": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo) return state;
      void action.note;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: updatePhoto(state.photos, action.candidateId, (item) => ({
          ...item,
          linked_defect_candidate_id: null,
          match_status: "未关联",
          review_status: "已确认",
        })),
      };
    }

    case "photo_ignore": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: updatePhoto(state.photos, action.candidateId, (item) => ({
          ...item,
          linked_defect_candidate_id: null,
          review_status: "已忽略",
        })),
      };
    }

    case "photo_reset": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: updatePhoto(state.photos, action.candidateId, (item) => ({
          ...item,
          match_status: "待校对",
          review_status: "待确认",
        })),
      };
    }

    case "edit_photo_number": {
      const photo = state.photos.find((item) => item.candidate_id === action.candidateId);
      if (!photo) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: updatePhoto(state.photos, action.candidateId, (photo) => ({ ...photo, photo_number: action.photoNumber })),
      };
    }

    case "confirm_missing_photo": {
      const defect = state.defects.find((item) => item.candidate_id === action.defectCandidateId);
      const isReferenced = defect?.photo_numbers.includes(action.photoNumber) ?? false;
      const hasCandidate = state.photos.some((item) => item.photo_number === action.photoNumber);
      if (!defect || !isReferenced || hasCandidate) return state;
      return {
        ...state,
        defects: updateDefect(state.defects, action.defectCandidateId, (item) => ({
          ...item,
          group_review_status: "待确认",
          confirmed_missing_photo_numbers: item.confirmed_missing_photo_numbers.includes(action.photoNumber)
            ? item.confirmed_missing_photo_numbers
            : [...item.confirmed_missing_photo_numbers, action.photoNumber],
        })),
      };
    }

    case "unconfirm_missing_photo": {
      return {
        ...state,
        defects: updateDefect(state.defects, action.defectCandidateId, (item) => ({
          ...item,
          group_review_status: "待确认",
          confirmed_missing_photo_numbers: item.confirmed_missing_photo_numbers.filter(
            (number) => number !== action.photoNumber
          ),
        })),
      };
    }

    case "confirm_defect_group": {
      if (!canConfirmDefectPhotoGroup(state, action.defectCandidateId).ok) return state;
      return {
        ...state,
        defects: updateDefect(state.defects, action.defectCandidateId, (item) => ({
          ...item,
          group_review_status: "已确认",
          review_status: item.review_status === "待确认" ? "已确认" : item.review_status,
        })),
      };
    }

    case "edit_rating_field": {
      return { ...state, ratings: applyRatingFieldEdit(state.ratings, action) };
    }

    case "set_rating_status": {
      return { ...state, ratings: applyRatingStatus(state.ratings, action.target, action.status) };
    }

    case "batch_confirm_normal_ratings": {
      return {
        ...state,
        ratings: batchConfirmNormalRatings(state.ratings),
      };
    }

    default: {
      // 穷尽性检查：将来新增第 13 种 action.type 却忘了在上面处理时，这里会编译失败。
      // 运行期兜底返回原 state（未类型化的 JS 调用方若派发未知 action 时不破坏状态）。
      const _exhaustive: never = action;
      void _exhaustive;
      return state;
    }
  }
}
