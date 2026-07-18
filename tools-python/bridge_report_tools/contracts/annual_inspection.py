"""模块 03 的年度检测候选数据契约。

这里的模型是 Word 解析、C++ 保存候选 JSON、前端校对和后续确认入库之间的共同边界。
它描述的是“候选数据”，不是已经写入 PostgreSQL 的正式事实。
"""

from __future__ import annotations

from datetime import date, datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


ReviewStatus = Literal["待确认", "已确认", "已修改", "已忽略"]
ScoreValidationStatus = Literal["一致", "不一致", "无法复算", "人工接受Word值", "人工采用复算值"]
DefectGroupReviewStatus = Literal["待确认", "已确认"]
ComparisonConfirmationStatus = Literal["待确认", "已确认", "已修改", "已拒绝"]
Severity = Literal["info", "warning", "error"]
StructurePart = Literal["全桥", "上部结构", "下部结构", "桥面系", "其他"]
RatingStructurePart = Literal["上部结构", "下部结构", "桥面系"]
SourceType = Literal["软件导出Word", "正式Word", "Excel病害表", "图片包", "接口同步", "JSON导入"]
FileRole = Literal["当前年度检测资料", "历史正式报告", "历史基线资料", "修订资料"]
DataRole = Literal["当前年度", "历史基线", "修订版"]
BridgeMatchStatus = Literal["匹配", "不匹配", "待人工确认"]
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
    """所有契约模型默认禁止额外字段，避免解析器悄悄输出未评审的数据。"""

    model_config = ConfigDict(extra="forbid")


class WarningItem(ContractModel):
    code: str
    message: str
    severity: Severity
    target_candidate_id: str | None = None


class SourceRef(ContractModel):
    """候选对象的来源证据，用于人工校对时回到 Word 表格行或段落。"""

    source_type: Literal["word", "manual"] = "word"
    chapter: str | None = None
    table_title: str | None = None
    table_index: int | None = Field(default=None, ge=0)
    row_index: int | None = Field(default=None, ge=0)
    column_name: str | None = None
    raw_row_text: str | None = None
    photo_area_caption: str | None = None
    file_role: FileRole | None = None
    paragraph_index: int | None = Field(default=None, ge=0)


class ContractInfo(ContractModel):
    name: Literal["BridgeAnnualInspectionData"]
    version: Literal["2.0"]
    generated_at: datetime
    producer: str
    parser_name: str
    parser_version: str


class ImportContext(ContractModel):
    source_type: SourceType
    file_role: FileRole
    archived_file_system_number: str
    import_record_system_number: str


class BridgeCheck(ContractModel):
    selected_bridge_system_number: str
    extracted_bridge_name: str | None = None
    match_status: BridgeMatchStatus
    warnings: list[WarningItem]


class InspectionInfo(ContractModel):
    inspection_year: int = Field(ge=1900, le=2200)
    inspection_date: date
    report_number: str
    project_name: str
    data_role: DataRole


class Measurement(ContractModel):
    dimension_type: str
    value: float
    unit: str
    source_text: str


class DefectCandidate(ContractModel):
    """第二章结构病害检查表中的一条病害候选记录。"""

    candidate_id: str
    source_structure_part: StructurePart | None = None
    component_name: str
    component_number: str | None = None
    bridge_component_id: str | None = None
    standard_component_category_id: str | None = None
    resolved_structure_part: StructurePart | None = None
    defect_type: str
    defect_location: str
    defect_scale: int | None = Field(default=None, gt=0, strict=True)
    defect_description: str
    quantity_text: str | None = None
    measurement_text: str | None = None
    measurements: list[Measurement]
    photo_numbers: list[str]
    group_review_status: DefectGroupReviewStatus
    confirmed_missing_photo_numbers: list[str] = Field(
        json_schema_extra={"uniqueItems": True}
    )
    severity: Severity | None = None
    remark: str | None = None
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    review_note: str | None = None
    warnings: list[WarningItem]

    @field_validator("confirmed_missing_photo_numbers")
    @classmethod
    def require_unique_missing_numbers(cls, value: list[str]) -> list[str]:
        if len(value) != len(set(value)):
            raise ValueError("confirmed_missing_photo_numbers must be unique")
        return value


class ExtractedPhotoFile(ContractModel):
    temporary_file_name: str
    original_caption: str | None = None
    archive_relative_path: str | None = None


class PhotoCandidate(ContractModel):
    candidate_id: str
    photo_number: str
    linked_defect_candidate_id: str | None = None
    extracted_file: ExtractedPhotoFile
    match_status: PhotoMatchStatus
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    warnings: list[WarningItem]


class OverallRating(ContractModel):
    total_score: float = Field(ge=0, le=100)
    overall_grade: str
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class StructurePartRating(ContractModel):
    structure_part: RatingStructurePart
    structure_score: float = Field(ge=0, le=100)
    weight: float = Field(ge=0, le=1)
    grade: str
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class EvaluationScoreRow(ContractModel):
    component_count: int = Field(ge=0)
    component_score: float = Field(ge=0, le=100)


