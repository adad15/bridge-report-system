import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
  ReviewStatus,
  StructurePart,
} from "../contracts/annualInspection";
import type { RatingTreeMatchMethod } from "../contracts/resolution";
import { isHumanAcknowledgeableWarning } from "./defectWarnings";
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
  ratingTreeVersionId: string;
  ratingTreeNodeId: string;
  defectDescription: string;
  defectScale?: number | null;
  isScoring: boolean;
}

export type CandidateIdFactory = () => string;

// 数值字段的 value 一律要求 number，字符串字段一律要求 string——把 field↔value 的合法组合
// 编译期锁死（模块 05 §8.1/§8.3 可编辑字段白名单）。HTML input 拿到的是字符串，由调用方
// （UI 组件，Task 13/14）在 dispatch 前用 Number(...) 转好再传进来，reducer 不再做运行时兜底转换。
export type ReviewDraftAction =
  | { type: "delete_defect"; candidateId: string }
  // §8.1 病害候选可编辑字段（measurement_text 走它自己的 edit_measurement_text action）。
  | { type: "edit_defect_field"; candidateId: string; field: "component_name"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "component_number"; value: string | null }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_location"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_scale"; value: number | null }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_type"; value: string }
  | { type: "edit_defect_field"; candidateId: string; field: "quantity_text"; value: string | null }
  // review_status 既可通过 set_defect_status 设置，也可通过 edit_defect_field 设置——两条路径
  // 等价，都不套用下面 nextStatusAfterContentEdit 的“内容编辑自动流转为已修改”规则。
  | { type: "edit_defect_field"; candidateId: string; field: "review_status"; value: ReviewStatus }
  | { type: "edit_defect_field"; candidateId: string; field: "defect_description"; value: string }
  | { type: "edit_measurement_text"; candidateId: string; text: string | null }
  | {
      type: "select_rating_tree_node";
      candidateId: string;
      versionId: string;
      nodeId: string;
      nodeName: string;
      isScoring: boolean;
      matchEvidence: string;
    }
  | {
      type: "select_rating_tree_nodes";
      candidateIds: string[];
      versionId: string;
      nodeId: string;
      nodeName: string;
      isScoring: boolean;
      matchEvidence: string;
    }
  // 后端批量匹配返回的唯一自动结果。方式沿用后端给的 exact/controlled_alias/
  | { type: "ignore_defect"; candidateId: string }
  | { type: "restore_ignored_defect"; candidateId: string }
  // 照片归属由 link/unlink 表达；换绑仍是先 unlink 再 link。
  | { type: "link_photo_to_defect"; photoCandidateId: string; defectCandidateId: string }
  | { type: "unlink_photo_from_defect"; photoCandidateId: string }
  | {
      type: "set_photo_reference_missing";
      defectCandidateId: string;
      photoNumber: string;
      missing: boolean;
    }
  // 范围拆分把 Word 引用整条复制给了每一侧，其中大半根本不是自己的图。这个 action
  // 把不属于本病害的编号从引用清单里摘掉——与"确认缺图"是两件事：那句说的是
  // "原报告没印这张图"，这句说的是"这张图好好的，只是它是别人的"。
  | {
      type: "remove_photo_reference";
      defectCandidateId: string;
      photoNumber: string;
    }
  // 人工上传/删除已经在服务端落库；这两个 action 只是把结果同步进本地草稿。
  | { type: "add_photo"; photo: PhotoCandidate }
  | { type: "remove_photo"; photoCandidateId: string }
  | { type: "confirm_defect_groups"; candidateIds: string[] }
  | { type: "set_defect_status"; candidateId: string; status: ReviewStatus }
  // 服务端已经改写过草稿（构件绑定、批量替换、标记缺失、取消绑定、范围拆分）时，
  // 用重新拉取到的最新草稿整体替换本地状态。服务端在这些操作上是权威方。
  | { type: "replace_draft"; data: BridgeAnnualInspectionData };

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

