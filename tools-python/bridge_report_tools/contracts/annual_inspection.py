"""模块 03 的年度检测候选数据契约。

这里的模型是 Word 解析、C++ 保存候选 JSON、前端校对和后续确认入库之间的共同边界。
它描述的是“候选数据”，不是已经写入 PostgreSQL 的正式事实。
"""

from __future__ import annotations

from datetime import date, datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


ReviewStatus = Literal["待确认", "已确认", "已修改", "已忽略"]
DefectGroupReviewStatus = Literal["待确认", "已确认"]
ComparisonConfirmationStatus = Literal["待确认", "已确认", "已修改", "已拒绝"]
Severity = Literal["info", "warning", "error"]
StructurePart = Literal["全桥", "上部结构", "下部结构", "桥面系", "其他"]
SourceType = Literal["软件导出Word", "正式Word", "Excel病害表", "图片包", "接口同步", "JSON导入"]
FileRole = Literal["当前年度检测资料", "历史正式报告", "历史基线资料", "修订资料"]
DataRole = Literal["当前年度", "历史基线", "修订版"]
BridgeMatchStatus = Literal["匹配", "不匹配", "待人工确认"]
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
    version: Literal["5.0"]
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
    value_type: Literal["single", "range"]
    value: float | None = None
    minimum_value: float | None = None
    maximum_value: float | None = None
    unit: str
    is_approximate: bool = False
    source_text: str

    @model_validator(mode="after")
    def validate_value_shape(self) -> Measurement:
        if self.value_type == "single":
            if self.value is None:
                raise ValueError("single measurement requires value")
            if self.minimum_value is not None or self.maximum_value is not None:
                raise ValueError("single measurement must not contain range endpoints")
            return self

        if self.value is not None:
            raise ValueError("range measurement must not contain value")
        if self.minimum_value is None or self.maximum_value is None:
            raise ValueError("range measurement requires both endpoints")
        if self.minimum_value > self.maximum_value:
            raise ValueError("range measurement minimum must not exceed maximum")
        return self


class PhotoReference(ContractModel):
    """病害对一张照片的引用，以及人工核对结论。

    `photo_number` 只是 Word 导入路的匹配键：Word 病害表的「照片编号」列和图片题注
    靠这个字符串对上，除它之外没有别的线索。来源软件导入没有这个概念——照片用外键
    直接绑在病害上（`images.ForeignKey`），所以编号为空，配对一律走
    `photo_candidate_id`。

    唯一的例外是 `pending` 和 `missing`：这两种状态下照片实体还不存在，
    `photo_candidate_id` 必为空，编号是这条引用仅有的标识，不能省。

    放宽为可空没有提高契约版本：5.0 下合法的文档现在依然合法，只是新文档允许省略
    该字段。升版会让库里已存的 5.0 草稿在重新打开时被拒。
    """

    photo_number: str | None = None
    resolution: Literal["pending", "matched", "relinked", "missing", "unrelated"]
    photo_candidate_id: str | None = None
    resolved_defect_candidate_id: str | None = None
    review_note: str | None = None

    @model_validator(mode="after")
    def validate_resolution_targets(self) -> PhotoReference:
        has_photo = self.photo_candidate_id is not None
        has_defect = self.resolved_defect_candidate_id is not None
        valid_targets = {
            "pending": not has_photo and not has_defect,
            "matched": has_photo and has_defect,
            "relinked": has_photo and has_defect,
            "missing": not has_photo and not has_defect,
            "unrelated": has_photo and not has_defect,
        }
        if not valid_targets[self.resolution]:
            raise ValueError(
                f"photo reference targets are invalid for resolution {self.resolution}"
            )
        if self.photo_number is not None and not self.photo_number.strip():
            raise ValueError("photo_number must be omitted rather than left blank")
        if self.resolution in ("pending", "missing") and self.photo_number is None:
            raise ValueError(
                f"photo reference with resolution {self.resolution} has no photo to point at,"
                " so photo_number is required"
            )
        return self


class DefectCandidate(ContractModel):
    """第二章结构病害检查表中的一条病害候选记录。"""

    candidate_id: str
    source_structure_part: StructurePart | None = None
    component_name: str
    component_number: str | None = None
    defect_type: str
    defect_location: str
    defect_scale: int | None = Field(default=None, gt=0, strict=True)
    defect_description: str
    quantity_text: str | None = None
    measurement_text: str | None = None
    measurements: list[Measurement]
    #: 来源软件原始评定分组和指标身份。ID 来自 judgeTreeId / judgeIndexId，编号来自
    #: judgeTree.chapterNum / judgeIndex.tableNum。它们与派生的 H21 指标严格分开，重绑与
    #: 草稿保存都不得清空。
    source_defect_group_id: str | None = None
    source_defect_group_number: str | None = None
    source_defect_indicator_id: str | None = None
    source_defect_indicator_number: str | None = None
    photo_references: list[PhotoReference]
    group_review_status: DefectGroupReviewStatus
    severity: Severity | None = None
    remark: str | None = None
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    warnings: list[WarningItem]

    @field_validator("photo_references")
    @classmethod
    def require_unique_photo_references(
        cls, value: list[PhotoReference]
    ) -> list[PhotoReference]:
        # 只比对有编号的引用：来源软件那条路整条链都没有编号，若把 None 也算进去，
        # 一条病害挂两张图就会被误判成重复。
        photo_numbers = [
            reference.photo_number for reference in value if reference.photo_number is not None
        ]
        if len(photo_numbers) != len(set(photo_numbers)):
            raise ValueError("photo_references.photo_number must be unique")
        # 没有编号时，photo_candidate_id 才是这条引用的身份，同样不许重复。
        candidate_ids = [
            reference.photo_candidate_id
            for reference in value
            if reference.photo_candidate_id is not None
        ]
        if len(candidate_ids) != len(set(candidate_ids)):
            raise ValueError("photo_references.photo_candidate_id must be unique")
        return value

    @field_validator(
        "source_defect_group_id",
        "source_defect_group_number",
        "source_defect_indicator_id",
        "source_defect_indicator_number",
    )
    @classmethod
    def require_non_empty_source_reference(cls, value: str | None) -> str | None:
        if value is not None and not value.strip():
            raise ValueError("source indicator references must not be blank")
        return value


class ExtractedPhotoFile(ContractModel):
    temporary_file_name: str
    original_caption: str | None = None
    archive_relative_path: str | None = None


class PhotoCandidate(ContractModel):
    candidate_id: str
    #: Word 路是图片题注里的编号；来源软件路没有编号，为空。见 PhotoReference 的说明。
    photo_number: str | None = None
    linked_defect_candidate_id: str | None = None
    extracted_file: ExtractedPhotoFile
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
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
