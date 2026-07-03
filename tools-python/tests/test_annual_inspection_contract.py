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


def test_valid_fixture_is_accepted() -> None:
    data = load_fixture("bridge_annual_inspection_data.valid.json")

    model = BridgeAnnualInspectionData.model_validate(data)

    assert model.contract.name == "BridgeAnnualInspectionData"
    assert model.ratings.overall.total_score == 85.61


def test_with_comparison_fixture_is_accepted() -> None:
    data = load_fixture("bridge_annual_inspection_data.with-comparison.json")

    model = BridgeAnnualInspectionData.model_validate(data)

    comparison = model.comparison_candidates[0]
    assert comparison.comparison_type == "原病害发展"
    assert comparison.match_basis.same_component is True
    assert comparison.match_basis.location_similarity == 0.82
    assert comparison.confirmation_status == "待确认"


def test_invalid_evaluation_part_grade_is_rejected() -> None:
    data = load_fixture("bridge_annual_inspection_data.invalid-evaluation-part-grade.json")

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "ratings.evaluation_parts.0.grade" in str(exc_info.value)


def test_defect_confidence_must_be_at_most_one() -> None:
    data = copy.deepcopy(load_fixture("bridge_annual_inspection_data.valid.json"))
    data["defects"][0]["confidence"] = 1.01

    with pytest.raises(ValidationError) as exc_info:
        BridgeAnnualInspectionData.model_validate(data)

    assert "less than or equal to 1" in str(exc_info.value)


def test_export_bridge_annual_inspection_schema(tmp_path: Path) -> None:
    schema_path = tmp_path / "bridge_annual_inspection_data.schema.json"

    export_bridge_annual_inspection_schema(schema_path)

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert schema["title"] == "BridgeAnnualInspectionData"
    assert "ratings" in schema["properties"]
