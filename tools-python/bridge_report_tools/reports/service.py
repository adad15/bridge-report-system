"""报告相关的服务层请求/响应模型。

C++ 后端把待校验的模板放到受控暂存位置，再把路径发过来。Python 不连数据库，也不
自己决定模板放哪——只负责"这份文件合不合契约"这一件事（设计 §6 的职责边界）。
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator

from bridge_report_tools.reports.contract import (
    CONTRACT_PERIODIC_INSPECTION_V1,
    get_contract,
)
from bridge_report_tools.reports.docx_builder import build_report
from bridge_report_tools.reports.errors import ReportBuildError
from bridge_report_tools.reports.field_updater import select_updater
from bridge_report_tools.reports.output_validator import (
    OutputValidationResult,
    validate_output,
)
from bridge_report_tools.reports.report_context import ReportContext
from bridge_report_tools.reports.template_validator import (
    TemplateConfig,
    TemplateValidationResult,
    validate_template,
)


class ReportModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class TemplateValidationRequest(ReportModel):
    template_path: Path
    contract_type: str = CONTRACT_PERIODIC_INSPECTION_V1
    #: 键为 内容块 或 内容块:结构部位，例如 DEFECT_TABLES:SUPERSTRUCTURE。
    table_number_formats: dict[str, str] = Field(default_factory=dict)
    required_personnel_roles: list[str] = Field(default_factory=list)

    @field_validator("template_path")
    @classmethod
    def require_docx_path(cls, value: Path) -> Path:
        if value.suffix.lower() != ".docx":
            raise ValueError("template_path must point to a .docx file")
        return value

    def to_config(self) -> TemplateConfig:
        return TemplateConfig(
            table_number_formats=dict(self.table_number_formats),
            required_personnel_roles=list(self.required_personnel_roles),
        )


class TemplateIssueModel(ReportModel):
    code: str
    message: str
    severity: Literal["error", "warning"]
    location: str | None = None


class TemplateValidationResponse(ReportModel):
    status: Literal["valid", "invalid"]
    issues: list[TemplateIssueModel]
    #: 锚点在文档中的真实顺序，供管理界面展示"查看锚点及文档顺序"（设计 §21.1）。
    anchors_in_document_order: list[str]
    placeholders_used: list[str]
    fields_used: list[str]

    @classmethod
    def from_result(cls, result: TemplateValidationResult) -> "TemplateValidationResponse":
        return cls(
            status=result.status,
            issues=[TemplateIssueModel(**issue.to_json()) for issue in result.issues],
            anchors_in_document_order=result.anchors_in_document_order,
            placeholders_used=result.placeholders_used,
            fields_used=result.fields_used,
        )


def validate_template_request(
    request: TemplateValidationRequest,
) -> TemplateValidationResponse:
    result = validate_template(
        request.template_path,
        request.to_config(),
        contract_type=request.contract_type,
    )
    return TemplateValidationResponse.from_result(result)


# ---------------------------------------------------------------------------
# 生成任务的三个阶段（设计 §17.2 的状态机）
#
# 拆成三个接口而不是一个 /reports/generate：C++ 拿着状态机，装配、刷域、校验各是一
# 个可见的阶段，界面才说得出"正在刷域"还是"正在装配"。刷域那一步还要排全局队列，
# 界面得能显示"等待字段更新"而不是看起来卡死（§17.3）。
#
# 大块数据一律走文件路径，不塞进请求体：真实上下文有近三百条病害和四百多张照片的
# 引用，几兆的 JSON 在 HTTP 里来回抄没有意义。C++ 本来就要在任务临时目录里落盘。
# ---------------------------------------------------------------------------


class ReportAssembleRequest(ReportModel):
    template_path: Path
    #: ReportContext 的 JSON，由 C++ 落在任务临时目录里。
    context_path: Path
    #: 归档根目录；上下文里的照片路径都是相对它的。
    archive_root: Path
    output_path: Path

    @field_validator("template_path", "output_path")
    @classmethod
    def require_docx_path(cls, value: Path) -> Path:
        if value.suffix.lower() != ".docx":
            raise ValueError("path must point to a .docx file")
        return value


class ReportAssembleResponse(ReportModel):
    output_path: Path
    #: 实际装配了的内容块，按文档顺序。最终校验要拿它对账（§20 第 4 条）。
    blocks_rendered: list[str]
    #: 模板里出现、但上下文没给取值的标量占位符。正常为空。
    missing_placeholders: list[str]
    elapsed_seconds: float


class FieldUpdateRequest(ReportModel):
    input_path: Path
    output_path: Path
    #: 单次更新的上限。排队等待另算，不占这个额度。
    timeout_seconds: float = Field(default=900.0, gt=0)


class FieldUpdateResponse(ReportModel):
    updater: str
    output_path: Path
    page_count: int | None
    #: 真正在 Word/WPS 里的时长。
    elapsed_seconds: float
    #: 排队等待时长，与上面分开记（§25.3 的规模验收要分别取数）。
    queued_seconds: float


class OutputValidationRequest(ReportModel):
    docx_path: Path
    #: 本次任务的临时目录。成品必须落在它里面（§20 第 9 条）。
    job_directory: Path
    contract_type: str = CONTRACT_PERIODIC_INSPECTION_V1
    blocks_rendered: list[str] = Field(default_factory=list)


class OutputValidationResponse(ReportModel):
    status: Literal["valid", "invalid"]
    issues: list[TemplateIssueModel]
    section_count: int
    image_count: int

    @classmethod
    def from_result(cls, result: OutputValidationResult) -> "OutputValidationResponse":
        return cls(
            status=result.status,
            issues=[TemplateIssueModel(**issue.to_json()) for issue in result.issues],
            section_count=result.section_count,
            image_count=result.image_count,
        )


def assemble_report_request(request: ReportAssembleRequest) -> ReportAssembleResponse:
    if not request.context_path.is_file():
        raise ReportBuildError(
            code="report_context_missing",
            message=f"上下文文件不存在：{request.context_path}",
        )
    try:
        payload = json.loads(request.context_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ReportBuildError(
            code="report_context_unreadable",
            message=f"上下文文件读不出来：{exc}",
        ) from exc
    try:
        context = ReportContext(**payload)
    except ValidationError as exc:
        # 上下文对不上模型说明 C++ 侧的取数有问题，必须当场说清是哪一项，
        # 不能让它以一份缺字段的上下文继续装配（§5 没有依据就不输出）。
        raise ReportBuildError(
            code="report_context_invalid",
            message=f"上下文不符合 ReportContext：{exc}",
        ) from exc

    started = time.monotonic()
    result = build_report(
        request.template_path, context, request.output_path, request.archive_root
    )
    return ReportAssembleResponse(
        output_path=result.output_path,
        blocks_rendered=result.blocks_rendered,
        missing_placeholders=result.missing_placeholders,
        elapsed_seconds=time.monotonic() - started,
    )


def update_fields_request(request: FieldUpdateRequest) -> FieldUpdateResponse:
    updater = select_updater(timeout_seconds=request.timeout_seconds)
    outcome = updater.update_fields(request.input_path, request.output_path)
    return FieldUpdateResponse(
        updater=outcome.updater,
        output_path=outcome.output_path,
        page_count=outcome.page_count,
        elapsed_seconds=outcome.elapsed_seconds,
        queued_seconds=outcome.queued_seconds,
    )


def validate_output_request(request: OutputValidationRequest) -> OutputValidationResponse:
    result = validate_output(
        request.docx_path,
        request.job_directory,
        get_contract(request.contract_type),
        request.blocks_rendered,
    )
    return OutputValidationResponse.from_result(result)
