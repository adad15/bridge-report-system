from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import (
    DefectCandidate,
    PhotoReference,
    SourceRef,
    WarningItem,
)
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.measurements import parse_measurements
from bridge_report_tools.importers.word_rules import DefectTableRule, WordRuleSet
from bridge_report_tools.importers.word_rules.common import normalize_rule_text


PHOTO_NUMBER_PATTERN = re.compile(r"(?:照片)?(?P<number>\d+(?:\.\d+)?-\d+)")
QUANTITY_PATTERN = re.compile(r"(?:共|约)?(?P<quantity>\d+(?:\.\d+)?\s*(?:处|条|个|块|道|孔|座))")
FUZZY_QUANTITY_PATTERN = re.compile(r"(?P<quantity>多(?:处|条|个|块|道|孔))")


def parse_optional_number(text: str) -> tuple[float | None, bool]:
    """解析可空数值单元格，返回 (数值, 是否为非法文本)。空文本不算非法。"""
    cleaned = text.strip()
    if not cleaned:
        return None, False
    try:
        return float(cleaned), False
    except ValueError:
        return None, True


def match_defect_table(table: DocxTable, rule_set: WordRuleSet) -> DefectTableRule | None:
    if not table.rows:
        return None
    rule = rule_set.match_defect_table_title(table.title)
    if rule is None:
        return None
    header = table.rows[0]
    header_text = "|".join(header)
    if "病害" not in header_text:
        return None
    if "照片" not in header_text:
        return None
    if not any(keyword in header_text for keyword in ["构件", "部件", "部位", "位置"]):
        return None
    return rule


def header_index(headers: list[str], keywords: list[str], excluded_keywords: list[str] | None = None) -> int | None:
    normalized_keywords = [normalize_rule_text(keyword) for keyword in keywords]
    normalized_excluded_keywords = [
        normalize_rule_text(keyword) for keyword in (excluded_keywords or [])
    ]
    for index, header in enumerate(headers):
        normalized_header = normalize_rule_text(header)
        if any(keyword in normalized_header for keyword in normalized_excluded_keywords):
            continue
        if any(keyword in normalized_header for keyword in normalized_keywords):
            return index
    return None


def get_cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def parse_photo_numbers(text: str) -> list[str]:
    return [match.group("number") for match in PHOTO_NUMBER_PATTERN.finditer(text)]


def derive_quantity_text(measurement_text: str | None) -> str | None:
    if not measurement_text:
        return None
    for pattern in (QUANTITY_PATTERN, FUZZY_QUANTITY_PATTERN):
        match = pattern.search(measurement_text)
        if match:
            return re.sub(r"\s+", "", match.group("quantity"))
    return None


def parse_defect_tables(
    tables: list[DocxTable],
    rule_set: WordRuleSet,
) -> tuple[list[DefectCandidate], list[WarningItem], list[WarningItem]]:
    defects: list[DefectCandidate] = []
    warnings: list[WarningItem] = []
    errors: list[WarningItem] = []
    defect_table_found = False
    found_table_numbers: set[str] = set()
    for table in tables:
        table_rule = match_defect_table(table, rule_set)
        if table_rule is None:
            continue
        found_table_numbers.add(table_rule.table_no)
        defect_table_found = True
        headers = table.rows[0]
        component_index = header_index(headers, ["部件名称", "构件名称", "构件", "部件"], ["构件编号", "构件评分"])
        component_alias_index = header_index(headers, ["构件编号"])
        location_index = header_index(headers, ["病害位置", "位置", "部位"])
        type_index = header_index(headers, ["病害类型", "病害名称", "病害"], ["病害位置", "病害特征", "病害扣分"])
        quantity_index = header_index(headers, ["数量"])
        measurement_index = header_index(headers, ["病害特征", "尺寸"])
        photo_index = header_index(headers, ["照片编号", "照片"])
        scale_index = header_index(headers, ["标度"])
        structure_part = table_rule.structure_part

        for row_index, row in enumerate(table.rows[1:], start=1):
            if not any(row):
                continue
            candidate_id = f"defect_{len(defects) + 1:04d}"
            measurement_text = get_cell(row, measurement_index) or None
            measurements, measurement_warnings = parse_measurements(measurement_text, candidate_id)
            photo_numbers = parse_photo_numbers(get_cell(row, photo_index))
            row_warnings = list(measurement_warnings)

            defect_scale_value, scale_invalid = parse_optional_number(get_cell(row, scale_index))
            defect_scale: int | None = None
            if defect_scale_value is not None and defect_scale_value.is_integer() and defect_scale_value > 0:
                defect_scale = int(defect_scale_value)
            elif defect_scale_value is not None:
                scale_invalid = True
            if scale_invalid:
                row_warnings.append(
                    WarningItem(
                        code="defect_scale_invalid",
                        message=f"病害标度“{get_cell(row, scale_index)}”不是正整数，请人工确认。",
                        severity="warning",
                        target_candidate_id=candidate_id,
                    )
                )

            location = get_cell(row, location_index)
            defect_type = get_cell(row, type_index)
            defect_description = f"{location}{defect_type}".strip() or "未识别病害描述"
            component_name = get_cell(row, component_index) or "未识别构件"
            component_number = get_cell(row, component_alias_index) or None
            defects.append(
                DefectCandidate(
                    candidate_id=candidate_id,
                    source_structure_part=structure_part,
                    component_name=component_name,
                    component_number=component_number,
                    bridge_component_id=None,
                    standard_component_category_id=None,
                    resolved_structure_part=None,
                    defect_type=defect_type or "未识别病害",
                    defect_location=location or "未识别位置",
                    defect_scale=defect_scale,
                    defect_description=defect_description,
                    quantity_text=get_cell(row, quantity_index) or derive_quantity_text(measurement_text),
                    measurement_text=measurement_text,
                    measurements=measurements,
                    standard_defect_indicator_id=None,
                    photo_references=[
                        PhotoReference(
                            photo_number=photo_number,
                            resolution="pending",
                            photo_candidate_id=None,
                            resolved_defect_candidate_id=None,
                            review_note=None,
                        )
                        for photo_number in photo_numbers
                    ],
                    group_review_status="待确认",
                    severity=None,
                    remark=None,
                    source_ref=SourceRef(
                        chapter=table.chapter,
                        table_title=table.title,
                        table_index=table.index,
                        row_index=row_index,
                        raw_row_text=" | ".join(row),
                    ),
                    confidence=0.92,
                    review_status="待确认",
                    review_note=None,
                    warnings=row_warnings,
                )
            )

    if defect_table_found:
        for table_rule in rule_set.defect_table_rules:
            if table_rule.table_no in found_table_numbers:
                continue
            warnings.append(
                WarningItem(
                    code="liaoning_trunk_defect_table_missing",
                    message=f"未识别到{table_rule.table_no}{table_rule.structure_part}病害检查表，请人工确认。",
                    severity="warning",
                    target_candidate_id=None,
                )
            )
    else:
        errors.append(
            WarningItem(
                code="defect_tables_not_found",
                message="未识别到辽宁国省干线表2.1-1、表2.2-1、表2.3-1病害检查表，本次导入没有生成病害候选。",
                severity="error",
                target_candidate_id=None,
            )
        )

    return defects, warnings, errors
