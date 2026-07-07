from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import (
    EvaluationPartRating,
    EvaluationScoreRow,
    OverallRating,
    Ratings,
    SourceRef,
    StructurePartRating,
    WarningItem,
)
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules import WordRuleSet


STRUCTURE_PARTS = {"上部结构", "下部结构", "桥面系"}
SCORE_ROW_PATTERN = re.compile(r"(?P<count>\d+)\s*[:：]\s*(?P<score>\d+(?:\.\d+)?)")


def is_overall_rating_table(table: DocxTable, rule_set: WordRuleSet) -> bool:
    table_rule = rule_set.match_rating_table_title(table.title)
    if table_rule is None or table_rule.table_kind != "overall":
        return False

    header = table.rows[0] if table.rows else []
    return (
        has_header(header, "层级")
        and has_header(header, "结构部位")
        and has_header(header, "类别编号")
        and has_header(header, "评价部件")
        and has_standalone_score_header(header)
        and has_header(header, "权重")
        and has_header(header, "等级")
        and has_header(header, "构件评分")
    )


def has_weight_table(tables: list[DocxTable], rule_set: WordRuleSet) -> bool:
    return any(
        (table_rule := rule_set.match_rating_table_title(table.title)) is not None
        and table_rule.table_kind == "weight"
        for table in tables
    )


def has_header(headers: list[str], required: str) -> bool:
    return any(required in header for header in headers)


def has_standalone_score_header(headers: list[str]) -> bool:
    return any("评分" in header and "构件评分" not in header for header in headers)


def header_index(headers: list[str], keywords: list[str]) -> int | None:
    for index, header in enumerate(headers):
        if any(keyword in header for keyword in keywords):
            return index
    return None


def score_header_index(headers: list[str]) -> int | None:
    for index, header in enumerate(headers):
        if "评分" in header and "构件评分" not in header:
            return index
    return None


def get_cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def parse_float(value: str) -> float:
    return float(value.strip())


def parse_int(value: str) -> int:
    match = re.search(r"\d+", value)
    if match is None:
        raise ValueError(f"missing integer value: {value}")
    return int(match.group(0))


def parse_score_rows(value: str) -> list[EvaluationScoreRow]:
    return [
        EvaluationScoreRow(component_count=int(match.group("count")), component_score=float(match.group("score")))
        for match in SCORE_ROW_PATTERN.finditer(value)
    ]


def source_ref(table: DocxTable, row_index: int, row: list[str]) -> SourceRef:
    return SourceRef(
        chapter=table.chapter,
        table_title=table.title,
        table_index=table.index,
        row_index=row_index,
        raw_row_text=" | ".join(row),
    )


def parse_rating_tables(tables: list[DocxTable], rule_set: WordRuleSet) -> tuple[Ratings, list[WarningItem]]:
    warnings: list[WarningItem] = []
    if not has_weight_table(tables, rule_set):
        warnings.append(
            WarningItem(
                code="liaoning_trunk_rating_weight_table_missing",
                message="未识别到表4.1-1桥梁部件权重计算表，请人工确认评分权重。",
                severity="warning",
                target_candidate_id=None,
            )
        )

    for table in tables:
        if not table.rows or not is_overall_rating_table(table, rule_set):
            continue

        headers = table.rows[0]
        level_index = header_index(headers, ["层级"])
        structure_part_index = header_index(headers, ["结构部位"])
        category_no_index = header_index(headers, ["类别编号"])
        evaluation_part_index = header_index(headers, ["评价部件"])
        score_index = score_header_index(headers)
        weight_index = header_index(headers, ["权重"])
        grade_index = header_index(headers, ["等级"])
        component_score_index = header_index(headers, ["构件评分"])
        table_overall: OverallRating | None = None
        table_structure_parts: list[StructurePartRating] = []
        table_evaluation_parts: list[EvaluationPartRating] = []

        try:
            for row_index, row in enumerate(table.rows[1:], start=1):
                if not any(row):
                    continue

                level = get_cell(row, level_index)
                ref = source_ref(table, row_index, row)

                if level == "全桥":
                    table_overall = OverallRating(
                        total_score=parse_float(get_cell(row, score_index)),
                        overall_grade=get_cell(row, grade_index),
                        source_ref=ref,
                        confidence=0.92,
                        review_status="待确认",
                    )
                    continue

                if level == "结构分部":
                    structure_part = get_cell(row, structure_part_index)
                    if structure_part not in STRUCTURE_PARTS:
                        continue
                    table_structure_parts.append(
                        StructurePartRating(
                            structure_part=structure_part,
                            structure_score=parse_float(get_cell(row, score_index)),
                            weight=parse_float(get_cell(row, weight_index)),
                            grade=get_cell(row, grade_index),
                            source_ref=ref,
                            confidence=0.92,
                            review_status="待确认",
                        )
                    )
                    continue

                if level == "评价部件":
                    structure_part = get_cell(row, structure_part_index)
                    if structure_part not in STRUCTURE_PARTS:
                        continue
                    table_evaluation_parts.append(
                        EvaluationPartRating(
                            structure_part=structure_part,
                            category_no=parse_int(get_cell(row, category_no_index)),
                            evaluation_part=get_cell(row, evaluation_part_index),
                            part_score=parse_float(get_cell(row, score_index)),
                            score_rows=parse_score_rows(get_cell(row, component_score_index)),
                            source_ref=ref,
                            confidence=0.92,
                            review_status="待确认",
                        )
                    )
        except ValueError:
            continue

        if table_overall is not None:
            ratings = Ratings(
                overall=table_overall,
                structure_parts=table_structure_parts,
                evaluation_parts=table_evaluation_parts,
                warnings=warnings,
            )
            return ratings, warnings

    raise WordImportError(
        code="rating_table_not_found",
        message="未识别到辽宁国省干线表4.1-2总体技术状况评定表。",
    )
