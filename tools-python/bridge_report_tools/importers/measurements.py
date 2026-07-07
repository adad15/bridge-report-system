from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import Measurement, WarningItem


DIMENSION_LABELS = {
    "L": "长度",
    "W": "宽度",
    "S": "面积",
    "A": "面积",
    "D": "间距",
}

DIMENSION_PATTERN = re.compile(
    r"(?P<label>[LWSAD])\s*[=:：]\s*(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>m2|m²|㎡|mm|cm|m)",
    re.IGNORECASE,
)
CHINESE_DIMENSION_LABELS = {
    "长度": "长度",
    "宽度": "宽度",
    "面积": "面积",
    "总面积": "总面积",
    "间距": "间距",
}
CHINESE_DIMENSION_PATTERN = re.compile(
    r"(?P<label>总面积|面积|长度|宽度|间距)\s*[=:：]\s*"
    r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>m2|m²|㎡|mm|cm|m)",
    re.IGNORECASE,
)
AREA_PRODUCT_PATTERN = re.compile(
    r"(?:(?P<label>[SA])\s*[=:：]\s*)?"
    r"(?P<first>\d+(?:\.\d+)?)\s*(?P<first_unit>mm|cm|m)?\s*"
    r"[×xX*]\s*"
    r"(?P<second>\d+(?:\.\d+)?)\s*(?P<second_unit>m2|m²|㎡|mm2|mm²|cm2|cm²|mm|cm|m)",
    re.IGNORECASE,
)
COUNT_PATTERN = re.compile(r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>处|条|个|块)")
MEASUREMENT_HINT_PATTERN = re.compile(
    r"\d|[LWSAD]\s*[=:：]|长度|宽度|面积|总面积|间距|m2|m²|㎡|mm|cm|×|x",
    re.IGNORECASE,
)
LEFTOVER_MEASUREMENT_HINT_PATTERN = re.compile(r"\d|m2|m²|㎡|mm|cm|×|x", re.IGNORECASE)


def normalize_unit(unit: str) -> str:
    if unit in {"m²", "㎡"}:
        return "m2"
    if unit == "cm²":
        return "cm2"
    if unit == "mm²":
        return "mm2"
    return unit


def area_unit(unit: str) -> str:
    normalized_unit = normalize_unit(unit)
    if normalized_unit in {"m2", "cm2", "mm2"}:
        return normalized_unit
    return f"{normalized_unit}2"


def overlaps(span: tuple[int, int], existing_spans: list[tuple[int, int]]) -> bool:
    start, end = span
    return any(start < existing_end and end > existing_start for existing_start, existing_end in existing_spans)


def low_confidence_warning(candidate_id: str) -> WarningItem:
    return WarningItem(
        code="measurement_parse_low_confidence",
        message="尺寸表达未能稳定结构化，请人工确认。",
        severity="warning",
        target_candidate_id=candidate_id,
    )


def should_warn(measurement_text: str, measurement_spans: list[tuple[int, int]]) -> bool:
    if not measurement_spans:
        return bool(MEASUREMENT_HINT_PATTERN.search(measurement_text))

    remaining_chars = list(measurement_text)
    for start, end in measurement_spans:
        remaining_chars[start:end] = " " * (end - start)
    return bool(LEFTOVER_MEASUREMENT_HINT_PATTERN.search("".join(remaining_chars)))


def parse_measurements(
    measurement_text: str | None, candidate_id: str
) -> tuple[list[Measurement], list[WarningItem]]:
    if not measurement_text:
        return [], []

    measurement_matches: list[tuple[tuple[int, int], Measurement]] = []
    for match in DIMENSION_PATTERN.finditer(measurement_text):
        label = match.group("label").upper()
        source_text = match.group(0).replace("：", "=")
        measurement_matches.append(
            (
                match.span(),
                Measurement(
                    dimension_type=DIMENSION_LABELS[label],
                    value=float(match.group("value")),
                    unit=normalize_unit(match.group("unit")),
                    source_text=source_text,
                ),
            )
        )

    for match in CHINESE_DIMENSION_PATTERN.finditer(measurement_text):
        span = match.span()
        existing_spans = [item[0] for item in measurement_matches]
        if overlaps(span, existing_spans):
            continue
        source_text = match.group(0).replace("：", "=")
        measurement_matches.append(
            (
                span,
                Measurement(
                    dimension_type=CHINESE_DIMENSION_LABELS[match.group("label")],
                    value=float(match.group("value")),
                    unit=normalize_unit(match.group("unit")),
                    source_text=source_text,
                ),
            )
        )

    for match in AREA_PRODUCT_PATTERN.finditer(measurement_text):
        span = match.span()
        existing_spans = [item[0] for item in measurement_matches]
        if overlaps(span, existing_spans):
            continue
        first = float(match.group("first"))
        second = float(match.group("second"))
        unit = area_unit(match.group("second_unit"))
        source_text = match.group(0).replace("：", "=")
        measurement_matches.append(
            (
                span,
                Measurement(
                    dimension_type="面积",
                    value=round(first * second, 6),
                    unit=unit,
                    source_text=source_text,
                ),
            )
        )

    for match in COUNT_PATTERN.finditer(measurement_text):
        span = match.span()
        existing_spans = [item[0] for item in measurement_matches]
        if overlaps(span, existing_spans):
            continue
        source_text = match.group(0)
        measurement_matches.append(
            (
                span,
                Measurement(
                    dimension_type="数量",
                    value=float(match.group("value")),
                    unit=match.group("unit"),
                    source_text=source_text,
                ),
            )
        )

    measurement_matches.sort(key=lambda item: item[0][0])
    measurements = [measurement for _, measurement in measurement_matches]

    measurement_spans = [span for span, _ in measurement_matches]
    if not should_warn(measurement_text, measurement_spans):
        return measurements, []

    warnings = [low_confidence_warning(candidate_id)]

    if measurements:
        return measurements, warnings

    return [], warnings
