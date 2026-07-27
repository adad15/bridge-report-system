import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import type { AttentionItem } from "./grouping";

export type DefectTargetField =
  | "component_match"
  | "component_name"
  | "component_number"
  | "defect_location"
  | "defect_type"
  | "defect_scale"
  | "quantity_text"
  | "photo_references"
  | "measurement_text";

const WARNING_FIELD_MAP: Record<string, DefectTargetField> = {
  defect_component_match_required: "component_match",
  defect_component_match_ambiguous: "component_match",
  defect_component_assignment_invalid: "component_match",
  defect_scale_invalid: "defect_scale",
  measurement_parse_low_confidence: "measurement_text",
  photo_number_unmatched: "photo_references",
  defect_component_missing: "component_name",
  component_alias_missing: "component_number",
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
  if (item.kind === "rating") return "系统技术状况评定";
  return "导入记录";
}
