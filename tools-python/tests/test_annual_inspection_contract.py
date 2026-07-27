import copy
import json
from pathlib import Path

import pytest
from pydantic import ValidationError

from bridge_report_tools.contracts import (
    BridgeAnnualInspectionData,
    export_bridge_annual_inspection_schema,
)


FIXTURE_DIR = Path(__file__).resolve().parents[2] / "samples" / "contracts"


def load_fixture(name: str) -> dict:
    fixture_path = FIXTURE_DIR / name
    return json.loads(fixture_path.read_text(encoding="utf-8"))


def valid_payload() -> dict:
    return copy.deepcopy(load_fixture("bridge_annual_inspection_data.v3.valid.json"))


def delete_path(data: dict, path: tuple[str | int, ...]) -> None:
    current: object = data
    for part in path[:-1]:
        current = current[part]  # type: ignore[index]
    del current[path[-1]]  # type: ignore[index]


def test_valid_version_three_fixture_is_accepted() -> None:
    model = BridgeAnnualInspectionData.model_validate(valid_payload())

    assert model.contract.name == "BridgeAnnualInspectionData"
    assert model.contract.version == "3.0"
    assert model.defects[0].source_structure_part == "上部结构"
    assert model.defects[0].component_number == "2-1#梁"
    assert model.defects[0].bridge_component_id is None
    assert model.defects[0].standard_component_category_id is None
    assert model.defects[0].resolved_structure_part is None
    assert model.defects[0].defect_scale == 2
    assert model.defects[0].source_ref.source_type == "word"
    assert not hasattr(model, "ratings")
    assert not hasattr(model.defects[0], "defect_deduction")


def test_range_split_origin_is_optional_and_validated() -> None:
    data = valid_payload()
    data["defects"][0]["range_split_origin"] = {
        "operation_id": "operation-1",
        "source_candidate_id": "defect_0001",
        "source_component_number": "1-1#板~1-25#板",
        "expanded_component_number": "1-7#板",
        "split_index": 7,
        "split_count": 25,
        "operated_by_user_id": "user-1",
        "operated_at": "2026-07-24T16:00:00+08:00",
    }

    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.defects[0].range_split_origin is not None
    assert model.defects[0].range_split_origin.split_index == 7

    data = valid_payload()
    assert BridgeAnnualInspectionData.model_validate(data).defects[0].range_split_origin is None


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("operation_id", ""),
        ("source_component_number", ""),
        ("split_index", 0),
        ("split_count", 1),
        ("split_index", 26),
    ],
)
def test_range_split_origin_rejects_invalid_values(field: str, value: object) -> None:
    data = valid_payload()
    origin = {
        "operation_id": "operation-1",
        "source_candidate_id": "defect_0001",
        "source_component_number": "1-1#板~1-25#板",
        "expanded_component_number": "1-7#板",
        "split_index": 7,
        "split_count": 25,
        "operated_by_user_id": "user-1",
        "operated_at": "2026-07-24T16:00:00+08:00",
    }
    origin[field] = value
    data["defects"][0]["range_split_origin"] = origin

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "range_split_origin" in str(exc_info.value)


def test_range_split_origin_rejects_extra_fields() -> None:
    data = valid_payload()
    data["defects"][0]["range_split_origin"] = {
        "operation_id": "operation-1",
        "source_candidate_id": "defect_0001",
        "source_component_number": "1-1#板~1-25#板",
        "expanded_component_number": "1-7#板",
        "split_index": 7,
        "split_count": 25,
        "operated_by_user_id": "user-1",
        "operated_at": "2026-07-24T16:00:00+08:00",
        "unexpected": True,
    }
    with pytest.raises(ValidationError):
        BridgeAnnualInspectionData.model_validate(data)


def test_with_comparison_fixture_is_accepted() -> None:
    model = BridgeAnnualInspectionData.model_validate(
        load_fixture("bridge_annual_inspection_data.v3.with-comparison.json")
    )

    assert len(model.comparison_candidates) == 1
    assert model.comparison_candidates[0].comparison_type == "原病害发展"
    assert model.comparison_candidates[0].match_basis is not None


@pytest.mark.parametrize("version", ["1.0", "1.1", "1.2", "2.0", "2.1", "3"])
def test_only_contract_version_three_is_accepted(version: str) -> None:
    data = valid_payload()
    data["contract"]["version"] = version

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "contract.version" in str(exc_info.value)


