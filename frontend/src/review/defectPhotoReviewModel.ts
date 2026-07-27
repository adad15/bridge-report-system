import type { AssessmentIssue } from "../api/assessmentApi";
import type { StandardDefectCatalog, StandardDefectIndicator } from "../api/standardsApi";
import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
} from "../contracts/annualInspection";

export type DefectReviewProblemCategory = "component" | "defect_type" | "scale" | "photo" | "other";
export type DefectReviewFilter = "all" | "batchable" | "needs_attention" | "confirmed";

export interface DefectReviewProblem {
  code: string;
  category: DefectReviewProblemCategory;
  message: string;
}

export interface DefectReviewRow {
  candidateId: string;
  defect: DefectCandidate;
  photos: PhotoCandidate[];
  problems: DefectReviewProblem[];
  batchEligible: boolean;
  status: "batchable" | "needs_attention" | "confirmed" | "ignored";
  indicator: StandardDefectIndicator | null;
  suggestedIndicator: StandardDefectIndicator | null;
}

export interface DefectPhotoReviewSummary {
  all: number;
  batchable: number;
  needs_attention: number;
  confirmed: number;
}

export interface DefectPhotoReviewModel {
  rows: DefectReviewRow[];
  summary: DefectPhotoReviewSummary;
  safeCandidateIds: Set<string>;
}

export interface DefectPhotoReviewModelInput {
  draft: BridgeAnnualInspectionData;
  defectCatalogs: StandardDefectCatalog[];
  assessmentIssues: AssessmentIssue[];
  filter?: DefectReviewFilter;
  problemCategory?: DefectReviewProblemCategory | null;
  search?: string;
}

interface CatalogEntry {
  indicator: StandardDefectIndicator;
  applicableComponentIds: Set<string>;
}

function catalogEntries(catalogs: StandardDefectCatalog[]): CatalogEntry[] {
  return catalogs.flatMap((catalog) =>
    catalog.indicators.map((indicator) => ({
      indicator,
      applicableComponentIds: new Set(catalog.applicable_component_ids),
    })),
  );
}

function addProblem(
  problems: DefectReviewProblem[],
  code: string,
  category: DefectReviewProblemCategory,
  message: string,
): void {
  if (!problems.some((problem) => problem.code === code)) {
    problems.push({ code, category, message });
  }
}

function photosForDefect(
  draft: BridgeAnnualInspectionData,
  candidateId: string,
): PhotoCandidate[] {
  return draft.photos.filter(
    (photo) => photo.linked_defect_candidate_id === candidateId,
  );
}

function exactIndicatorSuggestion(
  defect: DefectCandidate,
  entries: CatalogEntry[],
): StandardDefectIndicator | null {
  if (!defect.standard_component_category_id) return null;
  const matches = entries.filter(
    ({ indicator, applicableComponentIds }) =>
      indicator.name === defect.defect_type &&
      applicableComponentIds.has(defect.standard_component_category_id!),
  );
  return matches.length === 1 ? matches[0].indicator : null;
}

