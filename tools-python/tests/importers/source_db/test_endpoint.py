import base64
import json
import sqlite3
from datetime import date

import pytest
from fastapi.testclient import TestClient

from bridge_report_tools.contracts.annual_inspection import BridgeAnnualInspectionData
from bridge_report_tools.importers.source_db.context import SourceImportRequest, parse_source_import
from bridge_report_tools.importers.source_db.reader import SourceDatabaseError
from bridge_report_tools.main import app

JPEG = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\xff\xd9"
TASK = "task-1"


def build_source_db(path):
    db = sqlite3.connect(path)
    db.executescript(
        """
        create table tasks (id text primary key, name text, checkDate text);
        create table taskTrees (id text primary key, taskId text, name text, levelCode text,
            nodeType integer, memberCount integer, memberTypeName text);
        create table outerCheckData (id text primary key, taskId text, treeId text, name text,
            data text, pos text, degree integer, judgeIndexId text, judgeTreeId text,
            diseaseDefinition text, "数量" text, "数量单位" text, "长度" text, "长度单位" text,
            "宽度" text, "宽度单位" text, "面积一" text, "面积一单位" text, "走向" text);
        create table images (id text primary key, taskId text, fileName text, contentType text,
            w integer, h integer, ForeignTable text, ForeignKey text, memberNum text);
        create table judgeTree (id text primary key, chapterNum text, name text);
        create table judgeIndex (id text primary key, tableNum text, name text);
        """
    )
    db.execute("insert into tasks values (?,?,?)", (TASK, "百股大桥", "2024-06-21"))
    db.execute("insert into taskTrees values (?,?,?,?,?,?,?)",
               ("t-part", TASK, "上部结构", "001", 1, None, None))
    db.execute("insert into taskTrees values (?,?,?,?,?,?,?)",
               ("t-cat", TASK, "上部承重构件", "001001", 2, 825, None))
    db.execute("insert into taskTrees values (?,?,?,?,?,?,?)",
               ("t-a", TASK, "25-1#板", "001001001", 3, None, "空心板"))
    db.execute("insert into judgeIndex values ('idx-1','5.1.1-2','剥落、掉角')")
    db.execute("insert into judgeTree values ('jt-1','5.1.1','板式构件')")
    db.execute("insert into outerCheckData values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
               ("d-1", TASK, "t-a", "受渗水侵蚀，混凝土剥蚀破损", "受渗水侵蚀，总面积：2.5㎡",
                "左侧翼缘板", 2, "idx-1", "jt-1", "", None, None, None, None, None, None,
                "2.5", "㎡", None))
    db.execute("insert into images values (?,?,?,?,?,?,?,?,?)",
               ("i-1", TASK, "data:image/jpeg;base64," + base64.b64encode(JPEG).decode(),
                "image/jpeg", 1600, 1200, "outerCheckData", "d-1", "25-1#板"))
    db.commit()
    db.close()
    return path


@pytest.fixture()
def request_payload(tmp_path):
    return SourceImportRequest(
        source_db_path=build_source_db(tmp_path / "source.sqlite"),
        task_id=TASK,
        temporary_photo_output_dir=tmp_path / "photos",
        import_mode="已有桥年度导入",
        file_role="当前年度检测资料",
        data_role="当前年度",
        selected_bridge_system_number="QL-000019",
        selected_bridge_name="百股大桥",
        inspection_year=2024,
        inspection_date=date(2024, 6, 21),
        report_number="R-2024-001",
        project_name="2024年国省干线桥梁检测项目",
        archived_file_system_number="GDWJ-000001",
        import_record_system_number="DRJL-000001",
    )


def test_produces_a_valid_contract(request_payload):
    response = parse_source_import(request_payload)

    BridgeAnnualInspectionData.model_validate(response.data.model_dump())
    assert len(response.data.defects) == 1
    assert len(response.data.photos) == 1
    assert response.temporary_photo_files == ["source_photo_0001.jpg"]


def test_marks_the_import_as_coming_through_an_interface(request_payload):
    """契约的枚举里已有「接口同步」，不需要为此改契约。"""
    response = parse_source_import(request_payload)

    assert response.data.import_context.source_type == "接口同步"


