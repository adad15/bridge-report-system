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
    r"(?P<label>[LWSAD])\s*[=:：]\s*(?P<approx>约|大约|约为)?\s*"
    r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>m2|m²|㎡|mm|cm|m)",
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
    r"(?P<label>总面积|面积|长度|宽度|间距)(?:范围)?\s*[=:：]?\s*"
    r"(?P<approx>约|大约|约为)?\s*"
    r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>m2|m²|㎡|mm|cm|m)",
    re.IGNORECASE,
)
RANGE_PATTERN = re.compile(
    r"(?:(?P<label>总面积|面积|长度|宽度|间距|[LWSAD])(?:范围)?\s*[=:：]?\s*)?"
    r"(?P<approx>约|大约|约为)?\s*"
    r"(?P<minimum>\d+(?:\.\d+)?)\s*(?:~|～|至)\s*"
    r"(?P<maximum>\d+(?:\.\d+)?)\s*"
    r"(?P<unit>m2|m²|㎡|mm|cm|m)",
    re.IGNORECASE,
)
APPROXIMATE_SINGLE_PATTERN = re.compile(
    r"(?P<approx>约|大约|约为)\s*(?P<value>\d+(?:\.\d+)?)\s*"
    r"(?P<unit>m2|m²|㎡|mm|cm|m)",
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


def dimension_type_for(label: str | None, unit: str) -> str:
    if label:
        upper = label.upper()
        if upper in DIMENSION_LABELS:
            return DIMENSION_LABELS[upper]
        if label in CHINESE_DIMENSION_LABELS:
            return CHINESE_DIMENSION_LABELS[label]
    return "面积" if normalize_unit(unit).endswith("2") else "长度"


def single_measurement(
    dimension_type: str,
    value: float,
    unit: str,
    source_text: str,
    *,
    is_approximate: bool = False,
) -> Measurement:
    return Measurement(
        dimension_type=dimension_type,
        value_type="single",
        value=value,
        minimum_value=None,
        maximum_value=None,
        unit=unit,
        is_approximate=is_approximate,
        source_text=source_text,
    )


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
    for match in RANGE_PATTERN.finditer(measurement_text):
        unit = normalize_unit(match.group("unit"))
        measurement_matches.append(
            (
                match.span(),
                Measurement(
                    dimension_type=dimension_type_for(match.group("label"), unit),
                    value_type="range",
                    value=None,
                    minimum_value=float(match.group("minimum")),
                    maximum_value=float(match.group("maximum")),
                    unit=unit,
                    is_approximate=match.group("approx") is not None,
                    source_text=match.group(0),
                ),
            )
        )

    for match in DIMENSION_PATTERN.finditer(measurement_text):
        if overlaps(match.span(), [item[0] for item in measurement_matches]):
            continue
        label = match.group("label").upper()
        measurement_matches.append(
            (
                match.span(),
                single_measurement(
                    DIMENSION_LABELS[label],
                    float(match.group("value")),
                    normalize_unit(match.group("unit")),
                    match.group(0),
                    is_approximate=match.group("approx") is not None,
                ),
            )
        )

    for match in CHINESE_DIMENSION_PATTERN.finditer(measurement_text):
        span = match.span()
        existing_spans = [item[0] for item in measurement_matches]
        if overlaps(span, existing_spans):
            continue
        measurement_matches.append(
            (
                span,
                single_measurement(
                    CHINESE_DIMENSION_LABELS[match.group("label")],
                    float(match.group("value")),
                    normalize_unit(match.group("unit")),
                    match.group(0),
                    is_approximate=match.group("approx") is not None,
                ),
            )
        )

    for match in APPROXIMATE_SINGLE_PATTERN.finditer(measurement_text):
        span = match.span()
        if overlaps(span, [item[0] for item in measurement_matches]):
            continue
        unit = normalize_unit(match.group("unit"))
        measurement_matches.append(
            (
                span,
                single_measurement(
                    dimension_type_for(None, unit),
                    float(match.group("value")),
                    unit,
                    match.group(0),
                    is_approximate=True,
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
        measurement_matches.append(
            (
                span,
                single_measurement(
                    "面积",
                    round(first * second, 6),
                    unit,
                    match.group(0),
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
                single_measurement(
                    "数量",
                    float(match.group("value")),
                    match.group("unit"),
                    source_text,
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
