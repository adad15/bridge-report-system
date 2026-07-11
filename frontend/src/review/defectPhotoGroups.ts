import type { BridgeAnnualInspectionData, DefectCandidate, PhotoCandidate } from "../contracts/annualInspection";

export interface DefectPhotoGroup {
  defect: DefectCandidate;
  photos: PhotoCandidate[];
  missingPhotoNumbers: string[];
}

export interface DefectPhotoGroupConfirmation {
  ok: boolean;
  reasons: string[];
}

const REQUIRED_DEFECT_FIELDS: Array<keyof DefectCandidate> = [
  "structure_part",
  "component_name",
  "defect_location",
  "defect_type",
  "defect_description",
];

function isNonEmptyString(value: unknown): boolean {
  return typeof value === "string" && value.trim().length > 0;
}

function isPhotoResolved(photo: PhotoCandidate): boolean {
  return (
    (photo.review_status === "已确认" || photo.review_status === "已修改") &&
    photo.match_status === "已确认"
  );
}

function isUnlinkedPhotoHandled(photo: PhotoCandidate): boolean {
  return photo.review_status === "已忽略" || (photo.review_status === "已确认" && photo.match_status === "未关联");
}

function addReason(reasons: string[], reason: string): void {
  if (!reasons.includes(reason)) reasons.push(reason);
}

export function buildDefectPhotoGroup(
  data: BridgeAnnualInspectionData,
  candidateId: string
): DefectPhotoGroup | null {
  const defect = data.defects.find((item) => item.candidate_id === candidateId);
  if (!defect) return null;

  const photos = data.photos.filter((item) => item.linked_defect_candidate_id === candidateId);
  const candidateNumbers = new Set(data.photos.map((item) => item.photo_number));
  const missingPhotoNumbers = defect.photo_numbers.filter((number) => !candidateNumbers.has(number));

  return { defect, photos, missingPhotoNumbers };
}

export function canConfirmDefectPhotoGroup(
  data: BridgeAnnualInspectionData,
  candidateId: string
): DefectPhotoGroupConfirmation {
  const group = buildDefectPhotoGroup(data, candidateId);
  if (!group) return { ok: false, reasons: ["defect_not_found"] };

  const reasons: string[] = [];
  if (REQUIRED_DEFECT_FIELDS.some((field) => !isNonEmptyString(group.defect[field]))) {
    addReason(reasons, "defect_required_field_missing");
  }

  const hasBlockingError = group.defect.warnings.some((item) => item.severity === "error") ||
    data.errors.some((item) => item.target_candidate_id === candidateId);
  if (hasBlockingError) addReason(reasons, "defect_blocking_error");

  for (const photo of group.photos) {
    if (!isPhotoResolved(photo)) addReason(reasons, "photo_review_required");
    if (!photo.extracted_file.archive_relative_path?.trim()) addReason(reasons, "photo_archive_missing");
  }

  const referencedNumbers = new Set(group.defect.photo_numbers);
  const unresolvedReferencedCandidate = data.photos.some(
    (item) =>
      referencedNumbers.has(item.photo_number) &&
      item.linked_defect_candidate_id !== candidateId &&
      !isUnlinkedPhotoHandled(item)
  );
  if (unresolvedReferencedCandidate) addReason(reasons, "photo_link_unresolved");

  const acknowledged = new Set(group.defect.confirmed_missing_photo_numbers);
  if (group.missingPhotoNumbers.some((number) => !acknowledged.has(number))) {
    addReason(reasons, "missing_photo_confirmation_required");
  }

  return { ok: reasons.length === 0, reasons };
}