def test_carries_the_indicator_scale_and_photo_binding(request_payload):
    response = parse_source_import(request_payload)

    defect = response.data.defects[0]
    assert defect.source_defect_group_id == "jt-1"
    assert defect.source_defect_group_number == "5.1.1"
    assert defect.source_defect_indicator_id == "idx-1"
    assert defect.source_defect_indicator_number == "5.1.1-2"
    assert defect.defect_scale == 2
    assert defect.measurements[0].dimension_type == "面积"
    assert response.data.photos[0].linked_defect_candidate_id == defect.candidate_id
    assert defect.photo_references[0].photo_number == response.data.photos[0].photo_number


def test_leaves_comparison_and_report_text_empty(request_payload):
    """来源库里没有跨年度对比与报告正文；契约允许空数组，不阻塞导入。"""
    response = parse_source_import(request_payload)

    assert response.data.comparison_candidates == []
    assert response.data.report_text_candidates == []


def test_reports_an_unknown_task_with_an_actionable_message(request_payload):
    payload = request_payload.model_copy(update={"task_id": "nope"})

    with pytest.raises(SourceDatabaseError) as excinfo:
        parse_source_import(payload)

    assert excinfo.value.code == "source_task_not_found"
    assert "桌面程序" in excinfo.value.message


def test_reports_a_missing_database(request_payload, tmp_path):
    payload = request_payload.model_copy(update={"source_db_path": tmp_path / "gone.sqlite"})

    with pytest.raises(SourceDatabaseError) as excinfo:
        parse_source_import(payload)

    assert excinfo.value.code == "source_db_not_found"


def test_the_same_input_produces_identical_output(request_payload):
    first = parse_source_import(request_payload)
    second = parse_source_import(request_payload)

    def stripped(response):
        payload = json.loads(response.data.model_dump_json())
        payload["contract"].pop("generated_at")
        return json.dumps(payload, ensure_ascii=False, sort_keys=True)

    assert stripped(first) == stripped(second)


def test_endpoint_returns_the_contract(request_payload):
    client = TestClient(app)

    response = client.post("/imports/source/parse",
                           json=json.loads(request_payload.model_dump_json()))

    assert response.status_code == 200
    body = response.json()
    assert body["data"]["import_context"]["source_type"] == "接口同步"
    assert len(body["temporary_photo_files"]) == 1


def test_endpoint_maps_a_source_error_to_a_coded_400(request_payload):
    client = TestClient(app)
    payload = json.loads(request_payload.model_copy(update={"task_id": "nope"}).model_dump_json())

    response = client.post("/imports/source/parse", json=payload)

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "source_task_not_found"


def test_word_endpoint_is_untouched():
    """新增来源不得影响 Word 那条路。"""
    client = TestClient(app)

    response = client.post("/imports/word/parse", json={})

    assert response.status_code == 422  # 仍按原有请求模型校验


# ---------------------------------------------------------------------------
# 任务列表：taskId 是厂商库里的 UUID，用户不可能手填，界面必须先能列出来选。
# ---------------------------------------------------------------------------

def test_lists_the_tasks_in_the_offline_database(tmp_path):
    client = TestClient(app)
    path = build_source_db(tmp_path / "list.sqlite")

    response = client.post("/imports/source/tasks", json={"source_db_path": str(path)})

    assert response.status_code == 200
    tasks = response.json()["tasks"]
    assert tasks == [{
        "task_id": TASK,
        "name": "百股大桥",
        "check_date": "2024-06-21",
        "defect_count": 1,
        "photo_count": 1,
    }]


def test_task_rows_carry_enough_to_tell_two_years_apart(tmp_path):
    """同一座桥的两个年度只靠桥名分不开；日期与条数才是人能认的依据。"""
    path = build_source_db(tmp_path / "two.sqlite")
    db = sqlite3.connect(path)
    db.execute("insert into tasks values ('t-2','百股大桥','2025-06-20')")
    db.execute("insert into outerCheckData values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
               ("d-2", "t-2", "t-a", "破损", "破损", "", 1, "idx-1", "jt-1", "",
                None, None, None, None, None, None, None, None, None))
    db.commit()
    db.close()

    response = TestClient(app).post("/imports/source/tasks", json={"source_db_path": str(path)})

    rows = response.json()["tasks"]
    assert [r["check_date"] for r in rows] == ["2024-06-21", "2025-06-20"]
    assert [r["defect_count"] for r in rows] == [1, 1]


def test_listing_reports_a_missing_database_with_a_coded_error(tmp_path):
    response = TestClient(app).post(
        "/imports/source/tasks", json={"source_db_path": str(tmp_path / "nope.sqlite")})

    assert response.status_code == 400
    assert response.json()["detail"]["code"] == "source_db_not_found"
