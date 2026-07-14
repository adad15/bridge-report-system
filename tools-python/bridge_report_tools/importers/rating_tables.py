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
from bridge_report_tools.importers.word_rules.common import normalize_rule_text


STRUCTURE_PARTS = {"上部结构", "下部结构", "桥面系"}
SCORE_ROW_PATTERN = re.compile(r"(?P<count>\d+)\s*[:：]\s*(?P<score>\d+(?:\.\d+)?)")
LIAONING_OVERALL_HEADERS = (
    "结构",
    "类别",
    "评价部件",
    "构件数量",
    "构件评分",
    "桥梁部件技术状况评分",
    "桥梁结构技术状况评分",
    "桥梁结构组成权重",
    "等级",
    "桥梁总体技术状况评分",
    "综合评级",
)


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


def is_liaoning_overall_rating_table(table: DocxTable, rule_set: WordRuleSet) -> bool:
    table_rule = rule_set.match_rating_table_title(table.title)
    if table_rule is None or table_rule.table_kind != "overall":
        return False

    headers = table.rows[0] if table.rows else []
    return all(exact_header_index(headers, required) is not None for required in LIAONING_OVERALL_HEADERS)


def has_weight_table(tables: list[DocxTable], rule_set: WordRuleSet) -> bool:
    return any(
        (table_rule := rule_set.match_rating_table_title(table.title)) is not None
        and table_rule.table_kind == "weight"
        for table in tables
    )


def has_header(headers: list[str], required: str) -> bool:
    normalized_required = normalize_rule_text(required)
    return any(normalized_required in normalize_rule_text(header) for header in headers)


def has_standalone_score_header(headers: list[str]) -> bool:
    return any(
        "评分" in normalize_rule_text(header) and "构件评分" not in normalize_rule_text(header)
        for header in headers
    )


def header_index(headers: list[str], keywords: list[str]) -> int | None:
    normalized_keywords = [normalize_rule_text(keyword) for keyword in keywords]
    for index, header in enumerate(headers):
        normalized_header = normalize_rule_text(header)
        if any(keyword in normalized_header for keyword in normalized_keywords):
            return index
    return None


def exact_header_index(headers: list[str], required: str) -> int | None:
    normalized_required = normalize_rule_text(required)
    for index, header in enumerate(headers):
        if normalize_rule_text(header) == normalized_required:
            return index
    return None


def score_header_index(headers: list[str]) -> int | None:
    for index, header in enumerate(headers):
        normalized_header = normalize_rule_text(header)
        if "评分" in normalized_header and "构件评分" not in normalized_header:
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


def parse_liaoning_overall_rating_table(
    table: DocxTable, warnings: list[WarningItem]
) -> Ratings | None:
    headers = table.rows[0]
    structure_part_index = exact_header_index(headers, "结构")
    category_no_index = exact_header_index(headers, "类别")
    evaluation_part_index = exact_header_index(headers, "评价部件")
    component_count_index = exact_header_index(headers, "构件数量")
    component_score_index = exact_header_index(headers, "构件评分")
    part_score_index = exact_header_index(headers, "桥梁部件技术状况评分")
    structure_score_index = exact_header_index(headers, "桥梁结构技术状况评分")
    weight_index = exact_header_index(headers, "桥梁结构组成权重")
    grade_index = exact_header_index(headers, "等级")
    overall_score_index = exact_header_index(headers, "桥梁总体技术状况评分")
    overall_grade_index = exact_header_index(headers, "综合评级")

    overall: OverallRating | None = None
    structure_parts: dict[str, StructurePartRating] = {}
    evaluation_parts: dict[tuple[str, int, str, float], EvaluationPartRating] = {}

    for row_index, row in enumerate(table.rows[1:], start=1):
        if not any(row):
            continue

        structure_part = get_cell(row, structure_part_index)
        if structure_part not in STRUCTURE_PARTS:
            continue

        try:
            category_no = parse_int(get_cell(row, category_no_index))
            component_count = parse_int(get_cell(row, component_count_index))
            component_score = parse_float(get_cell(row, component_score_index))
            part_score = parse_float(get_cell(row, part_score_index))
            structure_score = parse_float(get_cell(row, structure_score_index))
            weight = parse_float(get_cell(row, weight_index))
            total_score = parse_float(get_cell(row, overall_score_index))
        except ValueError:
            continue

        ref = source_ref(table, row_index, row)
        grade = get_cell(row, grade_index)
        evaluation_part = get_cell(row, evaluation_part_index)

        if overall is None:
            overall = OverallRating(
                total_score=total_score,
                overall_grade=get_cell(row, overall_grade_index),
                source_ref=ref,
                confidence=0.92,
                review_status="待确认",
            )

        if structure_part not in structure_parts:
            structure_parts[structure_part] = StructurePartRating(
                structure_part=structure_part,
                structure_score=structure_score,
                weight=weight,
                grade=grade,
                source_ref=ref,
                confidence=0.92,
                review_status="待确认",
            )

        key = (structure_part, category_no, evaluation_part, part_score)
        if key not in evaluation_parts:
            evaluation_parts[key] = EvaluationPartRating(
                structure_part=structure_part,
                category_no=category_no,
                evaluation_part=evaluation_part,
                part_score=part_score,
                score_rows=[],
                source_ref=ref,
                confidence=0.92,
                review_status="待确认",
            )
        evaluation_parts[key].score_rows.append(
            EvaluationScoreRow(component_count=component_count, component_score=component_score)
        )

    if overall is None:
        return None

    return Ratings(
        overall=overall,
        structure_parts=list(structure_parts.values()),
        evaluation_parts=list(evaluation_parts.values()),
        component_ratings=[],
        warnings=warnings,
    )


def parse_legacy_overall_rating_table(table: DocxTable, warnings: list[WarningItem]) -> Ratings | None:
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
        return None

    if table_overall is None:
        return None

    return Ratings(
        overall=table_overall,
        structure_parts=table_structure_parts,
        evaluation_parts=table_evaluation_parts,
        component_ratings=[],
        warnings=warnings,
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
        if not table.rows:
            continue

        if is_liaoning_overall_rating_table(table, rule_set):
            ratings = parse_liaoning_overall_rating_table(table, warnings)
            if ratings is not None:
                return ratings, warnings
            continue

        if is_overall_rating_table(table, rule_set):
            ratings = parse_legacy_overall_rating_table(table, warnings)
            if ratings is not None:
                return ratings, warnings

    raise WordImportError(
        code="rating_table_not_found",
        message="未识别到辽宁国省干线表4.1-2总体技术状况评定表。",
    )
