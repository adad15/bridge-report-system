from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Literal

from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules.common import contains_all


COMPACT_DISEASE_PHOTO_NUMBER_PATTERN = re.compile(r"^2\.(?P<section>[123])(?P<serial>\d+)$")


@dataclass(frozen=True)
class DefectTableRule:
    table_no: str
    title_keywords: tuple[str, ...]
    structure_part: Literal["上部结构", "下部结构", "桥面系"]


@dataclass(frozen=True)
class RatingTableRule:
    table_no: str
    title_keywords: tuple[str, ...]
    table_kind: Literal["weight", "overall"]


@dataclass(frozen=True)
class PhotoCaption:
    number: str
    raw_text: str
    is_defect_photo: bool


@dataclass(frozen=True)
class WordRuleSet:
    profile: str
    defect_table_rules: tuple[DefectTableRule, ...]
    rating_table_rules: tuple[RatingTableRule, ...]
    disease_photo_pattern: re.Pattern[str]
    any_caption_pattern: re.Pattern[str]

    def match_defect_table_title(self, title: str | None) -> DefectTableRule | None:
        for rule in self.defect_table_rules:
            if contains_all(title, rule.title_keywords):
                return rule
        return None

    def match_rating_table_title(self, title: str | None) -> RatingTableRule | None:
        for rule in self.rating_table_rules:
            if contains_all(title, rule.title_keywords):
                return rule
        return None

    def parse_photo_caption(self, text: str) -> PhotoCaption | None:
        disease_match = self.disease_photo_pattern.search(text)
        if disease_match is not None:
            return PhotoCaption(
                number=normalize_photo_number(disease_match.group("number")),
                raw_text=text,
                is_defect_photo=True,
            )
        caption_match = self.any_caption_pattern.search(text)
        if caption_match is None:
            return None
        return PhotoCaption(
            number=normalize_photo_number(caption_match.group("number")),
            raw_text=text,
            is_defect_photo=False,
        )


def normalize_photo_number(number: str) -> str:
    compact_match = COMPACT_DISEASE_PHOTO_NUMBER_PATTERN.match(number)
    if compact_match is None:
        return number
    return f"2.{compact_match.group('section')}-{compact_match.group('serial')}"


def select_rule_set(rule_profile: str) -> WordRuleSet:
    if rule_profile == "辽宁国省干线":
        from bridge_report_tools.importers.word_rules.liaoning_trunk import (
            build_liaoning_trunk_rule_set,
        )

        return build_liaoning_trunk_rule_set()
    raise WordImportError(
        code="unsupported_rule_profile",
        message=f"不支持的 Word 解析规则：{rule_profile}",
    )
