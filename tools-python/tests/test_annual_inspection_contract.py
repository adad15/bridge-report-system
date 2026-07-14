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


def load_fixture_text(name: str) -> str:
    fixture_path = FIXTURE_DIR / name
    return fixture_path.read_text(encoding="utf-8")


def valid_payload() -> dict:
    payload = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    payload["contract"]["version"] = "1.2"
    payload["defects"][0]["group_review_status"] = "待确认"
    payload["defects"][0]["confirmed_missing_photo_numbers"] = []
    return payload


def collect_schema_property_names(node: object) -> set[str]:
    names: set[str] = set()
    if isinstance(node, dict):
        properties = node.get("properties")
        if isinstance(properties, dict):
            names.update(properties)
        for value in node.values():
            names.update(collect_schema_property_names(value))
    elif isinstance(node, list):
        for item in node:
            names.update(collect_schema_property_names(item))
    return names


def delete_path(data: dict, path: tuple[str | int, ...]) -> None:
    current: object = data
    for part in path[:-1]:
        current = current[part]  # type: ignore[index]
    del current[path[-1]]  # type: ignore[index]


def test_valid_fixture_is_accepted() -> None:
    data = load_fixture("bridge_annual_inspection_data.valid.json")
    fixture_text = load_fixture_text("bridge_annual_inspection_data.valid.json")

    model = BridgeAnnualInspectionData.model_validate(data)

    assert '"extracted_bridge_name"' in fixture_text
    assert "extracted_bridge\\u005fname" not in fixture_text
    assert model.contract.name == "BridgeAnnualInspectionData"
    assert model.import_context.file_role == "当前年度检测资料"
    assert model.bridge_check.selected_bridge_system_number == "QL-000001"
    assert model.bridge_check.extracted_bridge_name == "绕阳河二号桥"
    assert model.bridge_check.match_status == "匹配"
    assert model.defects[0].structure_part == "上部结构"
    assert model.defects[0].source_ref.chapter == "第二章病害"
    assert model.defects[0].source_ref.table_title == "病害记录表"
    assert model.defects[0].source_ref.file_role == "当前年度检测资料"
    assert model.defects[0].measurements[0].dimension_type == "长度"
    assert model.photos[0].candidate_id == "photo_0001"
    assert model.ratings.overall.total_score == 85.61
    assert model.ratings.structure_parts[0].structure_score == 87.45
    assert model.ratings.structure_parts[0].grade == "2"
    assert model.ratings.structure_parts[2].structure_score == 79.93
    assert model.ratings.structure_parts[2].grade == "3"
    assert model.ratings.evaluation_parts[0].category_no == 1
    assert model.ratings.evaluation_parts[0].part_score == 86.62
    assert model.ratings.evaluation_parts[1].part_score == 82.29
    assert model.ratings.evaluation_parts[2].part_score == 100
    assert not hasattr(model.ratings.evaluation_parts[0], "grade")
    assert model.ratings.warnings == []
    assert model.report_text_candidates == []


def test_with_comparison_fixture_is_accepted() -> None:
    data = load_fixture("bridge_annual_inspection_data.with-comparison.json")
    valid_data = load_fixture("bridge_annual_inspection_data.valid.json")

    model = BridgeAnnualInspectionData.model_validate(data)

    data_without_comparison = copy.deepcopy(data)
    data_without_comparison["comparison_candidates"] = []
    assert data_without_comparison == valid_data
    comparison = model.comparison_candidates[0]
    assert comparison.comparison_type == "原病害发展"
    assert comparison.match_basis.same_component is True
    assert comparison.match_basis.location_similarity == 0.82
    assert comparison.confirmation_status == "待确认"


def test_invalid_evaluation_part_grade_is_rejected() -> None:
    data = load_fixture("bridge_annual_inspection_data.invalid-evaluation-part-grade.json")
    valid_data = load_fixture("bridge_annual_inspection_data.valid.json")

    data_without_grade = copy.deepcopy(data)
    del data_without_grade["ratings"]["evaluation_parts"][0]["grade"]
    assert data_without_grade == valid_data
    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "ratings.evaluation_parts.0.grade" in str(exc_info.value)


def test_defect_confidence_must_be_at_most_one() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    data["defects"][0]["confidence"] = 1.01

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "less than or equal to 1" in str(exc_info.value)


def test_bridge_match_status_rejects_unplanned_value() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    data["bridge_check"]["match_status"] = "已匹配"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "bridge_check.match_status" in str(exc_info.value)


def test_historical_official_report_file_role_is_allowed() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    data["import_context"]["file_role"] = "历史正式报告"

    model = BridgeAnnualInspectionData.model_validate(data)

    assert model.import_context.file_role == "历史正式报告"


