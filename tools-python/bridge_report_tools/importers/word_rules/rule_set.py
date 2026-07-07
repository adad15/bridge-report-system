from __future__ import annotations

from dataclasses import dataclass

from bridge_report_tools.importers.word_errors import WordImportError


@dataclass(frozen=True)
class WordRuleSet:
    profile: str


def select_rule_set(rule_profile: str) -> WordRuleSet:
    if rule_profile == "辽宁国省干线":
        return WordRuleSet(profile="辽宁国省干线")
    raise WordImportError(
        code="unsupported_rule_profile",
        message=f"不支持的 Word 解析规则：{rule_profile}",
    )
