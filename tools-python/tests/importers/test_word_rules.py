from __future__ import annotations

import pytest

from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules import select_rule_set


def test_select_rule_set_returns_liaoning_trunk_rules() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.profile == "辽宁国省干线"


def test_select_rule_set_rejects_unknown_profile() -> None:
    with pytest.raises(WordImportError) as exc_info:
        select_rule_set("吉林国省干线")

    assert exc_info.value.code == "unsupported_rule_profile"
    assert exc_info.value.message == "不支持的 Word 解析规则：吉林国省干线"


def test_liaoning_trunk_matches_defect_table_titles() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    upper = rule_set.match_defect_table_title("表2.1-1  上部结构病害检查表")
    lower = rule_set.match_defect_table_title("表2.2-1 下部结构病害检查表")
    deck = rule_set.match_defect_table_title("表2.3-1  桥面系病害检查表")

    assert upper is not None
    assert upper.structure_part == "上部结构"
    assert lower is not None
    assert lower.structure_part == "下部结构"
    assert deck is not None
    assert deck.structure_part == "桥面系"


def test_liaoning_trunk_rejects_old_generic_defect_title() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.match_defect_table_title("上部结构病害检查表") is None


def test_liaoning_trunk_matches_rating_table_titles() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    weight = rule_set.match_rating_table_title("表4.1-1桥梁部件权重计算表")
    overall = rule_set.match_rating_table_title("表4.1-2  总体技术状况评定表")

    assert weight is not None
    assert weight.table_kind == "weight"
    assert overall is not None
    assert overall.table_kind == "overall"


def test_liaoning_trunk_does_not_use_appendix_rating_title() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.match_rating_table_title("附录1 桥梁技术状况评定表") is None


def test_liaoning_trunk_classifies_disease_photo_captions() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    caption = rule_set.parse_photo_caption("照片2.1-1 主梁梁底裂缝")

    assert caption is not None
    assert caption.number == "2.1-1"
    assert caption.is_defect_photo is True


def test_liaoning_trunk_classifies_overview_photo_as_non_disease() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    caption = rule_set.parse_photo_caption("照片1-1 辽小线绕阳河二号桥桥面正面照")

    assert caption is not None
    assert caption.number == "1-1"
    assert caption.is_defect_photo is False