def test_contract_version_one_two_is_accepted() -> None:
    model = BridgeAnnualInspectionData.model_validate(valid_payload())

    assert model.contract.version == "1.2"


@pytest.mark.parametrize("version", ["1.0", "1.1"])
def test_contract_old_versions_are_rejected(version: str) -> None:
    data = valid_payload()
    data["contract"]["version"] = version

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "contract.version" in str(exc_info.value)


def test_defect_scale_and_deduction_accept_null_and_values() -> None:
    data = valid_payload()
    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.defects[0].defect_scale == 2
    assert model.defects[0].defect_deduction == 35.0

    data["defects"][0]["defect_scale"] = None
    data["defects"][0]["defect_deduction"] = None
    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.defects[0].defect_scale is None
    assert model.defects[0].defect_deduction is None


@pytest.mark.parametrize(
    ("field_name", "value"),
    [
        ("defect_scale", 0),
        ("defect_scale", -1),
        ("defect_scale", "warning"),
        ("defect_deduction", -5),
        ("defect_deduction", 100.5),
    ],
)
def test_defect_scale_and_deduction_reject_invalid_values(
    field_name: str, value: object
) -> None:
    data = valid_payload()
    data["defects"][0][field_name] = value

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert f"defects.0.{field_name}" in str(exc_info.value)


def test_component_ratings_is_required() -> None:
    data = valid_payload()
    del data["ratings"]["component_ratings"]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "ratings.component_ratings" in str(exc_info.value)


def test_invalid_component_rating_status_fixture_is_rejected() -> None:
    data = load_fixture("bridge_annual_inspection_data.invalid-component-rating-status.json")
    valid_data = load_fixture("bridge_annual_inspection_data.valid.json")

    data_with_consistent_status = copy.deepcopy(data)
    data_with_consistent_status["ratings"]["component_ratings"][0][
        "score_validation_status"
    ] = "一致"
    assert data_with_consistent_status == valid_data
    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "confirmed_score 必须为空" in str(exc_info.value)


def test_unresolved_component_rating_must_not_carry_reason() -> None:
    data = valid_payload()
    rating = data["ratings"]["component_ratings"][0]
    rating["score_validation_status"] = "无法复算"
    rating["confirmed_score"] = None
    rating["score_resolution_reason"] = "顺手写的原因"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "score_resolution_reason 必须为空" in str(exc_info.value)


@pytest.mark.parametrize("status", ["人工接受Word值", "人工采用复算值"])
def test_manual_resolution_requires_confirmed_score_and_reason(status: str) -> None:
    data = valid_payload()
    rating = data["ratings"]["component_ratings"][0]
    rating["score_validation_status"] = status
    rating["confirmed_score"] = None
    rating["score_resolution_reason"] = None

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "confirmed_score 不能为空" in str(exc_info.value)

    rating["confirmed_score"] = 65
    rating["score_resolution_reason"] = "  "
    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "必须填写 score_resolution_reason" in str(exc_info.value)

    rating["score_resolution_reason"] = "现场复核采用 Word 分值"
    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.ratings.component_ratings[0].score_validation_status == status


def test_consistent_component_rating_rejects_reason() -> None:
    data = valid_payload()
    data["ratings"]["component_ratings"][0]["score_resolution_reason"] = "不该有原因"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "为一致时 score_resolution_reason 必须为空" in str(exc_info.value)


@pytest.mark.parametrize(
    "field_name",
    ["group_review_status", "confirmed_missing_photo_numbers"],
)
def test_defect_group_review_fields_are_required(field_name: str) -> None:
    payload = valid_payload()
    del payload["defects"][0][field_name]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(payload)

    assert f"defects.0.{field_name}" in str(exc_info.value)


def test_defect_group_review_status_rejects_unplanned_value() -> None:
    payload = valid_payload()
    payload["defects"][0]["group_review_status"] = "非法状态"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(payload)

    assert "Input should be '待确认' or '已确认'" in str(exc_info.value)


@pytest.mark.parametrize(
    "value",
    [
        ["2.1-2", "2.1-2"],
        [1],
    ],
)
def test_confirmed_missing_photo_numbers_reject_invalid_values(value: list[object]) -> None:
    payload = valid_payload()
    payload["defects"][0]["confirmed_missing_photo_numbers"] = value

    with pytest.raises(ValidationError):
        BridgeAnnualInspectionData.model_validate(payload)


@pytest.mark.parametrize(
    "field_name",
    ["comparison_candidates", "report_text_candidates", "warnings", "errors"],
)
def test_top_level_candidate_and_issue_lists_are_required(field_name: str) -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    del data[field_name]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert field_name in str(exc_info.value)


def test_ratings_warnings_is_required() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    del data["ratings"]["warnings"]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "ratings.warnings" in str(exc_info.value)


