from bridge_report_tools.importers.measurements import parse_measurements


def test_parse_length_and_width_measurements() -> None:
    measurements, warnings = parse_measurements("L=0.8m，W=0.12mm", "defect_0001")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["长度", "宽度"]
    assert measurements[0].value == 0.8
    assert measurements[0].unit == "m"
    assert measurements[0].source_text == "L=0.8m"
    assert measurements[1].value == 0.12
    assert measurements[1].unit == "mm"
    assert measurements[1].source_text == "W=0.12mm"


def test_parse_area_spacing_and_count_measurements() -> None:
    measurements, warnings = parse_measurements("S=0.3m2，D=0.15m，3处", "defect_0002")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["面积", "间距", "数量"]
    assert measurements[0].unit == "m2"
    assert measurements[1].unit == "m"
    assert measurements[2].unit == "处"
    assert measurements[2].value == 3


def test_low_confidence_measurement_keeps_warning() -> None:
    measurements, warnings = parse_measurements("局部破损，约20cm×30cm", "defect_0003")

    assert measurements == []
    assert [warning.model_dump() for warning in warnings] == [
        {
            "code": "measurement_parse_low_confidence",
            "message": "尺寸表达未能稳定结构化，请人工确认。",
            "severity": "warning",
            "target_candidate_id": "defect_0003",
        }
    ]