class EvaluationPartRating(ContractModel):
    structure_part: RatingStructurePart
    category_no: int = Field(ge=1)
    evaluation_part: str
    part_score: float = Field(ge=0, le=100)
    score_rows: list[EvaluationScoreRow]
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class ComponentRef(ContractModel):
    """第二章病害表中的具体构件标识，与病害候选的构件字段同源。"""

    structure_part: StructurePart
    component_name: str
    component_alias: str | None = None


class ComponentScoreCalculationDetails(ContractModel):
    """JTG/T H21-2011 第 4.1.1 条复算证据：降序扣分序列与展示舍入位数。"""

    standard: Literal["JTG/T H21-2011 4.1.1"]
    ordered_deductions: list[float]
    rounding_scale: Literal[2]


class ComponentRatingCandidate(ContractModel):
    """第二章具体构件评分候选，保存 Word 来源分、规范复算分和最终确认分。

    `一致` 可预填来源分；`不一致` 与 `无法复算` 必须留空最终分，
    由模块 05 人工显式选择并填写原因后才允许赋值。
    """

    candidate_id: str
    component_ref: ComponentRef
    source_score: float | None = Field(default=None, ge=0, le=100)
    calculated_score: float | None = Field(default=None, ge=0, le=100)
    confirmed_score: float | None = Field(default=None, ge=0, le=100)
    score_validation_status: ScoreValidationStatus
    score_resolution_reason: str | None = None
    deduction_defect_candidate_ids: list[str]
    calculation_details: ComponentScoreCalculationDetails | None = None
    review_status: ReviewStatus
    warnings: list[WarningItem]

    @model_validator(mode="after")
    def enforce_resolution_invariants(self) -> "ComponentRatingCandidate":
        status = self.score_validation_status
        if status in ("不一致", "无法复算"):
            if self.confirmed_score is not None:
                raise ValueError(
                    "score_validation_status 为不一致或无法复算时 confirmed_score 必须为空"
                )
            if self.score_resolution_reason is not None:
                raise ValueError(
                    "score_validation_status 为不一致或无法复算时 score_resolution_reason 必须为空"
                )
        elif status in ("人工接受Word值", "人工采用复算值"):
            if self.confirmed_score is None:
                raise ValueError("人工选择最终分后 confirmed_score 不能为空")
            if self.score_resolution_reason is None or not self.score_resolution_reason.strip():
                raise ValueError("人工选择最终分必须填写 score_resolution_reason")
        else:
            if self.score_resolution_reason is not None:
                raise ValueError("score_validation_status 为一致时 score_resolution_reason 必须为空")
        return self


class Ratings(ContractModel):
    """第四章总体技术状况评定表与第二章构件评分的候选数据。

    等级只放在整体和结构分部层级，评价部件只保存评分，不保存 grade。
    `component_ratings` 保存第二章具体构件的评分双值校验候选。
    """

    overall: OverallRating
    structure_parts: list[StructurePartRating]
    evaluation_parts: list[EvaluationPartRating]
    component_ratings: list[ComponentRatingCandidate]
    warnings: list[WarningItem]


class ComparisonMatchBasis(ContractModel):
    same_component: bool
    same_defect_type: bool
    location_similarity: float = Field(ge=0, le=1)
    measurement_change_detected: bool
    photo_number_related: bool


class ComparisonCandidate(ContractModel):
    """事实入库后生成的历史对比候选，不由 Word 解析器直接产生。"""

    candidate_id: str
    previous_defect_observation_system_number: str | None = None
    current_defect_observation_system_number: str | None = None
    comparison_type: ComparisonType
    match_basis: ComparisonMatchBasis | None = None
    change_summary: str | None = None
    confidence: float = Field(ge=0, le=1)
    confirmation_status: ComparisonConfirmationStatus
    review_note: str | None = None
    warnings: list[WarningItem]


class ReportTextCandidate(ContractModel):
    """正式报告文本抽取的预留扩展口，第一版不作为病害事实来源。"""

    candidate_id: str
    section_key: str
    section_title: str
    text: str
    usage: str | None = None
    source_ref: SourceRef | None = None
    review_status: ReviewStatus


class BridgeAnnualInspectionData(ContractModel):
    """一次导入任务的完整候选 JSON。"""

    contract: ContractInfo
    import_context: ImportContext
    bridge_check: BridgeCheck
    inspection: InspectionInfo
    defects: list[DefectCandidate]
    photos: list[PhotoCandidate]
    comparison_candidates: list[ComparisonCandidate]
    report_text_candidates: list[ReportTextCandidate]
    warnings: list[WarningItem]
    errors: list[WarningItem]
