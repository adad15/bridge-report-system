from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import DefectCandidate, SourceRef, WarningItem
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.measurements import parse_measurements


PHOTO_NUMBER_PATTERN = re.compile(r"(?:照片)?(?P<number>\d+(?:\.\d+)?-\d+)")


def infer_structure_part(table: DocxTable) -> str:
    text = " ".join(value or "" for value in [table.title, table.chapter])
    if "上部结构" in text:
        return "上部结构"
    if "下部结构" in text:
        return "下部结构"
    if "桥面系" in text:
        return "桥面系"
    return "其他"


def is_defect_table(table: DocxTable) -> bool:
    header = table.rows[0] if table.rows else []
    header_text = "|".join(header)
    chapter = table.chapter or ""
    return (
        "第二章" in chapter
        and "病害" in header_text
        and "照片" in header_text
        and any(keyword in header_text for keyword in ["构件", "部件", "部位", "位置"])
    )


def header_index(headers: list[str], keywords: list[str]) -> int | None:
    for index, header in enumerate(headers):
        if any(keyword in header for keyword in keywords):
            return index
    return None


def get_cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def parse_photo_numbers(text: str) -> list[str]:
    return [match.group("number") for match in PHOTO_NUMBER_PATTERN.finditer(text)]


def parse_defect_tables(tables: list[DocxTable]) -> tuple[list[DefectCandidate], list[WarningItem], list[WarningItem]]:
    defects: list[DefectCandidate] = []
    warnings: list[WarningItem] = []
    errors: list[WarningItem] = []
    defect_table_found = False

    for table in tables:
        if not table.rows or not is_defect_table(table):
            continue
        defect_table_found = True
        headers = table.rows[0]
        component_index = header_index(headers, ["构件", "部件"])
        location_index = header_index(headers, ["位置", "部位"])
        type_index = header_index(headers, ["病害"])
        quantity_index = header_index(headers, ["数量"])
        measurement_index = header_index(headers, ["尺寸"])
        photo_index = header_index(headers, ["照片"])
        structure_part = infer_structure_part(table)

        for row_index, row in enumerate(table.rows[1:], start=1):
            if not any(row):
                continue
            candidate_id = f"defect_{len(defects) + 1:04d}"
            measurement_text = get_cell(row, measurement_index) or None
            measurements, measurement_warnings = parse_measurements(measurement_text, candidate_id)
            photo_numbers = parse_photo_numbers(get_cell(row, photo_index))
            row_warnings = list(measurement_warnings)
            if not photo_numbers:
                row_warnings.append(
                    WarningItem(
                        code="photo_number_missing",
                        message="病害行缺少照片编号，请人工确认。",
                        severity="warning",
                        target_candidate_id=candidate_id,
                    )
                )

            location = get_cell(row, location_index)
            defect_type = get_cell(row, type_index)
            defect_description = f"{location}{defect_type}".strip() or "未识别病害描述"
            defects.append(
                DefectCandidate(
                    candidate_id=candidate_id,
                    structure_part=structure_part,
                    component_name=get_cell(row, component_index) or "未识别构件",
                    component_alias=None,
                    defect_type=defect_type or "未识别病害",
                    defect_location=location or "未识别位置",
                    defect_description=defect_description,
                    quantity_text=get_cell(row, quantity_index) or None,
                    measurement_text=measurement_text,
                    measurements=measurements,
                    photo_numbers=photo_numbers,
                    severity=None,
                    remark=None,
                    source_ref=SourceRef(
                        chapter=table.chapter,
                        table_title=table.title,
                        table_index=table.index,
                        row_index=row_index,
                        raw_row_text=" | ".join(row),
                    ),
                    confidence=0.92 if structure_part != "其他" else 0.75,
                    review_status="待确认",
                    review_note=None,
                    warnings=row_warnings,
                )
            )

    if not defect_table_found:
        errors.append(
            WarningItem(
                code="defect_tables_not_found",
                message="未识别到第二章结构病害检查表，本次导入没有生成病害候选。",
                severity="error",
                target_candidate_id=None,
            )
        )

    return defects, warnings, errors
