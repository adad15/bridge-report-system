import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import type { AttentionItem } from "./grouping";

export type DefectTargetField =
  | "structure_part"
  | "component_name"
  | "component_alias"
  | "defect_location"
  | "defect_type"
  | "defect_scale"
  | "defect_deduction"
  | "quantity_text"
  | "photo_numbers"
  | "measurement_text";

const WARNING_FIELD_MAP: Record<string, DefectTargetField> = {
  defect_scale_invalid: "defect_scale",
  defect_deduction_invalid: "defect_deduction",
  measurement_parse_low_confidence: "measurement_text",
  photo_number_unmatched: "photo_numbers",
  defect_component_missing: "component_name",
  component_alias_missing: "component_alias",
  defect_location_missing: "defect_location",
  defect_type_missing: "defect_type",
};

export function defectFieldForWarning(code: string): DefectTargetField | undefined {
  return WARNING_FIELD_MAP[code];
}

function encoded(value: string): string {
  return encodeURIComponent(value);
}

export function reviewTargetId(
  kind: "defect" | "defect-field" | "photo" | "unlinked-photo" | "rating" | "unlinked-photos",
  candidateId: string,
  field?: string
): string {
  const suffix = field ? `-${encoded(field)}` : "";
  return `review-target-${kind}-${encoded(candidateId)}${suffix}`;
}

export function defectSequence(data: BridgeAnnualInspectionData, candidateId: string): number | null {
  const index = data.defects.findIndex((defect) => defect.candidate_id === candidateId);
  return index < 0 ? null : index + 1;
}

export function ratingLabel(data: BridgeAnnualInspectionData, candidateId: string): string {
  if (candidateId === "ratings.overall") return "全桥评分";
  const structure = /^ratings\.structure_parts\[(\d+)]$/.exec(candidateId);
  if (structure) {
    const item = data.ratings.structure_parts[Number(structure[1])];
    return item ? `${item.structure_part}评分` : "结构分部评分";
  }
  const evaluation = /^ratings\.evaluation_parts\[(\d+)]$/.exec(candidateId);
  if (evaluation) {
    const item = data.ratings.evaluation_parts[Number(evaluation[1])];
    return item ? `${item.evaluation_part}评分` : "评价部件评分";
  }
  const component = data.ratings.component_ratings.find((item) => item.candidate_id === candidateId);
  if (component) {
    return `构件评分 ${component.component_ref.component_alias ?? component.component_ref.component_name}`;
  }
  return "技术状况评分";
}

export function attentionObjectLabel(item: AttentionItem, data: BridgeAnnualInspectionData): string {
  if (item.kind === "defect") {
    const sequence = defectSequence(data, item.candidateId);
    return sequence === null ? "病害（目标已变化）" : `病害 ${sequence}`;
  }
  if (item.kind === "photo") {
    const photo = data.photos.find((candidate) => candidate.candidate_id === item.candidateId);
    if (!photo) return "照片（目标已变化）";
    if (!photo.linked_defect_candidate_id) return `未关联照片 · ${photo.photo_number}`;
    const sequence = defectSequence(data, photo.linked_defect_candidate_id);
    return sequence === null
      ? `照片 ${photo.photo_number}`
      : `病害 ${sequence} · 照片 ${photo.photo_number}`;
  }
  if (item.kind === "rating") return ratingLabel(data, item.candidateId);
  return "导入记录";
}
