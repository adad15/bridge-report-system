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
    return copy.deepcopy(load_fixture("bridge_annual_inspection_data.v5.valid.json"))


def delete_path(data: dict, path: tuple[str | int, ...]) -> None:
    current: object = data
    for part in path[:-1]:
        current = current[part]  # type: ignore[index]
    del current[path[-1]]  # type: ignore[index]


def test_valid_version_five_fixture_is_accepted() -> None:
    model = BridgeAnnualInspectionData.model_validate(valid_payload())

    assert model.contract.name == "BridgeAnnualInspectionData"
    assert model.contract.version == "5.0"
    assert model.defects[0].source_structure_part == "上部结构"
    assert model.defects[0].component_number == "2-1#梁"
    assert model.defects[0].defect_scale == 2
    assert model.defects[0].source_ref.source_type == "word"
    assert not hasattr(model, "ratings")
    assert not hasattr(model.defects[0], "defect_deduction")
    assert not hasattr(model.photos[0], "match_status")
    assert not hasattr(model.photos[0], "review_status")


# 5.0 把构件解析与评分树解析整体搬进关系表。这些字段一个都不能再被合同接受：
# 静默忽略等于让旧客户端的草稿继续把陈旧解析结果回传，正好是本次拆分要根除的。
RESOLUTION_FIELDS_REMOVED_IN_V5 = [
    ("bridge_component_id", "component-1"),
    ("standard_component_category_id", "category-1"),
    ("resolved_structure_part", "上部结构"),
    ("component_inventory_revision_id", "revision-1"),
    ("component_match_candidate_ids", ["component-1"]),
    ("component_match_method", "manual"),
    ("component_match_confirmed_by", "user-1"),
    ("rating_tree_version_id", "tree-1"),
    ("rating_tree_node_id", "node-1"),
    ("rating_tree_match_method", "manual"),
    ("rating_tree_match_evidence", "人工选择"),
    ("standard_defect_indicator_id", "indicator-1"),
    (
        "range_split_origin",
        {
            "operation_id": "operation-1",
            "source_candidate_id": "defect_0001",
            "source_component_number": "1-1#板~1-25#板",
            "expanded_component_number": "1-7#板",
            "split_index": 7,
            "split_count": 25,
            "operated_by_user_id": "user-1",
            "operated_at": "2026-07-24T16:00:00+08:00",
        },
    ),
]


@pytest.mark.parametrize(
    "field_name,value",
    RESOLUTION_FIELDS_REMOVED_IN_V5,
    ids=[name for name, _ in RESOLUTION_FIELDS_REMOVED_IN_V5],
)
def test_resolution_fields_are_rejected_in_version_five(
    field_name: str, value: object
) -> None:
    data = valid_payload()
    data["defects"][0][field_name] = value

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    message = str(exc_info.value)
    assert f"defects.0.{field_name}" in message
    assert "Extra inputs are not permitted" in message


def test_version_four_fixture_is_rejected_whole() -> None:
    """4.0 不是"多几个字段的 5.0"，整份都得被挡下来，不能只挑版本号报错。"""

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(
            load_fixture("bridge_annual_inspection_data.v4.valid.json")
        )

    message = str(exc_info.value)
    assert "contract.version" in message
    assert "defects.0.bridge_component_id" in message
    assert "defects.0.rating_tree_node_id" in message


def test_with_comparison_fixture_is_accepted() -> None:
    model = BridgeAnnualInspectionData.model_validate(
        load_fixture("bridge_annual_inspection_data.v5.with-comparison.json")
    )

    assert len(model.comparison_candidates) == 1
    assert model.comparison_candidates[0].comparison_type == "原病害发展"
    assert model.comparison_candidates[0].match_basis is not None


@pytest.mark.parametrize(
    "version", ["1.0", "1.1", "1.2", "2.0", "2.1", "3.0", "4.0", "4", "5"]
)
def test_only_contract_version_five_is_accepted(version: str) -> None:
    data = valid_payload()
    data["contract"]["version"] = version

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "contract.version" in str(exc_info.value)


def test_ratings_are_rejected_in_version_five() -> None:
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


def test_source_indicator_identity_is_optional_and_validated() -> None:
    """来源分组/指标身份不是评分树解析结果，5.0 里继续留在来源事实中。"""

    data = valid_payload()
    data["defects"][0].update(
        source_defect_group_id="source-group-1",
        source_defect_group_number="9.1.2",
        source_defect_indicator_id="source-indicator-1",
        source_defect_indicator_number="9.1.2-1",
    )

    model = BridgeAnnualInspectionData.model_validate(data)
    assert model.defects[0].source_defect_group_id == "source-group-1"
    assert model.defects[0].source_defect_group_number == "9.1.2"
    assert model.defects[0].source_defect_indicator_id == "source-indicator-1"
    assert model.defects[0].source_defect_indicator_number == "9.1.2-1"

    for field_name in (
        "source_defect_group_id",
        "source_defect_group_number",
        "source_defect_indicator_id",
        "source_defect_indicator_number",
    ):
        invalid = valid_payload()
        invalid["defects"][0][field_name] = "   "
        with pytest.raises(ValidationError) as exc_info:
            BridgeAnnualInspectionData.model_validate(invalid)
        assert f"defects.0.{field_name}" in str(exc_info.value)


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


