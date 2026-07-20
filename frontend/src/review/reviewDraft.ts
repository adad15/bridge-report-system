import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
  ReviewStatus,
  StructurePart,
} from "../contracts/annualInspection";
import { canConfirmDefectPhotoGroup } from "./defectPhotoGroups";
import { parseMeasurements } from "./measurementParser";

// useReducer 草稿 reducer：所有分支都必须返回新对象/新数组，绝不原地修改传入的 state
// （测试会断言原 state 引用未被改动）。只读字段 candidate_id/source_ref/confidence/
// warnings/original_caption（模块 05 §8.1/§8.2）没有对应的 action，本文件不提供修改它们的入口。

export interface DefectComponentSelection {
  componentName: string;
  componentNumber: string;
  bridgeComponentId: string;
  standardComponentCategoryId: string;
  resolvedStructurePart: StructurePart;
  inventoryRevisionId: string;
}

export interface ManualDefectInput extends DefectComponentSelection {
  defectLocation: string;
  defectType: string;
  defectDescription: string;
  defectScale?: number | null;
}

export type CandidateIdFactory = () => string;

// 数值字段的 value 一律要求 number，字符串字段一律要求 string——把 field↔value 的合法组合
// 编译期锁死（模块 05 §8.1/§8.3 可编辑字段白名单）。HTML input 拿到的是字符串，由调用方
// （UI 组件，Task 13/14）在 dispatch 前用 Number(...) 转好再传进来，reducer 不再做运行时兜底转换。
export type ReviewDraftAction =
  | { type: "add_defect"; input: ManualDefectInput }
  | { type: "delete_defect"; candidateId: string }
  | { type: "link_defect_component"; candidateId: string; component: DefectComponentSelection }
  // §8.1 病害候选可编辑字段（measurement_text 走它自己的 edit_measurement_text action）。
  | { type: "edit_defect_field"; candidateId: string; field: "component_name"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "component_number"; value: string | null }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_location"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_scale"; value: number | null }
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
  | { type: "confirm_defect_group"; defectCandidateId: string };

// 从 union 里抽出各分组，供 reducer 内部 helper 使用（Extract/Exclude 保证与上面的 union 单一真源同步）。
type EditDefectFieldAction = Extract<ReviewDraftAction, { type: "edit_defect_field" }>;
type EditDefectContentFieldAction = Exclude<EditDefectFieldAction, { field: "review_status" }>;

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
    case "component_name":
      return { ...defect, component_name: action.value, review_status, group_review_status: "待确认" };
    case "component_number":
      return { ...defect, component_number: action.value, review_status, group_review_status: "待确认" };
    case "defect_location":
      return { ...defect, defect_location: action.value, review_status, group_review_status: "待确认" };
    case "defect_scale":
      return { ...defect, defect_scale: action.value, review_status, group_review_status: "待确认" };
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

function defaultCandidateIdFactory(): string {
  return `manual_defect_${crypto.randomUUID()}`;
}

function nextUnusedCandidateId(
  state: BridgeAnnualInspectionData,
  factory: CandidateIdFactory,
  issuedCandidateIds: Set<string>,
): string {
  const existing = new Set(state.defects.map((defect) => defect.candidate_id));
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const candidateId = factory();
    if (candidateId.startsWith("manual_defect_") && !existing.has(candidateId) && !issuedCandidateIds.has(candidateId)) {
      issuedCandidateIds.add(candidateId);
      return candidateId;
    }
  }
  throw new Error("无法生成唯一的人工病害编号");
}

function reduceReviewDraft(
  state: BridgeAnnualInspectionData,
  action: ReviewDraftAction,
  candidateIdFactory: CandidateIdFactory,
  issuedCandidateIds: Set<string>,
): BridgeAnnualInspectionData {
  switch (action.type) {
    case "add_defect": {
      const input = action.input;
      const defect: DefectCandidate = {
        candidate_id: nextUnusedCandidateId(state, candidateIdFactory, issuedCandidateIds),
        source_structure_part: null,
        component_name: input.componentName,
        component_number: input.componentNumber,
        bridge_component_id: input.bridgeComponentId,
        standard_component_category_id: input.standardComponentCategoryId,
        resolved_structure_part: input.resolvedStructurePart,
        component_inventory_revision_id: input.inventoryRevisionId,
        component_match_candidate_ids: [input.bridgeComponentId],
        component_match_method: "manual",
        component_match_confirmed_by: null,
        defect_location: input.defectLocation,
        defect_type: input.defectType,
        defect_description: input.defectDescription,
        defect_scale: input.defectScale ?? null,
        quantity_text: null,
        measurement_text: null,
        measurements: [],
        photo_numbers: [],
        group_review_status: "待确认",
        confirmed_missing_photo_numbers: [],
        severity: null,
        remark: null,
        source_ref: { source_type: "manual" },
        confidence: 1,
        review_status: "已修改",
        review_note: null,
        warnings: [],
      };
      return { ...state, defects: [...state.defects, defect] };
    }

    case "delete_defect": {
      if (!state.defects.some((defect) => defect.candidate_id === action.candidateId)) return state;
      return {
        ...state,
        defects: state.defects.filter((defect) => defect.candidate_id !== action.candidateId),
        photos: state.photos.map((photo) =>
          photo.linked_defect_candidate_id === action.candidateId
            ? {
                ...photo,
                linked_defect_candidate_id: null,
                match_status: "未关联",
                review_status: "已修改",
              }
            : photo
        ),
      };
    }

    case "link_defect_component": {
      const component = action.component;
      return {
        ...state,
        defects: updateDefect(state.defects, action.candidateId, (defect) => ({
          ...defect,
          bridge_component_id: component.bridgeComponentId,
          standard_component_category_id: component.standardComponentCategoryId,
          resolved_structure_part: component.resolvedStructurePart,
          component_inventory_revision_id: component.inventoryRevisionId,
          component_match_candidate_ids: [component.bridgeComponentId],
          component_match_method: "manual",
          component_match_confirmed_by: null,
          warnings: defect.warnings.filter((warning) =>
            warning.code !== "defect_component_match_required" &&
            warning.code !== "defect_component_match_ambiguous"),
          review_status: nextStatusAfterContentEdit(defect.review_status),
          group_review_status: "待确认",
        })),
      };
    }

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
      return {
        ...state,
        defects: updateDefect(state.defects, candidateId, (defect) => applyDefectContentEdit(defect, action)),
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

    default: {
      // 穷尽性检查：将来新增第 13 种 action.type 却忘了在上面处理时，这里会编译失败。
      // 运行期兜底返回原 state（未类型化的 JS 调用方若派发未知 action 时不破坏状态）。
      const _exhaustive: never = action;
      void _exhaustive;
      return state;
    }
  }
}

export function createReviewDraftReducer(candidateIdFactory: CandidateIdFactory = defaultCandidateIdFactory) {
  const issuedCandidateIds = new Set<string>();
  return (state: BridgeAnnualInspectionData, action: ReviewDraftAction): BridgeAnnualInspectionData =>
    reduceReviewDraft(state, action, candidateIdFactory, issuedCandidateIds);
}

export const reviewDraftReducer = createReviewDraftReducer();
