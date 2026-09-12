"""ReportContext 在 Python 端的契约校验（设计 §9.2、§23.3）。

契约在 C++、Python 和 TypeScript 里各校一遍。这一层不是"照抄字段"：C++ 侧承诺过的
不变量在这里重验，跨语言边界上两边都以为对方保证了的事，才是最容易一起漏掉的。
"""

from __future__ import annotations

import pytest
from pydantic import ValidationError

from bridge_report_tools.reports.report_context import ReportContext, ReportPhoto

from tests.reports.context_fixtures import context, defect_row, part, photo


def test_photo_numbers_must_match_between_table_and_captions() -> None:
    """病害表的照片编号列与图题必须是同一批号码（设计 §11.8）。"""
    with pytest.raises(ValidationError) as raised:
        context(
            parts=[
                part(
                    "SUPERSTRUCTURE",
                    "上部结构",
                    rows=[defect_row(1, photo_numbers=["照片2.1-1"])],
                    photos=[photo("照片2.1-9", "photos/a.png")],
                )
            ]
        )

    assert "照片编号不一致" in str(raised.value)


def test_photo_order_must_match_too() -> None:
    """号码集合对得上、顺序对不上同样不行：照片是按病害表行序排的。"""
    with pytest.raises(ValidationError):
        context(
            parts=[
                part(
                    "SUPERSTRUCTURE",
                    "上部结构",
                    rows=[defect_row(1, photo_numbers=["照片2.1-1", "照片2.1-2"])],
                    photos=[
                        photo("照片2.1-2", "photos/a.png"),
                        photo("照片2.1-1", "photos/a.png"),
                    ],
                )
            ]
        )


def test_caption_must_be_built_from_the_report_number() -> None:
    """图题「{图号}␠␠{标题}」，两个空格（设计 §11.7）。"""
    with pytest.raises(ValidationError):
        ReportPhoto(
            photo_id="p",
            archived_file_id="f",
            storage_relative_path="photos/a.png",
            report_number="照片2.1-1",
            title="横向裂缝",
            caption="照片2.1-1 横向裂缝",  # 只有一个空格
        )


def test_caption_without_title_is_just_the_number() -> None:
    parsed = ReportPhoto(
        photo_id="p",
        archived_file_id="f",
        storage_relative_path="photos/a.png",
        report_number="照片2.1-1",
        title=None,
        caption="照片2.1-1",
    )

    assert parsed.caption == "照片2.1-1"


def test_unknown_field_is_rejected() -> None:
    """C++ 加了字段而这边没跟上时要立刻炸，不能静默丢掉。"""
    with pytest.raises(ValidationError):
        context(unexpected_field="x")


def test_number_format_lookup_uses_the_template_config() -> None:
    parsed = context()

    assert parsed.number_format("DEFECT_PHOTOS:SUPERSTRUCTURE") == "照片2.1-{n}"
    assert parsed.number_format("DEFECT_PHOTOS:WHOLE_BRIDGE") is None


def test_part_lookup_returns_none_for_a_part_without_defects() -> None:
    """C++ 只把有病害的部位放进 parts，缺了不是错，是"这个部位没病害"。"""
    parsed = context(
        parts=[part("SUPERSTRUCTURE", "上部结构", rows=[defect_row(1)], photos=[])]
    )

    assert parsed.part("SUPERSTRUCTURE") is not None
    assert parsed.part("DECK") is None


def test_context_round_trips_the_cpp_payload_shape() -> None:
    """字段名与 C++ 的 ReportContext::to_json() 一致，序列化回去形状不变。"""
    parsed = context(
        parts=[
            part(
                "SUPERSTRUCTURE",
                "上部结构",
                rows=[defect_row(1, photo_numbers=["照片2.1-1"])],
                photos=[photo("照片2.1-1", "photos/a.png")],
            )
        ]
    )

    payload = parsed.model_dump()
    assert ReportContext(**payload) == parsed