@pytest.mark.parametrize("field_name", ["match_status", "review_status"])
def test_legacy_photo_review_statuses_are_rejected(field_name: str) -> None:
    data = valid_payload()
    data["photos"][0][field_name] = "已确认"

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert f"photos.0.{field_name}" in str(exc_info.value)


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


def test_photo_number_may_be_omitted_when_the_photo_is_already_bound() -> None:
    """来源软件导入没有照片编号：照片用外键直绑，配对走 photo_candidate_id。"""
    data = valid_payload()
    defect_id = data["defects"][0]["candidate_id"]
    reference = data["defects"][0]["photo_references"][0]
    reference["resolution"] = "matched"
    reference["photo_candidate_id"] = data["photos"][0]["candidate_id"]
    reference["resolved_defect_candidate_id"] = defect_id
    reference.pop("photo_number", None)

    parsed = BridgeAnnualInspectionData.model_validate(data)

    assert parsed.defects[0].photo_references[0].photo_number is None


@pytest.mark.parametrize("resolution", ["pending", "missing"])
def test_photo_number_is_required_when_no_photo_is_bound(resolution: str) -> None:
    """待核对和原报告缺图这两种状态下没有照片实体可指，编号是唯一标识。"""
    data = valid_payload()
    reference = data["defects"][0]["photo_references"][0]
    reference["resolution"] = resolution
    reference["photo_candidate_id"] = None
    reference["resolved_defect_candidate_id"] = None
    reference.pop("photo_number", None)

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.photo_references.0" in str(exc_info.value)


def test_blank_photo_number_is_rejected_rather_than_treated_as_absent() -> None:
    data = valid_payload()
    data["defects"][0]["photo_references"][0]["photo_number"] = "   "

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "defects.0.photo_references.0" in str(exc_info.value)


def test_two_unnumbered_references_are_not_treated_as_duplicates() -> None:
    """一条病害挂多张来源库照片时，编号全为空不能被当成重复引用。"""
    data = valid_payload()
    defect_id = data["defects"][0]["candidate_id"]
    first = data["defects"][0]["photo_references"][0]
    first["resolution"] = "matched"
    first["photo_candidate_id"] = data["photos"][0]["candidate_id"]
    first["resolved_defect_candidate_id"] = defect_id
    first.pop("photo_number", None)

    second = copy.deepcopy(first)
    second["photo_candidate_id"] = "photo_0002"
    data["defects"][0]["photo_references"].append(second)

    extra_photo = copy.deepcopy(data["photos"][0])
    extra_photo["candidate_id"] = "photo_0002"
    extra_photo.pop("photo_number", None)
    data["photos"].append(extra_photo)

    parsed = BridgeAnnualInspectionData.model_validate(data)

    assert len(parsed.defects[0].photo_references) == 2


def test_duplicate_photo_candidate_ids_are_rejected() -> None:
    data = valid_payload()
    reference = data["defects"][0]["photo_references"][0]
    reference["resolution"] = "matched"
    reference.pop("photo_number", None)
    data["defects"][0]["photo_references"].append(copy.deepcopy(reference))

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
        ("bridge_annual_inspection_data.v5.valid.json", ("bridge_check", "warnings"), "bridge_check.warnings"),
        ("bridge_annual_inspection_data.v5.valid.json", ("defects", 0, "measurements"), "defects.0.measurements"),
        ("bridge_annual_inspection_data.v5.valid.json", ("defects", 0, "photo_references"), "defects.0.photo_references"),
        ("bridge_annual_inspection_data.v5.valid.json", ("defects", 0, "warnings"), "defects.0.warnings"),
        ("bridge_annual_inspection_data.v5.valid.json", ("photos", 0, "warnings"), "photos.0.warnings"),
        (
            "bridge_annual_inspection_data.v5.with-comparison.json",
            ("comparison_candidates", 0, "warnings"),
            "comparison_candidates.0.warnings",
        ),
        ("bridge_annual_inspection_data.v5.valid.json", ("defects", 0, "source_ref"), "defects.0.source_ref"),
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
    # 5.0 的导出 schema 是 C++ 与前端契约的共同来源。这些字段必须一并消失，
    # 否则下游会照着 schema 重新长出解析字段。
    for removed_field, _ in RESOLUTION_FIELDS_REMOVED_IN_V5:
        assert removed_field not in defect_properties
    assert "RangeSplitOrigin" not in schema["$defs"]
    assert defect_properties["source_defect_group_id"]["default"] is None
    assert defect_properties["source_defect_group_number"]["default"] is None
    assert defect_properties["source_defect_indicator_id"]["default"] is None
    assert defect_properties["source_defect_indicator_number"]["default"] is None
    assert schema["$defs"]["ContractInfo"]["properties"]["version"]["const"] == "5.0"
    assert set(source_properties["source_type"]["enum"]) == {"word", "manual"}
    assert set(measurement_properties["value_type"]["enum"]) == {"single", "range"}
    assert measurement_properties["minimum_value"]["default"] is None
    assert measurement_properties["maximum_value"]["default"] is None
    assert measurement_properties["is_approximate"]["default"] is False