function analyzeDefect(
  draft: BridgeAnnualInspectionData,
  defect: DefectCandidate,
  entries: CatalogEntry[],
  assessmentIssues: AssessmentIssue[],
  repeatedPhotoNumbers: Set<string>,
): DefectReviewRow {
  const problems: DefectReviewProblem[] = [];
  const photos = photosForDefect(draft, defect.candidate_id);
  const indicatorEntry = entries.find(
    ({ indicator }) => indicator.id === defect.standard_defect_indicator_id,
  );

  if (!defect.bridge_component_id || !defect.standard_component_category_id) {
    addProblem(problems, "component_required", "component", "尚未选择实际构件。");
  }
  if (!defect.standard_defect_indicator_id) {
    addProblem(problems, "indicator_required", "defect_type", "尚未选择规范病害。");
  } else if (!indicatorEntry) {
    addProblem(problems, "indicator_unknown", "defect_type", "规范病害已不存在。");
  } else if (
    !defect.standard_component_category_id ||
    !indicatorEntry.applicableComponentIds.has(defect.standard_component_category_id)
  ) {
    addProblem(problems, "indicator_not_applicable", "defect_type", "规范病害不适用于当前构件。");
  }
  if (
    indicatorEntry &&
    (defect.defect_scale === null ||
      defect.defect_scale === undefined ||
      !indicatorEntry.indicator.allowed_scales.includes(defect.defect_scale))
  ) {
    addProblem(problems, "scale_not_allowed", "scale", "病害标度不在规范允许范围内。");
  }

  for (const reference of defect.photo_references) {
    if (repeatedPhotoNumbers.has(reference.photo_number)) {
      addProblem(problems, "photo_number_conflict", "photo", `照片编号 ${reference.photo_number} 被多条病害引用。`);
    }
    if (reference.resolution === "pending") {
      addProblem(problems, "photo_reference_pending", "photo", `照片编号 ${reference.photo_number} 尚未核对。`);
      continue;
    }
    if (reference.resolution === "matched") {
      const photo = draft.photos.find(
        (item) => item.candidate_id === reference.photo_candidate_id,
      );
      if (
        !photo ||
        photo.linked_defect_candidate_id !== defect.candidate_id ||
        photo.match_status !== "已确认" ||
        !photo.extracted_file.archive_relative_path
      ) {
        addProblem(problems, "photo_match_invalid", "photo", `照片编号 ${reference.photo_number} 的实际关联未确认。`);
      }
    }
  }

  for (const warning of defect.warnings) {
    addProblem(problems, warning.code, "other", warning.message);
  }
  for (const issue of assessmentIssues) {
    if (issue.entity_type === "defect" && issue.entity_id === defect.candidate_id) {
      const category: DefectReviewProblemCategory =
        issue.field_path === "defect_scale"
          ? "scale"
          : issue.field_path === "defect_type" ||
              issue.field_path === "standard_defect_indicator_id"
            ? "defect_type"
            : issue.field_path.startsWith("photo")
              ? "photo"
              : "other";
      addProblem(problems, issue.code, category, issue.message);
    }
  }
  for (const error of draft.errors) {
    if (error.target_candidate_id === defect.candidate_id) {
      addProblem(problems, error.code, "other", error.message);
    }
  }

  const ignored = defect.review_status === "已忽略";
  const confirmed = !ignored && defect.group_review_status === "已确认" && problems.length === 0;
  const batchEligible = !ignored && !confirmed && problems.length === 0;
  return {
    candidateId: defect.candidate_id,
    defect,
    photos,
    problems,
    batchEligible,
    status: ignored
      ? "ignored"
      : confirmed
        ? "confirmed"
        : batchEligible
          ? "batchable"
          : "needs_attention",
    indicator: indicatorEntry?.indicator ?? null,
    suggestedIndicator: defect.standard_defect_indicator_id
      ? null
      : exactIndicatorSuggestion(defect, entries),
  };
}

export function buildDefectPhotoReviewModel(
  input: DefectPhotoReviewModelInput,
): DefectPhotoReviewModel {
  const entries = catalogEntries(input.defectCatalogs);
  const photoNumberCounts = new Map<string, number>();
  for (const defect of input.draft.defects) {
    for (const reference of defect.photo_references) {
      photoNumberCounts.set(
        reference.photo_number,
        (photoNumberCounts.get(reference.photo_number) ?? 0) + 1,
      );
    }
  }
  const repeatedPhotoNumbers = new Set(
    [...photoNumberCounts.entries()]
      .filter(([, count]) => count > 1)
      .map(([number]) => number),
  );
  const allRows = input.draft.defects.map((defect) =>
    analyzeDefect(
      input.draft,
      defect,
      entries,
      input.assessmentIssues,
      repeatedPhotoNumbers,
    ),
  );
  const summary = {
    all: allRows.length,
    batchable: allRows.filter((row) => row.status === "batchable").length,
    needs_attention: allRows.filter((row) => row.status === "needs_attention").length,
    confirmed: allRows.filter((row) => row.status === "confirmed").length,
  };
  const search = input.search?.trim().toLocaleLowerCase() ?? "";
  const rows = allRows.filter((row) => {
    if (input.filter && input.filter !== "all") {
      if (input.filter === "batchable" && row.status !== "batchable") return false;
      if (input.filter === "needs_attention" && row.status !== "needs_attention") return false;
      if (input.filter === "confirmed" && row.status !== "confirmed") return false;
    }
    if (
      input.problemCategory &&
      !row.problems.some((problem) => problem.category === input.problemCategory)
    ) {
      return false;
    }
    if (!search) return true;
    const haystack = [
      row.defect.component_number,
      row.defect.defect_location,
      row.defect.defect_type,
      ...row.defect.photo_references.map((reference) => reference.photo_number),
    ].join(" ").toLocaleLowerCase();
    return haystack.includes(search);
  });
  return {
    rows,
    summary,
    safeCandidateIds: new Set(
      allRows.filter((row) => row.batchEligible).map((row) => row.candidateId),
    ),
  };
}
