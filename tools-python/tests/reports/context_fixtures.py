"""构造 ReportContext 的测试固件。

字段与形状照 C++ 侧 `ReportContext::to_json()` 来：这些用例同时是 Python 端对那份
JSON 的契约断言，字段名写错就该在这里炸，而不是等到真跑一次生成。
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

from bridge_report_tools.reports.report_context import ReportContext


NUMBER_FORMATS = {
    "DEFECT_TABLES:SUPERSTRUCTURE": "表2.1-{n}",
    "DEFECT_TABLES:SUBSTRUCTURE": "表2.2-{n}",
    "DEFECT_TABLES:DECK": "表2.3-{n}",
    "DEFECT_PHOTOS:SUPERSTRUCTURE": "照片2.1-{n}",
    "DEFECT_PHOTOS:SUBSTRUCTURE": "照片2.2-{n}",
    "DEFECT_PHOTOS:DECK": "照片2.3-{n}",
    # 4.1.1 与 4.1.2 共用一条序列：格式串相同即共号（表4.1-1、表4.1-2）。
    "COMPONENT_WEIGHTS": "表4.1-{n}",
    "ASSESSMENT_RESULT": "表4.1-{n}",
    "ASSESSMENT_APPENDIX": "附表1-{n}",
}

SCALARS = {
    "report_no": "Q2026-1",
    "bridge_name": "百股大桥",
    "route_code": "S320",
    "route_name": "大养线",
    "administrative_region": "太和区",
    "inspection_date": "2026-05-18",
    "inspection_year": "2026",
    "project_name": "定检A4标段",
    "inspection_org": "某某院",
    "report_date": "2026-09-07",
    "overall_grade": "2类",
    "comparison_year": "2025",
}

NO_COMPARISON = {
    "current_source_defect_count": 0,
    "previous_source_defect_count": 0,
    "delta": 0,
    "has_previous": False,
}

#: 没有正式评定的上下文。第 4、5 章和附表1 在这种上下文上必须报错而不是出空表。
NO_ASSESSMENT = {"has_formal_run": False}


def assessment(**overrides) -> dict:
    """一份典型的正式评定结果：三个结构、四个部件类别、两条主要扣分病害。"""
    payload = {
        "has_formal_run": True,
        "overall_score": 86.4321,
        "overall_grade": "2类",
        "parts": [
            {"part_code": "SUPERSTRUCTURE", "part_label": "上部结构",
             "score": 85.5, "grade": "2类", "weight": 0.4},
            {"part_code": "SUBSTRUCTURE", "part_label": "下部结构",
             "score": 90.0, "grade": "1类", "weight": 0.4},
            {"part_code": "DECK", "part_label": "桥面系",
             "score": 78.25, "grade": "3类", "weight": 0.2},
        ],
        "categories": [
            {"part_code": "SUPERSTRUCTURE", "part_label": "上部结构",
             "category_id": "SB01", "category_name": "上部承重构件",
             "component_count": 12, "score": 85.5, "grade": "2类", "weight": 0.7,
             # 分档合计必须等于构件数量——上下文模型会验这一条。
             "score_bands": [{"score": 65.0, "component_count": 2},
                             {"score": 75.0, "component_count": 4},
                             {"score": 100.0, "component_count": 6}]},
            {"part_code": "SUPERSTRUCTURE", "part_label": "上部结构",
             "category_id": "SB02", "category_name": "上部一般构件",
             "component_count": 4, "score": 92.0, "grade": "1类", "weight": 0.3,
             "score_bands": [{"score": 75.0, "component_count": 1},
                             {"score": 100.0, "component_count": 3}]},
            {"part_code": "SUBSTRUCTURE", "part_label": "下部结构",
             "category_id": "XB01", "category_name": "桥墩",
             "component_count": 3, "score": 90.0, "grade": "1类", "weight": 1.0,
             "score_bands": [{"score": 100.0, "component_count": 3}]},
            {"part_code": "DECK", "part_label": "桥面系",
             "category_id": "QM01", "category_name": "桥面铺装",
             "component_count": 2, "score": 78.25, "grade": "3类", "weight": 1.0,
             "score_bands": [{"score": 61.74, "component_count": 1},
                             {"score": 100.0, "component_count": 1}]},
        ],
        # 表4.1-1：含一行"无此构件"，权重被摊给同部位其余部件。
        "component_weights": [
            {"part_code": "SUPERSTRUCTURE", "part_label": "上部结构", "order": 1,
             "category_id": "SB01", "category_name": "上部承重构件",
             "configured_weight": 0.7, "effective_weight": 0.7,
             "component_count": 12, "present": True},
            {"part_code": "SUPERSTRUCTURE", "part_label": "上部结构", "order": 2,
             "category_id": "SB02", "category_name": "上部一般构件",
             "configured_weight": 0.3, "effective_weight": 0.3,
             "component_count": 4, "present": True},
            {"part_code": "SUBSTRUCTURE", "part_label": "下部结构", "order": 3,
             "category_id": "XB01", "category_name": "桥墩",
             "configured_weight": 0.98, "effective_weight": 1.0,
             "component_count": 3, "present": True},
            {"part_code": "SUBSTRUCTURE", "part_label": "下部结构", "order": 4,
             "category_id": "XB09", "category_name": "调治构造物",
             "configured_weight": 0.02, "effective_weight": None,
             "component_count": None, "present": False},
            {"part_code": "DECK", "part_label": "桥面系", "order": 5,
             "category_id": "QM01", "category_name": "桥面铺装",
             "configured_weight": 1.0, "effective_weight": 1.0,
             "component_count": 2, "present": True},
        ],
        "triggered_controls": [],
        "top_deductions": [
            {"part_code": "DECK", "component_number": "1#跨桥面铺装",
             "defect_type": "坑槽", "deduction": 12.0},
            {"part_code": "SUPERSTRUCTURE", "component_number": "1-1#板",
             "defect_type": "裂缝", "deduction": 5.0},
        ],
    }
    payload.update(overrides)
    return payload


def png(path: Path, width: int, height: int) -> Path:
    """写一张纯色 PNG。手拼是为了不给测试引入 Pillow 依赖。"""

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    raw = b"".join(b"\x00" + b"\x80\x80\x80" * width for _ in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )
    return path


def photo(number: str, path: str, title: str | None = "横向裂缝") -> dict:
    return {
        "photo_id": f"photo-{number}",
        "archived_file_id": f"file-{number}",
        "storage_relative_path": path,
        "report_number": number,
        "title": title,
        "caption": f"{number}  {title}" if title else number,
        "source_photo_number": None,
    }


def defect_row(
    row_number: int,
    *,
    component_number: str = "1-1#板",
    photo_numbers: list[str] | None = None,
    part_name: str = "上部承重构件",
    deduction: float | None = 5.0,
) -> dict:
    return {
        "row_number": row_number,
        "observation_id": f"obs-{row_number}",
        "part_name": part_name,
        "component_number": component_number,
        "defect_location": "跨中",
        "defect_type": "裂缝",
        "description": "横向裂缝 1 条，长 0.8m，宽 0.15mm",
        "scale": "2",
        "deduction": deduction,
        "component_score": 85.0,
        "photo_numbers": photo_numbers or [],
    }


def part(
    part_code: str,
    part_label: str,
    *,
    rows: list[dict],
    photos: list[dict],
    comparison: dict | None = None,
) -> dict:
    return {
        "part_code": part_code,
        "part_label": part_label,
        "defect_rows": rows,
        "photos": photos,
        "comparison": comparison or dict(NO_COMPARISON),
    }


def comparison(current: int, previous: int) -> dict:
    return {
        "current_source_defect_count": current,
        "previous_source_defect_count": previous,
        "delta": current - previous,
        "has_previous": True,
    }


def context(**overrides) -> ReportContext:
    payload = {
        "inspection_year_id": "year-1",
        "template_id": "template-1",
        "template_code": "PERIODIC_V1",
        "template_config": {
            "table_number_formats": dict(NUMBER_FORMATS),
            "required_personnel_roles": ["approver", "reviewer"],
        },
        "scalars": dict(SCALARS),
        "parts": [],
        "personnel": [],
        "equipment": [],
        "assessment": assessment(),
        "bridge_profile": {
            "business_code": "L0123",
            "station_mark": "K12+345",
            "bridge_type": "钢筋混凝土简支板桥",
            "bridge_scale": "中桥",
            "span_combination": "5×13",
            "bridge_length_m": 68.5,
            "bridge_width_m": 12.0,
            "built_year": 1998,
            "maintenance_org": "太和公路段",
        },
        "overall_comparison": dict(NO_COMPARISON),
    }
    payload.update(overrides)
    return ReportContext(**payload)