def test_report_text_candidate_review_status_is_required() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    data["report_text_candidates"] = [
        {
            "candidate_id": "text_0001",
            "section_key": "inspection.summary",
            "section_title": "检查概述",
            "text": "绕阳河二号桥年度检查概述。",
            "usage": "正文候选",
            "source_ref": None,
        }
    ]

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "report_text_candidates.0.review_status" in str(exc_info.value)


@pytest.mark.parametrize(
    ("fixture_name", "path", "error_path"),
    [
        (
            "bridge_annual_inspection_data.valid.json",
            ("bridge_check", "warnings"),
            "bridge_check.warnings",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("defects", 0, "measurements"),
            "defects.0.measurements",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("defects", 0, "photo_numbers"),
            "defects.0.photo_numbers",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("defects", 0, "warnings"),
            "defects.0.warnings",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("photos", 0, "warnings"),
            "photos.0.warnings",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("ratings", "evaluation_parts", 0, "score_rows"),
            "ratings.evaluation_parts.0.score_rows",
        ),
        (
            "bridge_annual_inspection_data.with-comparison.json",
            ("comparison_candidates", 0, "warnings"),
            "comparison_candidates.0.warnings",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("defects", 0, "source_ref"),
            "defects.0.source_ref",
        ),
        (
            "bridge_annual_inspection_data.valid.json",
            ("ratings", "overall", "source_ref"),
            "ratings.overall.source_ref",
        ),
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


def test_export_bridge_annual_inspection_schema(tmp_path: Path) -> None:
    schema_path = tmp_path / "bridge_annual_inspection_data.schema.json"

    export_bridge_annual_inspection_schema(schema_path)

    schema_text = schema_path.read_text(encoding="utf-8")
    schema = json.loads(schema_text)
    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert schema["title"] == "BridgeAnnualInspectionData"
    assert "ratings" in schema["properties"]
    assert set(schema["required"]) == {
        "contract",
        "import_context",
        "bridge_check",
        "inspection",
        "defects",
        "photos",
        "ratings",
        "comparison_candidates",
        "report_text_candidates",
        "warnings",
        "errors",
    }
    assert "warnings" in schema["$defs"]["Ratings"]["required"]
    assert "warnings" in schema["$defs"]["BridgeCheck"]["required"]
    assert {
        "measurements",
        "photo_numbers",
        "group_review_status",
        "confirmed_missing_photo_numbers",
        "source_ref",
        "warnings",
    }.issubset(set(schema["$defs"]["DefectCandidate"]["required"]))
    assert {"source_ref", "warnings"}.issubset(
        set(schema["$defs"]["PhotoCandidate"]["required"])
    )
    assert {"score_rows", "source_ref"}.issubset(
        set(schema["$defs"]["EvaluationPartRating"]["required"])
    )
    assert "warnings" in schema["$defs"]["ComparisonCandidate"]["required"]
    version_schema = schema["$defs"]["ContractInfo"]["properties"]["version"]
    assert version_schema.get("const") == "1.2" or version_schema.get("enum") == ["1.2"]
    assert "component_ratings" in schema["$defs"]["Ratings"]["required"]
    assert {
        "candidate_id",
        "component_ref",
        "score_validation_status",
        "deduction_defect_candidate_ids",
        "review_status",
        "warnings",
    }.issubset(set(schema["$defs"]["ComponentRatingCandidate"]["required"]))
    assert {"standard", "ordered_deductions", "rounding_scale"}.issubset(
        set(schema["$defs"]["ComponentScoreCalculationDetails"]["required"])
    )
    missing_photo_numbers_schema = schema["$defs"]["DefectCandidate"]["properties"][
        "confirmed_missing_photo_numbers"
    ]
    assert missing_photo_numbers_schema["uniqueItems"] is True
    assert '"extracted_bridge_name"' in schema_text
    assert "extracted_bridge\\u005fname" not in schema_text
    property_names = collect_schema_property_names(schema)
    assert "extracted_bridge_name" in property_names
    for planned_field in [
        "chapter",
        "table_title",
        "table_index",
        "row_index",
        "column_name",
        "raw_row_text",
        "photo_area_caption",
        "file_role",
        "paragraph_index",
        "target_candidate_id",
        "warnings",
        "section_key",
        "section_title",
    ]:
        assert planned_field in property_names
    for old_field in [
        "bridge_name",
        "bridge_code",
        "route_name",
        "source_documents",
        "data_scope",
        "source_document_name",
        "photo_id",
        "inspection_type",
        "item_name",
        "deduction",
        "source_document_id",
        "source_chapter",
        "source_table",
        "source_row",
        "raw_text",
        "path",
    ]:
        assert old_field not in property_names
