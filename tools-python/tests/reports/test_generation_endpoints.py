"""生成任务三个阶段的端点：C++ 状态机与 Python 之间的接缝（设计 §17.2）。

装配、刷域、校验各是一个可见阶段，所以是三个接口而不是一个 /reports/generate——
界面要说得出"正在装配"还是"正在等待字段更新"。
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from bridge_report_tools.main import app
from bridge_report_tools.reports.contract import PERIODIC_INSPECTION_V1

from tests.reports.context_fixtures import defect_row, part, photo, png
from tests.reports.context_fixtures import context as report_context
from tests.reports.template_fixtures import CORE_ANCHORS, build_builder_template


#: 端点用例要走完整契约：最终校验会核对"必需内容块都装配了"（§20 第 4 条）。
FULL_ANCHORS = list(CORE_ANCHORS)


client = TestClient(app)

office_only = pytest.mark.skipif(
    os.environ.get("BRIDGE_REPORT_OFFICE_TESTS") != "1",
    reason="需要 BRIDGE_REPORT_OFFICE_TESTS=1，且本机装有 Word 或 WPS",
)


@pytest.fixture
def workspace(tmp_path: Path) -> dict:
    """一套完整的装配输入：模板、上下文文件、归档目录和任务临时目录。"""
    archive = tmp_path / "archive"
    (archive / "photos").mkdir(parents=True)
    png(archive / "photos/a.png", 800, 600)
    context = report_context(
        parts=[
            part(
                "SUPERSTRUCTURE",
                "上部结构",
                rows=[defect_row(1, photo_numbers=["照片2.1-1"])],
                photos=[photo("照片2.1-1", "photos/a.png")],
            )
        ]
    )
    job = tmp_path / "job"
    job.mkdir()
    context_path = job / "context.json"
    context_path.write_text(
        json.dumps(json.loads(context.model_dump_json()), ensure_ascii=False),
        encoding="utf-8",
    )
    return {
        "template_path": str(
            build_builder_template(tmp_path / "template.docx", anchors=FULL_ANCHORS)
        ),
        "context_path": str(context_path),
        "archive_root": str(archive),
        "output_path": str(job / "report.docx"),
        "job": job,
    }


def assemble(workspace: dict, **overrides):
    body = {key: workspace[key] for key in
            ("template_path", "context_path", "archive_root", "output_path")}
    body.update(overrides)
    return client.post("/reports/assemble", json=body)


def test_assemble_produces_the_document_and_reports_its_blocks(workspace: dict) -> None:
    response = assemble(workspace)

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["blocks_rendered"] == FULL_ANCHORS
    assert body["missing_placeholders"] == []
    assert Path(body["output_path"]).is_file()
    assert body["elapsed_seconds"] > 0


def test_assemble_refuses_a_context_that_is_not_a_report_context(workspace: dict) -> None:
    """上下文对不上模型说明 C++ 侧取数有问题，必须当场说清，不能带着缺字段往下走。"""
    Path(workspace["context_path"]).write_text('{"inspection_year_id": "x"}', encoding="utf-8")

    response = assemble(workspace)

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "report_context_invalid"


def test_assemble_reports_a_missing_context_file(workspace: dict, tmp_path: Path) -> None:
    response = assemble(workspace, context_path=str(tmp_path / "gone.json"))

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "report_context_missing"


def test_assemble_refuses_to_overwrite_the_template(workspace: dict) -> None:
    """模板是受控归档文件，任何一次生成都不能碰它。"""
    response = assemble(workspace, output_path=workspace["template_path"])

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "report_output_overwrites_template"


def test_output_validation_passes_for_a_freshly_assembled_report(workspace: dict) -> None:
    assembled = assemble(workspace).json()

    response = client.post(
        "/reports/output/validate",
        json={
            "docx_path": assembled["output_path"],
            "job_directory": str(workspace["job"]),
            "blocks_rendered": assembled["blocks_rendered"],
        },
    )

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["status"] == "valid", body["issues"]
    assert body["image_count"] == 1
    assert body["section_count"] >= 1


def test_output_validation_failure_is_200_with_details_not_an_error(
    workspace: dict,
) -> None:
    """"没通过"是正常结果：任务要把每条明细记进错误信息，不是一句 400。"""
    assembled = assemble(workspace).json()

    response = client.post(
        "/reports/output/validate",
        json={
            "docx_path": assembled["output_path"],
            "job_directory": str(workspace["job"]),
            "blocks_rendered": [],
        },
    )

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "invalid"
    assert {issue["code"] for issue in body["issues"]} == {"report_output_block_missing"}


def test_output_validation_reports_a_missing_file_as_an_error(workspace: dict) -> None:
    response = client.post(
        "/reports/output/validate",
        json={
            "docx_path": str(workspace["job"] / "nope.docx"),
            "job_directory": str(workspace["job"]),
            "blocks_rendered": list(PERIODIC_INSPECTION_V1.required_anchors),
        },
    )

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "report_output_missing"


@office_only
def test_field_update_fills_the_toc_and_reports_page_count(workspace: dict) -> None:
    assembled = assemble(workspace).json()
    updated = workspace["job"] / "final.docx"

    response = client.post(
        "/reports/fields/update",
        json={
            "input_path": assembled["output_path"],
            "output_path": str(updated),
            "timeout_seconds": 300,
        },
    )

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["page_count"] is not None and body["page_count"] >= 1
    assert body["updater"]
    assert updated.is_file()
    # 中间文档必须原封不动——失败时要能用同一份输入重试。
    assert Path(assembled["output_path"]).is_file()


@office_only
def test_field_update_reports_a_missing_input(workspace: dict, tmp_path: Path) -> None:
    response = client.post(
        "/reports/fields/update",
        json={
            "input_path": str(tmp_path / "gone.docx"),
            "output_path": str(workspace["job"] / "final.docx"),
        },
    )

    assert response.status_code == 502
    detail = response.json()["detail"]
    assert detail["code"] == "REPORT_FIELD_UPDATE_FAILED"
    assert detail["stage"] == "open"
