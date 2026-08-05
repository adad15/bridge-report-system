"""把来源库的照片组装成契约里的照片候选。

来源库用外键把照片直接绑在病害上，归属是确定的，不需要像 Word 那样按编号去猜。
缺的是两样：照片编号和题注——来源库两者都没有（`noteContent` 与 `oriFileName` 全空），
只能按规则生成。
"""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any

from bridge_report_tools.importers.source_db.reader import (
    ComponentNode, SourcePhoto, load_photo_content)

#: 部位层级码 → 报告章节号。做成参数而不是写死：章节结构因桥而异，写死会在遇到
#: 没有桥面系、或章节从别的号起排的桥时静默出错。
DEFAULT_SECTION_MAP = {"001": "2.1", "002": "2.2", "003": "2.3"}

EXTENSIONS = {
    "image/jpeg": ".jpg",
    "image/png": ".png",
    "image/gif": ".gif",
    "image/bmp": ".bmp",
    "image/webp": ".webp",
    "image/tiff": ".tiff",
}


def photo_caption(component_number: str, defect_type: str, description: str) -> str:
    """题注 = 构件编号 + 病害类型；类型为空时退回用描述。

    题注允许重复——报告里靠前面的照片编号区分（现有报告中「两侧护栏破损露筋」连续
    出现三次，编号分别是 2.3-63/64/65），所以不加序号后缀。
    """
    tail = (defect_type or "").strip() or (description or "").strip()
    return f"{component_number} {tail}".strip()


def _warning(code: str, message: str) -> dict[str, str]:
    return {"code": code, "message": message, "severity": "warning"}


def build_photo_candidates(
    db: sqlite3.Connection,
    photos: list[SourcePhoto],
    defects: list[dict[str, Any]],
    links: dict[str, Any],
    tree: list[ComponentNode],
    output_dir: str | Path,
    section_map: dict[str, str] | None = None,
) -> tuple[list[dict[str, Any]], list[str]]:
    """产出照片候选，并把照片本体写进临时目录。

    同时就地给对应病害补上 `photo_references`，让照片与病害的关联在契约层面与
    Word 那条路完全一致，后端不需要为来源库单开一套。
    """
    sections = dict(DEFAULT_SECTION_MAP if section_map is None else section_map)
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=True)
    by_id = {node.id: node for node in tree}
    defects_by_id = {d["candidate_id"]: d for d in defects}

    candidates: list[dict[str, Any]] = []
    written: list[str] = []
    counters: dict[str, int] = {}
    for position, photo in enumerate(photos, start=1):
        link = links.get(photo.defect_id)
        defect = defects_by_id.get(link.candidate_id) if link else None
        if defect is None:
            # 照片指向的病害不在本次导入里，跳过——宁可少一张图，也不产生悬空引用。
            continue
        component = by_id.get(link.tree_id)
        level_code = component.level_code if component else ""
        section = sections.get(level_code[:3], "")
        counters[section] = counters.get(section, 0) + 1
        number = f"{section}-{counters[section]}" if section else str(counters[section])

        content_type, payload = load_photo_content(db, photo.id)
        suffix = EXTENSIONS.get(content_type, ".jpg")
        file_name = f"source_photo_{position:04d}{suffix}"
        (directory / file_name).write_bytes(payload)
        written.append(file_name)

        component_number = component.name if component else photo.member_number
        warnings = []
        if "~" in component_number:
            warnings.append(_warning(
                "source_photo_on_range_component",
                f"照片挂在范围构件 {component_number} 上，拆分后只能落到第一个构件，请确认。"))

        candidate_id = f"source_photo_{position:04d}"
        candidates.append({
            "candidate_id": candidate_id,
            "photo_number": number,
            "linked_defect_candidate_id": defect["candidate_id"],
            "extracted_file": {
                "temporary_file_name": file_name,
                "original_caption": photo_caption(
                    component_number, defect.get("defect_type", ""), defect.get("defect_description", "")),
                "archive_relative_path": None,
            },
            # 归属来自外键而不是编号推断，所以匹配状态直接是已确认；是否入库仍由人校对。
            "match_status": "已确认",
            "source_ref": {"source_type": "word"},
            "confidence": 1.0,
            "review_status": "待确认",
            "warnings": warnings,
        })
        defect["photo_references"].append({
            "photo_number": number,
            "resolution": "matched",
            "photo_candidate_id": candidate_id,
            "resolved_defect_candidate_id": defect["candidate_id"],
            "review_note": None,
        })
    return candidates, written
