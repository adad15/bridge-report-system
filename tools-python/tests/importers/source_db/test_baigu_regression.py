"""百股大桥三个年度的验收回归。

跑这个需要一份离线库快照——那是几百 MB 的本机文件，不进仓库、也不进 CI。
设好环境变量再跑：

    $env:BRIDGE_REPORT_SOURCE_DB_SNAPSHOT = "C:\\...\\source-snapshot.sqlite"
    .venv/Scripts/python.exe -m pytest tests/importers/source_db/test_baigu_regression.py

没设就整体跳过；这些断言守的是"真实数据上还成立吗"，不是解析逻辑本身——
逻辑的单测在 test_defects.py / test_photos.py 里，那些不依赖任何外部文件。
"""

from __future__ import annotations

import json
import os
import re
from collections import defaultdict
from datetime import date
from pathlib import Path

import pytest

from bridge_report_tools.importers.source_db.context import (
    SourceImportRequest, parse_source_import)

SNAPSHOT = os.environ.get("BRIDGE_REPORT_SOURCE_DB_SNAPSHOT", "")

pytestmark = pytest.mark.skipif(
    not SNAPSHOT or not Path(SNAPSHOT).is_file(),
    reason="需要 BRIDGE_REPORT_SOURCE_DB_SNAPSHOT 指向一份离线库快照")

#: (任务 id, 年度, 检测日期, 病害数, 照片数)。数量写死是故意的——
#: 对着固定快照跑，数量变了就说明解析行为变了，必须有人看一眼。
YEARS = [
    ("4ec7bd71-2c0c-4f19-9ba1-0d1a17b44a20", 2024, date(2024, 6, 21), 279, 166),
    ("7ce48f01-cdae-41fc-826b-e4163cd591d4", 2025, date(2025, 6, 20), 314, 253),
    ("ccaa788b-52b6-4569-8a16-c7bccab8219d", 2026, date(2026, 3, 18), 381, 455),
]


def build(task_id: str, year: int, checked: date, output: Path):
    return parse_source_import(SourceImportRequest(
        source_db_path=Path(SNAPSHOT),
        task_id=task_id,
        temporary_photo_output_dir=output,
        import_mode="已有桥年度导入",
        file_role="当前年度检测资料",
        data_role="当前年度",
        selected_bridge_system_number="QL-000019",
        selected_bridge_name="百股大桥",
        inspection_year=year,
        inspection_date=checked,
        report_number=f"BG-{year}-001",
        project_name=f"百股大桥{year}年度定期检测",
        archived_file_system_number="LSWJ-000001",
        import_record_system_number=f"DRJL-{year}",
    ))


@pytest.fixture(scope="module")
def imports(tmp_path_factory):
    root = tmp_path_factory.mktemp("baigu")
    return {
        year: (build(task_id, year, checked, root / str(year)), defects, photos)
        for task_id, year, checked, defects, photos in YEARS
    }


@pytest.mark.parametrize("year", [year for _, year, _, _, _ in YEARS])
def test_counts_match_the_source_database(imports, year):
    response, expected_defects, expected_photos = imports[year]

    assert len(response.data.defects) == expected_defects
    assert len(response.data.photos) == expected_photos


@pytest.mark.parametrize("year", [year for _, year, _, _, _ in YEARS])
def test_every_defect_carries_the_source_indicator(imports, year):
    """换数据源的全部理由就在这条：Word 那边只有 51%。"""
    response, _, _ = imports[year]

    filled = sum(1 for d in response.data.defects if d.source_defect_indicator_id)
    assert filled == len(response.data.defects)


@pytest.mark.parametrize("year", [year for _, year, _, _, _ in YEARS])
def test_component_name_is_never_blank(imports, year):
    """契约对它用的是 require_non_empty_string，空一个就整次导入报错。"""
    response, _, _ = imports[year]

    assert [d.candidate_id for d in response.data.defects
            if not (d.component_name or "").strip()] == []


@pytest.mark.parametrize("year", [year for _, year, _, _, _ in YEARS])
def test_photo_numbers_run_without_gaps_inside_each_section(imports, year):
    response, _, _ = imports[year]

    sections: dict[str, list[int]] = defaultdict(list)
    for photo in response.data.photos:
        match = re.fullmatch(r"(\d+\.\d+)-(\d+)", photo.photo_number)
        assert match, f"照片编号不合规范：{photo.photo_number}"
        sections[match.group(1)].append(int(match.group(2)))

    for section, numbers in sections.items():
        assert sorted(numbers) == list(range(1, len(numbers) + 1)), f"{section} 段有断号"


@pytest.mark.parametrize("year", [year for _, year, _, _, _ in YEARS])
def test_every_photo_has_a_caption(imports, year):
    response, _, _ = imports[year]

    assert [p.photo_number for p in response.data.photos
            if not p.extracted_file.original_caption.strip()] == []


def test_the_same_snapshot_produces_identical_output(tmp_path):
    """反复导入必须逐字节一致，否则"重新解析"会凭空制造差异。"""
    first = build(*YEARS[0][:3], tmp_path / "a")
    second = build(*YEARS[0][:3], tmp_path / "b")

    def stripped(response):
        payload = json.loads(response.data.model_dump_json())
        payload["contract"].pop("generated_at")
        return json.dumps(payload, ensure_ascii=False, sort_keys=True)

    assert stripped(first) == stripped(second)


def test_2024_reconciles_with_the_existing_word_import(imports):
    """来源库把范围行原样带过来，Word 那边已经拆开了；展开后总数必须相等。

    实测 2024：单构件 264 条 + 15 条范围行展开 97 个 = 361，与 Word 导入的 361 条一致。
    """
    response, _, _ = imports[2024]

    def width(number: str) -> int:
        if not number or "~" not in number:
            return 1
        left, right = number.split("~", 1)
        a = re.fullmatch(r"(\d+)-(\d+)(#.+)", left.strip())
        b = re.fullmatch(r"(\d+)-(\d+)(#.+)", right.strip())
        if not a or not b or a.group(3) != b.group(3) or a.group(1) != b.group(1):
            return 1
        return int(b.group(2)) - int(a.group(2)) + 1

    assert sum(width(d.component_number or "") for d in response.data.defects) == 361