function applyRatingTreeSelection(
  defect: DefectCandidate,
  selection: {
    versionId: string;
    nodeId: string;
    nodeName: string;
    isScoring: boolean;
    matchEvidence: string;
  },
): DefectCandidate {
  // 5.0：节点本身写进评分树解析表，由调用方走
  // `PUT /defect-instances/{id}/rating-resolution`。草稿这边只跟着改人选完节点后
  // 连带变化的**来源事实**：病害名称与标度。
  void selection.versionId;
  void selection.matchEvidence;
  return {
    ...defect,
    defect_type: selection.nodeName,
    defect_scale: selection.isScoring ? defect.defect_scale : null,
    review_status: nextStatusAfterContentEdit(defect.review_status),
    group_review_status: "待确认",
  };
}

function reduceReviewDraft(
  state: BridgeAnnualInspectionData,
  action: ReviewDraftAction,
  candidateIdFactory: CandidateIdFactory,
  issuedCandidateIds: Set<string>,
): BridgeAnnualInspectionData {
  switch (action.type) {
    case "replace_draft": {
      return action.data;
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
              }
            : photo
        ),
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

    case "select_rating_tree_node": {
      return {
        ...state,
        defects: updateDefect(state.defects, action.candidateId, (defect) =>
          applyRatingTreeSelection(defect, action)),
      };
    }

    case "select_rating_tree_nodes": {
      const candidateIds = new Set(action.candidateIds);
      return {
        ...state,
        defects: state.defects.map((defect) =>
          candidateIds.has(defect.candidate_id) &&
          defect.review_status !== "已忽略" &&
          defect.group_review_status !== "已确认"
            ? applyRatingTreeSelection(defect, action)
            : defect),
      };
    }

    case "ignore_defect": {
      return {
        ...state,
        defects: updateDefect(state.defects, action.candidateId, (defect) => ({
          ...defect,
          review_status: "已忽略",
          group_review_status: "待确认",
        })),
      };
    }

    case "restore_ignored_defect": {
      return {
        ...state,
        defects: updateDefect(state.defects, action.candidateId, (defect) => ({
          ...defect,
          review_status: defect.review_status === "已忽略" ? "待确认" : defect.review_status,
          group_review_status: "待确认",
        })),
      };
    }

    // ── 照片四原语 ────────────────────────────────────────────────
    // 归属（linked_defect_candidate_id）是唯一决定照片能否入库的事实；
    // photo_references 的 resolution 只是随之维护的 Word 来源证据。

    case "link_photo_to_defect": {
      const photo = state.photos.find((item) => item.candidate_id === action.photoCandidateId);
      const defect = state.defects.find((item) => item.candidate_id === action.defectCandidateId);
      if (!photo || !defect) return state;
      const previousOwner = photo.linked_defect_candidate_id;
      return {
        ...state,
        defects: invalidateDefectGroups(
          updateDefect(state.defects, action.defectCandidateId, (item) => ({
            ...item,
            photo_references: item.photo_references.map((reference) =>
              reference.photo_number === photo.photo_number
                ? {
                    ...reference,
                    resolution: "matched" as const,
                    photo_candidate_id: photo.candidate_id,
                    resolved_defect_candidate_id: item.candidate_id,
                  }
                : reference,
            ),
          })),
          [previousOwner, action.defectCandidateId],
        ),
        photos: updatePhoto(state.photos, photo.candidate_id, (item) => ({
          ...item,
          linked_defect_candidate_id: action.defectCandidateId,
        })),
      };
    }

    case "unlink_photo_from_defect": {
      const photo = state.photos.find((item) => item.candidate_id === action.photoCandidateId);
      if (!photo) return state;
      const owner = photo.linked_defect_candidate_id;
      return {
        ...state,
        defects: invalidateDefectGroups(
          owner
            ? updateDefect(state.defects, owner, (item) => ({
                ...item,
                photo_references: item.photo_references.map((reference) =>
                  reference.photo_candidate_id === photo.candidate_id ||
                  reference.photo_number === photo.photo_number
                    ? {
                        ...reference,
                        resolution: "pending" as const,
                        photo_candidate_id: null,
                        resolved_defect_candidate_id: null,
                      }
                    : reference,
                ),
              }))
            : state.defects,
          [owner],
        ),
        photos: updatePhoto(state.photos, photo.candidate_id, (item) => ({
          ...item,
          linked_defect_candidate_id: null,
        })),
      };
    }

    case "set_photo_reference_missing": {
      const defect = state.defects.find((item) => item.candidate_id === action.defectCandidateId);
      const reference = defect?.photo_references.find(
        (item) => item.photo_number === action.photoNumber,
      );
      if (!defect || !reference) return state;
      // 这张图明明在本次导入里（只是没挂上）时，"原报告缺图"就是假话。
      if (action.missing && state.photos.some((item) => item.photo_number === action.photoNumber)) {
        return state;
      }
      return {
        ...state,
        defects: updateDefect(state.defects, action.defectCandidateId, (item) => ({
          ...item,
          group_review_status: "待确认",
          photo_references: item.photo_references.map((itemReference) =>
            itemReference.photo_number === action.photoNumber
              ? {
                  ...itemReference,
                  resolution: action.missing ? ("missing" as const) : ("pending" as const),
                  photo_candidate_id: null,
                  resolved_defect_candidate_id: null,
                }
              : itemReference,
          ),
        })),
      };
    }

    case "remove_photo_reference": {
      const defect = state.defects.find((item) => item.candidate_id === action.defectCandidateId);
      if (!defect) return state;
      if (!defect.photo_references.some((item) => item.photo_number === action.photoNumber)) {
        return state;
      }
      // 这个编号的图还挂在本病害上时，摘掉引用并不会让卡片消失（照片本身照样成卡），
      // 只会让一张没人认领的图继续印进报告。界面因此只在缺图卡上给这个动作：
      // 要先"删除照片"把图退回未归属，再来划掉这句话。
      const stillLinked = state.photos.some(
        (item) =>
          item.linked_defect_candidate_id === action.defectCandidateId &&
          item.photo_number === action.photoNumber,
      );
      if (stillLinked) return state;
      return {
        ...state,
        defects: updateDefect(state.defects, action.defectCandidateId, (item) => ({
          ...item,
          group_review_status: "待确认",
          photo_references: item.photo_references.filter(
            (reference) => reference.photo_number !== action.photoNumber,
          ),
        })),
      };
    }

    case "add_photo": {
      const { photo } = action;
      if (!photo.linked_defect_candidate_id) return state;
      if (!state.defects.some((item) => item.candidate_id === photo.linked_defect_candidate_id)) {
        return state;
      }
      // 上传的照片没有对应的 Word 引用条目，因此不碰 photo_references：
      // Word 没承诺过这张图，它也就不该顶替任何编号。
      const photos = state.photos.some((item) => item.candidate_id === photo.candidate_id)
        ? state.photos.map((item) => (item.candidate_id === photo.candidate_id ? photo : item))
        : [...state.photos, photo];
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos,
      };
    }

    case "remove_photo": {
      const photo = state.photos.find((item) => item.candidate_id === action.photoCandidateId);
      if (!photo) return state;
      return {
        ...state,
        defects: invalidateDefectGroups(state.defects, [photo.linked_defect_candidate_id]),
        photos: state.photos.filter((item) => item.candidate_id !== action.photoCandidateId),
      };
    }

    case "confirm_defect_groups": {
      const selectedIds = new Set(action.candidateIds);
      const defects = state.defects.map((defect) => {
        if (!selectedIds.has(defect.candidate_id) || defect.review_status === "已忽略") {
          return defect;
        }
        const photoReferences = defect.photo_references.map((reference) => {
          if (reference.resolution !== "pending") return reference;
          const candidates = state.photos.filter(
            (photo) =>
              photo.photo_number === reference.photo_number &&
              photo.linked_defect_candidate_id === defect.candidate_id &&
              Boolean(photo.extracted_file.archive_relative_path),
          );
          if (candidates.length !== 1) return reference;
          const photo = candidates[0];
          return {
            ...reference,
            resolution: "matched" as const,
            photo_candidate_id: photo.candidate_id,
            resolved_defect_candidate_id: defect.candidate_id,
          };
        });
        return {
          ...defect,
          photo_references: photoReferences,
          group_review_status: "已确认" as const,
          review_status: defect.review_status === "待确认"
            ? "已确认" as const
            : defect.review_status,
          // 确认就是这些警告要的那次"人工确认"，答复过了就不该继续挂着。
          warnings: defect.warnings.filter(
            (warning) => !isHumanAcknowledgeableWarning(warning.code),
          ),
        };
      });
      return {
        ...state,
        defects,
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
