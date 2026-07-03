from __future__ import annotations

from datetime import date, datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


ReviewStatus = Literal["待确认", "已确认", "已修改", "已忽略"]
ComparisonConfirmationStatus = Literal["待确认", "已确认", "已修改", "已拒绝"]
Severity = Literal["info", "warning", "error"]
StructurePart = Literal["全桥", "上部结构", "下部结构", "桥面系", "其他"]
SourceType = Literal["软件导出Word", "正式Word", "Excel病害表", "图片包", "接口同步", "JSON导入"]
DataRole = Literal["当前年度", "历史基线", "修订版"]
PhotoMatchStatus = Literal["高置信候选", "待校对", "已确认", "未关联", "已忽略"]
ComparisonType = Literal[
    "原病害无明显变化",
    "原病害发展",
    "原病害减轻",
    "原病害修复",
    "新增病害",
    "原病害未见",
    "无法判断",
]


class ContractModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class DiagnosticIssue(ContractModel):
    code: str
    message: str
    severity: Severity
    path: str | None = None


class SourceRef(ContractModel):
    source_document_id: str | None = None
    source_document_name: str | None = None
    source_chapter: str | None = None
    source_table: str | None = None
    source_row: int | None = Field(default=None, ge=0)
    raw_text: str | None = None


class ContractInfo(ContractModel):
    name: Literal["BridgeAnnualInspectionData"]
    version: str
    description: str | None = None


class ImportContext(ContractModel):
    source_type: SourceType
    data_scope: str
    source_document_name: str
    imported_at: datetime
    operator: str
    data_role: DataRole | None = None


class SourceDocument(ContractModel):
    source_document_id: str
    source_type: SourceType
    data_role: DataRole
    document_name: str
    imported_at: datetime | None = None


class BridgeCheck(ContractModel):
    bridge_name: str | None = None
    inspection_year: int = Field(ge=1900, le=2200)
    bridge_code: str | None = None
    route_name: str | None = None
    source_chapters: list[str] = Field(default_factory=list)
    selected_bridge_system_number: str | None = None
    extracted_bridge_name: str | None = None
    match_status: str | None = None
    warnings: list[DiagnosticIssue] = Field(default_factory=list)


class InspectionInfo(ContractModel):
    inspection_type: str
    inspection_year: int = Field(ge=1900, le=2200)
    inspection_date: date
    data_category: str
    report_source: SourceType
    report_number: str | None = None
    project_name: str | None = None
    data_role: DataRole | None = None


class Measurement(ContractModel):
    name: str
    value: float
    unit: str


class DefectCandidate(ContractModel):
    candidate_id: str
    observation_system_number: str | None = None
    source_chapter: str | None = None
    component_name: str
    location_text: str
    defect_type: str
    description: str
    measurement_text: str | None = None
    measurements: list[Measurement] = Field(default_factory=list)
    photo_numbers: list[str] = Field(default_factory=list)
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    source_ref: SourceRef | None = None
    review_note: str | None = None
    warnings: list[DiagnosticIssue] = Field(default_factory=list)


class PhotoCandidate(ContractModel):
    photo_id: str
    photo_number: str
    caption: str | None = None
    source_chapter: str | None = None
    linked_defect_candidate_id: str | None = None
    match_status: PhotoMatchStatus
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    source_ref: SourceRef | None = None
    photo_area_caption: str | None = None
    review_note: str | None = None
    warnings: list[DiagnosticIssue] = Field(default_factory=list)


class OverallRating(ContractModel):
    table_name: str
    total_score: float = Field(ge=0, le=100)
    overall_grade: str
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    source_ref: SourceRef | None = None


class StructurePartRating(ContractModel):
    structure_part: StructurePart
    structure_score: float = Field(ge=0, le=100)
    weight: float = Field(ge=0, le=1)
    grade: str
    confidence: float | None = Field(default=None, ge=0, le=1)
    review_status: ReviewStatus | None = None
    source_ref: SourceRef | None = None


class EvaluationScoreRow(ContractModel):
    item_name: str
    deduction: float | None = Field(default=None, ge=0)
    score: float | None = Field(default=None, ge=0, le=100)
    component_score: float | None = Field(default=None, ge=0, le=100)
    source_text: str | None = None


class EvaluationPartRating(ContractModel):
    evaluation_part: str
    part_score: float = Field(ge=0, le=100)
    score_rows: list[EvaluationScoreRow] = Field(default_factory=list)
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    source_ref: SourceRef | None = None


class Ratings(ContractModel):
    overall: OverallRating
    structure_parts: list[StructurePartRating]
    evaluation_parts: list[EvaluationPartRating]
    warnings: list[DiagnosticIssue] = Field(default_factory=list)


class ComparisonMatchBasis(ContractModel):
    same_component: bool
    same_defect_type: bool
    location_similarity: float = Field(ge=0, le=1)
    measurement_change_detected: bool
    photo_number_related: bool


class ComparisonCandidate(ContractModel):
    candidate_id: str
    previous_defect_observation_system_number: str | None = None
    current_defect_observation_system_number: str | None = None
    comparison_type: ComparisonType
    match_basis: ComparisonMatchBasis | None = None
    change_summary: str | None = None
    confidence: float = Field(ge=0, le=1)
    confirmation_status: ComparisonConfirmationStatus
    review_note: str | None = None
    warnings: list[DiagnosticIssue] = Field(default_factory=list)


class ReportTextCandidate(ContractModel):
    candidate_id: str
    source_chapter: str | None = None
    source_table: str | None = None
    text: str
    usage: str | None = None
    source_ref: SourceRef | None = None
    confidence: float | None = Field(default=None, ge=0, le=1)
    review_status: ReviewStatus | None = None


class BridgeAnnualInspectionData(ContractModel):
    contract: ContractInfo
    import_context: ImportContext
    source_documents: list[SourceDocument] = Field(default_factory=list)
    bridge_check: BridgeCheck
    inspection: InspectionInfo
    defects: list[DefectCandidate]
    photos: list[PhotoCandidate]
    ratings: Ratings
    comparison_candidates: list[ComparisonCandidate] = Field(default_factory=list)
    report_text_candidates: list[ReportTextCandidate] = Field(default_factory=list)
    warnings: list[DiagnosticIssue] = Field(default_factory=list)
    errors: list[DiagnosticIssue] = Field(default_factory=list)
