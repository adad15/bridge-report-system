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
