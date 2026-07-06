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
COUNT_PATTERN = re.compile(r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>处|条|个|块)")
SEPARATOR_PATTERN = re.compile(r"[\s,，;；、.。:：/\\|()\[\]{}（）【】<>《》]+")


def normalize_unit(unit: str) -> str:
    if unit in {"m²", "㎡"}:
        return "m2"
    return unit


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

    for match in COUNT_PATTERN.finditer(measurement_text):
        source_text = match.group(0)
        measurement_matches.append(
            (
                match.span(),
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

    remaining_chars = list(measurement_text)
    for (start, end), _ in measurement_matches:
        remaining_chars[start:end] = " " * (end - start)
    has_meaningful_leftover = bool(SEPARATOR_PATTERN.sub("", "".join(remaining_chars)))

    if measurements and not has_meaningful_leftover:
        return measurements, []

    warnings = [
        WarningItem(
            code="measurement_parse_low_confidence",
            message="尺寸表达未能稳定结构化，请人工确认。",
            severity="warning",
            target_candidate_id=candidate_id,
        )
    ]

    if measurements:
        return measurements, warnings

    return [], warnings
