"""模板校验端点：C++ 后端与 Python 之间的接缝。"""

from __future__ import annotations

from pathlib import Path

from fastapi.testclient import TestClient

from bridge_report_tools.main import app
from tests.reports.template_fixtures import (
    CORE_ANCHORS,
    NUMBER_FORMATS,
    add_complex_field,
    build_template,
    valid_config,
)


client = TestClient(app)


def payload(path: Path, **overrides) -> dict:
    body = {
        "template_path": str(path),
        "table_number_formats": dict(NUMBER_FORMATS),
        "required_personnel_roles": list(valid_config().required_personnel_roles),
    }
    body.update(overrides)
    return body


def test_valid_template_returns_document_order(tmp_path: Path) -> None:
    template = build_template(tmp_path / "t.docx")

    response = client.post("/reports/templates/validate", json=payload(template))

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "valid"
    assert body["issues"] == []
    assert body["anchors_in_document_order"] == list(CORE_ANCHORS)
    assert body["fields_used"] == ["PAGEREF", "TOC"]


def test_invalid_template_is_200_with_details_not_an_error(tmp_path: Path) -> None:
    """校验不通过是正常结果：管理员要看到每条问题出在模板哪里，不是一句 400。"""

    def customize(document) -> None:
        add_complex_field(document.add_paragraph(), " SEQ 表 \\* ARABIC ", "1")

    template = build_template(tmp_path / "t.docx", customize=customize)

    response = client.post("/reports/templates/validate", json=payload(template))

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "invalid"
    codes = [issue["code"] for issue in body["issues"]]
    assert "template_field_rejected" in codes
    assert all(issue["severity"] in ("error", "warning") for issue in body["issues"])


def test_missing_number_format_is_reported(tmp_path: Path) -> None:
    template = build_template(tmp_path / "t.docx")

    response = client.post(
        "/reports/templates/validate",
        json=payload(template, table_number_formats={}),
    )

    body = response.json()
    assert body["status"] == "invalid"
    assert [issue["code"] for issue in body["issues"]].count(
        "template_number_format_missing"
    ) == len(NUMBER_FORMATS)


def test_non_docx_file_is_rejected_up_front(tmp_path: Path) -> None:
    broken = tmp_path / "not-a-template.docx"
    broken.write_bytes(b"definitely not a zip")

    response = client.post("/reports/templates/validate", json=payload(broken))

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "template_not_docx"


def test_missing_file_is_rejected(tmp_path: Path) -> None:
    response = client.post(
        "/reports/templates/validate", json=payload(tmp_path / "nope.docx")
    )

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "template_file_missing"


def test_unknown_contract_type_is_rejected(tmp_path: Path) -> None:
    template = build_template(tmp_path / "t.docx")

    response = client.post(
        "/reports/templates/validate",
        json=payload(template, contract_type="special_inspection_v9"),
    )

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "template_contract_unknown"


def test_non_docx_extension_is_rejected_by_the_request_model(tmp_path: Path) -> None:
    other = tmp_path / "template.doc"
    other.write_bytes(b"x")

    response = client.post("/reports/templates/validate", json=payload(other))

    assert response.status_code == 422
