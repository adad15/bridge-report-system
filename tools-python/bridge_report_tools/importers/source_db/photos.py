"""把来源库的照片组装成契约里的照片候选。

来源库用外键（`images.ForeignKey` → `outerCheckData.id`）把照片直接绑在病害上，
归属是确定的，不需要像 Word 那样按编号去猜。

因此这条路**不产生照片编号**。早先为了填满契约的必填字段，这里按结构部位造过
"2.1-1"这样的号，但来源库的 `photoNum` 列 887 张全空——那个号不是来源事实，是我们
凭空编的，而且按病害 UUID 顺序发放，与构件台账顺序完全无关。它既不能当来源证据，
也不该展示给用户，更不该进报告：报告里的图号在生成时按模板结构重排。

题注仍然要生成：来源库的 `noteContent` 与 `oriFileName` 同样全空，而报告的图题需要
一句说明，只能按构件编号和病害类型拼。
"""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any

from bridge_report_tools.importers.source_db.reader import (
    ComponentNode, SourcePhoto, load_photo_content)

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

    题注允许重复——报告里靠生成时排的图号区分（现有报告中「两侧护栏破损露筋」连续
    出现三次），所以不加序号后缀。
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
) -> tuple[list[dict[str, Any]], list[str]]:
    """产出照片候选，并把照片本体写进临时目录。

    同时就地给对应病害补上 `photo_references`。引用一律是 `matched` 且带
    `photo_candidate_id`，配对不依赖编号——契约允许这条路省略 `photo_number`。
    """
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=True)
    by_id = {node.id: node for node in tree}
    defects_by_id = {d["candidate_id"]: d for d in defects}

    candidates: list[dict[str, Any]] = []
    written: list[str] = []
    for position, photo in enumerate(photos, start=1):
        link = links.get(photo.defect_id)
        defect = defects_by_id.get(link.candidate_id) if link else None
        if defect is None:
            # 照片指向的病害不在本次导入里，跳过——宁可少一张图，也不产生悬空引用。
            continue
        component = by_id.get(link.tree_id)

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
            "linked_defect_candidate_id": defect["candidate_id"],
            "extracted_file": {
                "temporary_file_name": file_name,
                "original_caption": photo_caption(
                    component_number, defect.get("defect_type", ""), defect.get("defect_description", "")),
                "archive_relative_path": None,
            },
            "source_ref": {"source_type": "word"},
            "confidence": 1.0,
            "warnings": warnings,
        })
        defect["photo_references"].append({
            "resolution": "matched",
            "photo_candidate_id": candidate_id,
            "resolved_defect_candidate_id": defect["candidate_id"],
            "review_note": None,
        })
    return candidates, written
