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


def test_parse_range_measurements_with_supported_separators() -> None:
    for source_text, minimum, maximum in [
        ("0.5~4.0m", 0.5, 4.0),
        ("0.5～4.0m", 0.5, 4.0),
        ("15至20m", 15.0, 20.0),
    ]:
        measurements, warnings = parse_measurements(source_text, "defect_range")

        assert warnings == []
        assert len(measurements) == 1
        measurement = measurements[0]
        assert measurement.dimension_type == "长度"
        assert measurement.value_type == "range"
        assert measurement.value is None
        assert measurement.minimum_value == minimum
        assert measurement.maximum_value == maximum
        assert measurement.unit == "m"
        assert measurement.is_approximate is False
        assert measurement.source_text == source_text


def test_parse_approximate_area_and_unseparated_chinese_length() -> None:
    area, area_warnings = parse_measurements("总面积约1.0m²", "defect_area")
    length, length_warnings = parse_measurements("长度20.0m", "defect_length")

    assert area_warnings == []
    assert [item.model_dump() for item in area] == [
        {
            "dimension_type": "总面积",
            "value_type": "single",
            "value": 1.0,
            "minimum_value": None,
            "maximum_value": None,
            "unit": "m2",
            "is_approximate": True,
            "source_text": "总面积约1.0m²",
        }
    ]
    assert length_warnings == []
    assert length[0].value_type == "single"
    assert length[0].value == 20.0
    assert length[0].is_approximate is False


def test_parse_area_spacing_and_count_measurements() -> None:
    measurements, warnings = parse_measurements("S=0.3m2，D=0.15m，3处", "defect_0002")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["面积", "间距", "数量"]
    assert measurements[0].unit == "m2"
    assert measurements[1].unit == "m"
    assert measurements[2].unit == "处"
    assert measurements[2].value == 3


def test_low_confidence_measurement_keeps_warning_for_unstable_numeric_text() -> None:
    measurements, warnings = parse_measurements("局部破损，约20左右", "defect_0003")

    assert measurements == []
    assert [warning.model_dump() for warning in warnings] == [
        {
            "code": "measurement_parse_low_confidence",
            "message": "尺寸表达未能稳定结构化，请人工确认。",
            "severity": "warning",
            "target_candidate_id": "defect_0003",
        }
    ]


def test_mixed_measurement_text_ignores_descriptive_leftover_after_parse() -> None:
    measurements, warnings = parse_measurements("多条横向裂缝,L=1.2m,W=0.15m，间距D=0.15m", "defect_0004")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["长度", "宽度", "间距"]
    assert measurements[0].value == 1.2
    assert measurements[0].unit == "m"


def test_count_before_dimension_preserves_source_order() -> None:
    measurements, warnings = parse_measurements("3处，L=0.8m", "defect_0005")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["数量", "长度"]


def test_parse_area_product_expression() -> None:
    measurements, warnings = parse_measurements("1处蜂窝、麻面，S=0.6×0.1m²", "defect_0006")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["数量", "面积"]
    assert measurements[1].value == 0.06
    assert measurements[1].unit == "m2"
    assert measurements[1].source_text == "S=0.6×0.1m²"


def test_parse_chinese_length_and_total_area_labels() -> None:
    length_measurements, length_warnings = parse_measurements("勾缝砂浆脱落,长度：5m", "defect_0007")
    area_measurements, area_warnings = parse_measurements("混凝土剥落，破损掉角,总面积：1m²", "defect_0008")

    assert length_warnings == []
    assert [item.dimension_type for item in length_measurements] == ["长度"]
    assert length_measurements[0].value == 5
    assert length_measurements[0].unit == "m"
    assert length_measurements[0].source_text == "长度：5m"
    assert area_warnings == []
    assert [item.dimension_type for item in area_measurements] == ["总面积"]
    assert area_measurements[0].value == 1
    assert area_measurements[0].unit == "m2"
    assert area_measurements[0].source_text == "总面积：1m²"


def test_basic_good_description_is_not_measurement_warning() -> None:
    measurements, warnings = parse_measurements("基本完好", "defect_0009")

    assert measurements == []
    assert warnings == []