def test_ratings_are_rejected_in_version_three() -> None:
    data = valid_payload()
    data["ratings"] = {"overall": {"total_score": 85.61}}

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "ratings" in str(exc_info.value)
    assert "Extra inputs are not permitted" in str(exc_info.value)


def test_word_deduction_is_rejected_with_exact_path() -> None:
    data = valid_payload()
    data["defects"][0]["defect_deduction"] = 35

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.defect_deduction" in str(exc_info.value)


@pytest.mark.parametrize("value", [None, 1, 4])
def test_defect_scale_accepts_null_or_positive_integer(value: int | None) -> None:
    data = valid_payload()
    data["defects"][0]["defect_scale"] = value

    model = BridgeAnnualInspectionData.model_validate(data)

    assert model.defects[0].defect_scale == value


@pytest.mark.parametrize("value", [0, -1, 2.5, "2"])
def test_defect_scale_rejects_invalid_values(value: object) -> None:
    data = valid_payload()
    data["defects"][0]["defect_scale"] = value

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.defect_scale" in str(exc_info.value)


def test_manual_source_reference_is_valid_without_word_location() -> None:
    data = valid_payload()
    data["defects"][0]["source_ref"] = {"source_type": "manual"}

    model = BridgeAnnualInspectionData.model_validate(data)

    assert model.defects[0].source_ref.source_type == "manual"
    assert model.defects[0].source_ref.table_index is None
    assert model.defects[0].source_ref.row_index is None


def test_component_match_audit_fields_are_validated() -> None:
    data = valid_payload()
    data["defects"][0].update(
        component_inventory_revision_id="revision-1",
        component_match_candidate_ids=["component-1"],
        component_match_method="manual",
        component_match_confirmed_by="editor",
    )

    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.defects[0].component_match_method == "manual"
    assert model.defects[0].component_match_candidate_ids == ["component-1"]

    data["defects"][0]["component_match_candidate_ids"] = ["component-1", "component-1"]
    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)
    assert "component_match_candidate_ids" in str(exc_info.value)


def test_unknown_source_reference_type_is_rejected() -> None:
    data = valid_payload()
    data["defects"][0]["source_ref"] = {"source_type": "spreadsheet"}

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.source_ref.source_type" in str(exc_info.value)


def test_defect_confidence_must_be_at_most_one() -> None:
    data = valid_payload()
    data["defects"][0]["confidence"] = 1.1

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.confidence" in str(exc_info.value)


def test_bridge_match_status_rejects_unplanned_value() -> None:
    data = valid_payload()
    data["bridge_check"]["match_status"] = "大概匹配"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "bridge_check.match_status" in str(exc_info.value)


def test_historical_official_report_file_role_is_allowed() -> None:
    data = valid_payload()
    data["import_context"]["source_type"] = "正式Word"
    data["import_context"]["file_role"] = "历史正式报告"

    model = BridgeAnnualInspectionData.model_validate(data)

    assert model.import_context.file_role == "历史正式报告"


@pytest.mark.parametrize("field_name", ["group_review_status", "photo_references"])
def test_defect_group_review_fields_are_required(field_name: str) -> None:
    data = valid_payload()
    del data["defects"][0][field_name]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert f"defects.0.{field_name}" in str(exc_info.value)


def test_duplicate_photo_reference_numbers_are_rejected() -> None:
    data = valid_payload()
    data["defects"][0]["photo_references"].append(
        copy.deepcopy(data["defects"][0]["photo_references"][0])
    )

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.photo_references" in str(exc_info.value)


@pytest.mark.parametrize(
    ("resolution", "photo_candidate_id", "resolved_defect_candidate_id"),
    [
        ("pending", "photo_0001", None),
        ("matched", None, "defect_0001"),
        ("relinked", "photo_0001", None),
        ("missing", "photo_0001", None),
        ("unrelated", None, None),
    ],
)
def test_photo_reference_rejects_invalid_target_combinations(
    resolution: str,
    photo_candidate_id: str | None,
    resolved_defect_candidate_id: str | None,
) -> None:
    data = valid_payload()
    reference = data["defects"][0]["photo_references"][0]
    reference["resolution"] = resolution
    reference["photo_candidate_id"] = photo_candidate_id
    reference["resolved_defect_candidate_id"] = resolved_defect_candidate_id

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.photo_references.0" in str(exc_info.value)


