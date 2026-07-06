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


def normalize_unit(unit: str) -> str:
    if unit in {"m²", "㎡"}:
        return "m2"
    return unit


def parse_measurements(
    measurement_text: str | None, candidate_id: str
) -> tuple[list[Measurement], list[WarningItem]]:
    if not measurement_text:
        return [], []

    measurements: list[Measurement] = []
    for match in DIMENSION_PATTERN.finditer(measurement_text):
        label = match.group("label").upper()
        source_text = match.group(0).replace("：", "=")
        measurements.append(
            Measurement(
                dimension_type=DIMENSION_LABELS[label],
                value=float(match.group("value")),
                unit=normalize_unit(match.group("unit")),
                source_text=source_text,
            )
        )

    for match in COUNT_PATTERN.finditer(measurement_text):
        source_text = match.group(0)
        measurements.append(
            Measurement(
                dimension_type="数量",
                value=float(match.group("value")),
                unit=match.group("unit"),
                source_text=source_text,
            )
        )

    if measurements:
        return measurements, []

    return [], [
        WarningItem(
            code="measurement_parse_low_confidence",
            message="尺寸表达未能稳定结构化，请人工确认。",
            severity="warning",
            target_candidate_id=candidate_id,
        )
    ]
