"""模板契约校验器（设计 §7.2-§7.6、§23.1）。

输出既是 report_templates.validation_result_json 的内容，也是系统管理界面
"查看锚点及文档顺序"的数据源，因此不只报错，也要报出模板实际长什么样。
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field

from bridge_report_tools.reports.contract import (
    CONTRACT_PERIODIC_INSPECTION_V1,
    TABLE_COLUMNS,
    FIELDS_ALLOWED_ANYWHERE,
    FIELDS_ALLOWED_INSIDE_TOC,
    FIELDS_EXPLICITLY_REJECTED,
    STRUCTURE_PART_CODES,
    ReportContract,
    get_contract,
    numbering_key,
    personnel_placeholder,
)
from bridge_report_tools.reports.docx_scan import (
    LOCATION_BODY,
    AnchorHit,
    TemplateScan,
    scan_template,
)
from bridge_report_tools.reports.issues import TemplateIssue, error
from bridge_report_tools.reports.package_guard import (
    DEFAULT_LIMITS,
    PackageLimits,
    inspect_package,
)


NUMBER_TOKEN = "{n}"
BRACE_TOKEN_PATTERN = re.compile(r"\{[^{}]*\}")

PERSONNEL_PLACEHOLDER_PATTERN = re.compile(r"^personnel\.(?P<role>[A-Za-z0-9_]+)\.names$")


class TemplateConfig(BaseModel):
    """report_templates.contract_config_json 的形状。"""

    model_config = ConfigDict(extra="forbid")

    #: 内容块 -> 编号格式，例如 {"DEFECT_TABLES": "表 2-{n}"}。
    table_number_formats: dict[str, str] = Field(default_factory=dict)
    #: 本模板要求配置的人员角色代码。
    required_personnel_roles: list[str] = Field(default_factory=list)


@dataclass(frozen=True)
class TemplateValidationResult:
    status: Literal["valid", "invalid"]
    issues: list[TemplateIssue] = field(default_factory=list)
    anchors_in_document_order: list[str] = field(default_factory=list)
    placeholders_used: list[str] = field(default_factory=list)
    fields_used: list[str] = field(default_factory=list)

    @property
    def is_valid(self) -> bool:
        return self.status == "valid"

    def codes(self) -> list[str]:
        return [issue.code for issue in self.issues]

    def to_json(self) -> dict:
        return {
            "status": self.status,
            "issues": [issue.to_json() for issue in self.issues],
            "anchors_in_document_order": self.anchors_in_document_order,
            "placeholders_used": self.placeholders_used,
            "fields_used": self.fields_used,
        }


def _validate_anchors(
    contract: ReportContract, anchors: list[AnchorHit]
) -> tuple[list[TemplateIssue], dict[tuple[str, str | None], str]]:
    """校验锚点，并返回被接受的 (内容块, 结构部位) -> 位置。"""
    issues: list[TemplateIssue] = []
    seen: dict[tuple[str, str | None], str] = {}

    for hit in anchors:
        where = f"{hit.location}#{hit.paragraph_index}"
        if hit.block not in contract.all_anchors:
            issues.append(
                error(
                    "template_anchor_unknown",
                    f"未知内容锚点 {hit.raw}；契约 {contract.contract_type} 不认识它。",
                    where,
                )
            )
            continue
        if hit.location != LOCATION_BODY:
            issues.append(
                error(
                    "template_anchor_outside_body",
                    f"内容锚点 {hit.raw} 出现在 {hit.location}；锚点只能放在正文里。",
                    where,
                )
            )
            continue
        if not hit.owns_paragraph:
            issues.append(
                error(
                    "template_anchor_not_own_paragraph",
                    f"内容锚点 {hit.raw} 与其他文字同段（“{hit.paragraph_text}”）；"
                    "锚点必须独占一个段落，生成时整段替换。",
                    where,
                )
            )
            continue

        if contract.is_per_part(hit.block):
            if hit.part is None:
                issues.append(
                    error(
                        "template_anchor_part_required",
                        f"{hit.raw} 必须指明结构部位，写成 "
                        f"[[REPORT:{hit.block}:SUPERSTRUCTURE]] 这样的形式——"
                        "正式报告按上部结构、下部结构、桥面系各起一节。",
                        where,
                    )
                )
                continue
            if hit.part not in STRUCTURE_PART_CODES:
                issues.append(
                    error(
                        "template_anchor_part_unknown",
                        f"{hit.raw} 里的结构部位代码 {hit.part} 未知；"
                        f"可用值：{'、'.join(sorted(STRUCTURE_PART_CODES))}。",
                        where,
                    )
                )
                continue
        elif hit.part is not None:
            issues.append(
                error(
                    "template_anchor_part_not_allowed",
                    f"{hit.raw} 不按结构部位拆分，不能带 :{hit.part} 后缀。",
                    where,
                )
            )
            continue

        key = (hit.block, hit.part)
        if key in seen:
            issues.append(
                error(
                    "template_anchor_duplicated",
                    f"内容锚点 {hit.raw} 重复出现（另一处在 {seen[key]}）。",
                    where,
                )
            )
            continue
        seen[key] = where

    present_blocks = {block for block, _ in seen}
    for missing in sorted(contract.required_anchors - present_blocks):
        issues.append(
            error(
                "template_anchor_missing",
                f"模板缺少核心内容锚点 [[REPORT:{missing}]]。",
            )
        )

    issues.extend(_validate_part_coverage(contract, seen))
    return issues, seen


def _validate_part_coverage(
    contract: ReportContract, seen: dict[tuple[str, str | None], str]
) -> list[TemplateIssue]:
    """所有按部位拆分的内容块必须覆盖同一组结构部位。

    否则会出现"病害表分了上部/下部/桥面系，照片只放了上部"这种模板，生成时下部和
    桥面系的照片无处可去，只能被静默丢掉——正是设计反复要避免的那类错误输出。
    """
    coverage: dict[str, set[str]] = {}
    for block, part in seen:
        if contract.is_per_part(block) and part is not None:
            coverage.setdefault(block, set()).add(part)
    if len(coverage) <= 1:
        return []

    distinct = {frozenset(parts) for parts in coverage.values()}
    if len(distinct) == 1:
        return []

    detail = "；".join(
        f"{block} 覆盖 {'、'.join(sorted(parts))}" for block, parts in sorted(coverage.items())
    )
    return [
        error(
            "template_part_coverage_mismatch",
            f"按结构部位拆分的内容块覆盖范围不一致（{detail}）。"
            "它们必须覆盖同一组结构部位，否则会有部位的内容无处输出。",
        )
    ]


def _validate_placeholders(
    contract: ReportContract, scan: TemplateScan
) -> list[TemplateIssue]:
    issues: list[TemplateIssue] = []
    known = contract.all_placeholders

    for hit in scan.placeholders:
        where = f"{hit.location}#{hit.paragraph_index}"
        if not hit.is_well_formed:
            issues.append(
                error(
                    "template_placeholder_malformed",
                    f"占位符 {hit.raw} 的名称不合法；只允许字母、数字、下划线和点号。",
                    where,
                )
            )
            continue
        if hit.name not in known:
            issues.append(
                error(
                    "template_placeholder_unknown",
                    f"未知占位符 {hit.raw}；契约 {contract.contract_type} 没有这个字段。",
                    where,
                )
            )

    for where in scan.unbalanced_braces:
        issues.append(
            error(
                "template_placeholder_unbalanced",
                "段落里有配不成对的 {{ 或 }}，或残缺的 [[REPORT: 前缀；"
                "它会原样出现在生成的报告里。",
                where,
            )
        )
    return issues


def _validate_fields(scan: TemplateScan) -> list[TemplateIssue]:
    issues: list[TemplateIssue] = []
    for hit in scan.fields:
        if hit.keyword in FIELDS_ALLOWED_ANYWHERE:
            continue
        if hit.keyword in FIELDS_ALLOWED_INSIDE_TOC:
            if hit.inside_toc:
                continue
            issues.append(
                error(
                    "template_field_outside_toc",
                    f"域 {hit.keyword} 只允许作为目录内部结果存在，不能单独使用"
                    f"（指令：{hit.instruction}）。交叉引用请交给生成器写成普通文本。",
                    hit.location,
                )
            )
            continue
        if hit.keyword in FIELDS_EXPLICITLY_REJECTED:
            issues.append(
                error(
                    "template_field_rejected",
                    f"模板使用了被禁止的域 {hit.keyword}（指令：{hit.instruction}）。"
                    "表号、图号、正文引用和动态页眉一律由生成器确定性写入。",
                    hit.location,
                )
            )
            continue
        issues.append(
            error(
                "template_field_not_allowed",
                f"模板使用了不在白名单内的域 {hit.keyword or '(空指令)'}"
                f"（指令：{hit.instruction}）。只允许 TOC、PAGE、NUMPAGES。",
                hit.location,
            )
        )
    return issues


def _validate_config(
    contract: ReportContract,
    config: TemplateConfig,
    present: dict[tuple[str, str | None], str],
) -> list[TemplateIssue]:
    issues: list[TemplateIssue] = []

    for key, number_format in config.table_number_formats.items():
        block, _, part_text = key.partition(":")
        part = part_text or None

        if block not in contract.numbered_blocks:
            issues.append(
                error(
                    "template_number_format_unknown_block",
                    f"编号格式配置里出现未知内容块 {block}。",
                )
            )
            continue
        if contract.is_per_part(block) and part is None:
            issues.append(
                error(
                    "template_number_format_part_required",
                    f"{block} 按结构部位拆分，编号格式的键必须写成 "
                    f"{block}:SUPERSTRUCTURE 这样的形式。",
                )
            )
            continue
        if not contract.is_per_part(block) and part is not None:
            issues.append(
                error(
                    "template_number_format_part_not_allowed",
                    f"{block} 不按结构部位拆分，编号格式的键不能带 :{part} 后缀。",
                )
            )
            continue
        if part is not None and part not in STRUCTURE_PART_CODES:
            issues.append(
                error(
                    "template_number_format_part_unknown",
                    f"编号格式的键 {key} 里结构部位代码 {part} 未知。",
                )
            )
            continue

        if BRACE_TOKEN_PATTERN.findall(number_format) != [NUMBER_TOKEN]:
            issues.append(
                error(
                    "template_number_format_invalid",
                    f"{key} 的编号格式“{number_format}”必须且只能包含一个 {NUMBER_TOKEN}。",
                )
            )

    for block, part in sorted(present, key=lambda item: (item[0], item[1] or "")):
        if block not in contract.numbered_blocks:
            continue
        key = numbering_key(block, part)
        if key not in config.table_number_formats:
            issues.append(
                error(
                    "template_number_format_missing",
                    f"模板含 [[REPORT:{key}]]，但没有配置它的编号格式。",
                )
            )

    declared_roles = set(config.required_personnel_roles)
    for role in sorted(declared_roles - contract.personnel_roles):
        issues.append(
            error("template_personnel_role_unknown", f"未知人员角色代码 {role}。")
        )
    return issues


def _validate_personnel_placeholders(
    scan: TemplateScan, config: TemplateConfig
) -> list[TemplateIssue]:
    """模板里用到的人员角色必须被声明为必填。

    否则生成前检查放行，签字页却留空——正是设计要避免的那类静默错误输出。
    """
    declared = set(config.required_personnel_roles)
    issues: list[TemplateIssue] = []
    reported: set[str] = set()
    for hit in scan.placeholders:
        match = PERSONNEL_PLACEHOLDER_PATTERN.match(hit.name)
        if match is None:
            continue
        role = match.group("role")
        if role in declared or role in reported:
            continue
        reported.add(role)
        issues.append(
            error(
                "template_personnel_role_not_required",
                f"模板使用了 {{{{{personnel_placeholder(role)}}}}}，"
                f"但 required_personnel_roles 未声明 {role}；生成时该位置会留空。",
                f"{hit.location}#{hit.paragraph_index}",
            )
        )
    return issues


def _validate_table_samples(scan: TemplateScan) -> list[TemplateIssue]:
    """校验表头样表（设计 §7.7）。

    样表是可选的：没画就退回生成器的默认列宽和表头。但一旦画了，列数必须与契约
    一致——模板作者删掉一列或调换两列，生成器照填不误，「病害位置」的内容会印到
    「病害类型」下面，看着完全正常但全错。这是这一层唯一必须拦住的事。
    """
    issues: list[TemplateIssue] = []
    seen: set[str] = set()

    for sample in scan.table_samples:
        where = f"{LOCATION_BODY}#{sample.paragraph_index}"
        if sample.block not in TABLE_COLUMNS:
            issues.append(
                error(
                    "template_table_sample_unknown_block",
                    f"表头样表 {sample.raw} 的内容块未知，或该内容块不支持样表；"
                    f"可用值：{'、'.join(sorted(TABLE_COLUMNS))}。",
                    where,
                )
            )
            continue
        if not sample.starts_paragraph:
            issues.append(
                error(
                    "template_table_sample_not_leading",
                    f"表头样表标记 {sample.raw} 不在段首；"
                    "生成时整段删掉，写在句子中间会把前面的话一起删了。"
                    "标记后面可以跟一句说明。",
                    where,
                )
            )
            continue
        if sample.block in seen:
            issues.append(
                error(
                    "template_table_sample_duplicated",
                    f"表头样表 {sample.raw} 重复出现。",
                    where,
                )
            )
            continue
        seen.add(sample.block)

        if not sample.has_table:
            issues.append(
                error(
                    "template_table_sample_missing_table",
                    f"表头样表标记 {sample.raw} 后面没有跟着表格；"
                    "标记的下一个元素必须是那张只有表头行的表。",
                    where,
                )
            )
            continue

        expected = TABLE_COLUMNS[sample.block]
        if len(sample.headers) != len(expected):
            issues.append(
                error(
                    "template_table_sample_column_mismatch",
                    f"{sample.raw} 的样表有 {len(sample.headers)} 列，"
                    f"而 {sample.block} 需要 {len(expected)} 列"
                    f"（{'、'.join(expected)}）。列数对不上，生成的报告会把内容"
                    "印到错误的列下面。",
                    where,
                )
            )
    return issues


def validate_template(
    path: Path,
    config: TemplateConfig | None = None,
    contract_type: str = CONTRACT_PERIODIC_INSPECTION_V1,
    limits: PackageLimits = DEFAULT_LIMITS,
) -> TemplateValidationResult:
    """校验一份上传的模板。

    包级检查先跑：不安全的包直接判定不通过，绝不进入解压和解析。
    """
    contract = get_contract(contract_type)
    config = config or TemplateConfig()

    inspection = inspect_package(path, limits)
    if not inspection.is_safe:
        return TemplateValidationResult(status="invalid", issues=inspection.issues)

    scan = scan_template(path)
    issues = list(inspection.issues)
    anchor_issues, present = _validate_anchors(contract, scan.anchors)
    issues.extend(anchor_issues)
    issues.extend(_validate_placeholders(contract, scan))
    issues.extend(_validate_fields(scan))
    issues.extend(_validate_config(contract, config, present))
    issues.extend(_validate_personnel_placeholders(scan, config))
    issues.extend(_validate_table_samples(scan))

    status = "invalid" if any(issue.severity == "error" for issue in issues) else "valid"
    return TemplateValidationResult(
        status=status,
        issues=issues,
        anchors_in_document_order=[
            hit.name for hit in scan.anchors if hit.location == LOCATION_BODY
        ],
        placeholders_used=sorted({hit.name for hit in scan.placeholders}),
        fields_used=sorted({hit.keyword for hit in scan.fields if hit.keyword}),
    )