@pytest.mark.parametrize("field_name", ["photo_numbers", "confirmed_missing_photo_numbers"])
def test_removed_photo_fields_are_rejected(field_name: str) -> None:
    data = valid_payload()
    data["defects"][0][field_name] = []

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert f"defects.0.{field_name}" in str(exc_info.value)


@pytest.mark.parametrize(
    "field_name",
    [
        "defects",
        "photos",
        "comparison_candidates",
        "report_text_candidates",
        "warnings",
        "errors",
    ],
)
def test_top_level_candidate_and_issue_lists_are_required(field_name: str) -> None:
    data = valid_payload()
    del data[field_name]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert field_name in str(exc_info.value)


@pytest.mark.parametrize(
    ("fixture_name", "path", "error_path"),
    [
        ("bridge_annual_inspection_data.v3.valid.json", ("bridge_check", "warnings"), "bridge_check.warnings"),
        ("bridge_annual_inspection_data.v3.valid.json", ("defects", 0, "measurements"), "defects.0.measurements"),
        ("bridge_annual_inspection_data.v3.valid.json", ("defects", 0, "photo_references"), "defects.0.photo_references"),
        ("bridge_annual_inspection_data.v3.valid.json", ("defects", 0, "warnings"), "defects.0.warnings"),
        ("bridge_annual_inspection_data.v3.valid.json", ("photos", 0, "warnings"), "photos.0.warnings"),
        (
            "bridge_annual_inspection_data.v3.with-comparison.json",
            ("comparison_candidates", 0, "warnings"),
            "comparison_candidates.0.warnings",
        ),
        ("bridge_annual_inspection_data.v3.valid.json", ("defects", 0, "source_ref"), "defects.0.source_ref"),
    ],
)
def test_nested_arrays_and_source_refs_are_required(
    fixture_name: str,
    path: tuple[str | int, ...],
    error_path: str,
) -> None:
    data = copy.deepcopy(load_fixture(fixture_name))
    delete_path(data, path)

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert error_path in str(exc_info.value)


@pytest.mark.parametrize(
    "measurement",
    [
        {
            "dimension_type": "长度",
            "value_type": "range",
            "value": None,
            "minimum_value": 4.0,
            "maximum_value": 0.5,
            "unit": "m",
            "is_approximate": False,
            "source_text": "4.0~0.5m",
        },
        {
            "dimension_type": "长度",
            "value_type": "single",
            "value": 1.0,
            "minimum_value": 0.5,
            "maximum_value": None,
            "unit": "m",
            "is_approximate": False,
            "source_text": "1.0m",
        },
        {
            "dimension_type": "长度",
            "value_type": "range",
            "value": 1.0,
            "minimum_value": 0.5,
            "maximum_value": 4.0,
            "unit": "m",
            "is_approximate": False,
            "source_text": "0.5~4.0m",
        },
    ],
)
def test_measurement_single_range_invariants_are_enforced(measurement: dict) -> None:
    data = valid_payload()
    data["defects"][0]["measurements"] = [measurement]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.measurements.0" in str(exc_info.value)


def test_export_bridge_annual_inspection_schema(tmp_path: Path) -> None:
    schema_path = tmp_path / "bridge_annual_inspection_data.schema.json"

    export_bridge_annual_inspection_schema(schema_path)

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    properties = schema["properties"]
    defect_properties = schema["$defs"]["DefectCandidate"]["properties"]
    source_properties = schema["$defs"]["SourceRef"]["properties"]
    measurement_properties = schema["$defs"]["Measurement"]["properties"]

    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert schema["title"] == "BridgeAnnualInspectionData"
    assert "ratings" not in properties
    assert "ratings" not in schema["required"]
    assert "defect_deduction" not in defect_properties
    assert defect_properties["bridge_component_id"]["default"] is None
    assert defect_properties["standard_component_category_id"]["default"] is None
    assert defect_properties["resolved_structure_part"]["default"] is None
    assert set(source_properties["source_type"]["enum"]) == {"word", "manual"}
    assert set(measurement_properties["value_type"]["enum"]) == {"single", "range"}
    assert measurement_properties["minimum_value"]["default"] is None
    assert measurement_properties["maximum_value"]["default"] is None
    assert measurement_properties["is_approximate"]["default"] is False
