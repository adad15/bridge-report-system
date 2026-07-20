from __future__ import annotations

import re

from bridge_report_tools.importers.word_rules.rule_set import (
    DefectTableRule,
    WordRuleSet,
)


DISEASE_PHOTO_PATTERN = re.compile(r"(?:^|照片)\s*(?P<number>2\.(?:1|2|3)-\d+|2\.(?:1|2|3)\d+)")
ANY_CAPTION_PATTERN = re.compile(r"(?:照片|图)\s*(?P<number>\d{1,2}(?:\.\d+)?-\d+)")


def build_liaoning_trunk_rule_set() -> WordRuleSet:
    return WordRuleSet(
        profile="辽宁国省干线",
        defect_table_rules=(
            DefectTableRule(
                table_no="表2.1-1",
                title_keywords=("表2.1-1", "上部结构", "病害检查表"),
                structure_part="上部结构",
            ),
            DefectTableRule(
                table_no="表2.2-1",
                title_keywords=("表2.2-1", "下部结构", "病害检查表"),
                structure_part="下部结构",
            ),
            DefectTableRule(
                table_no="表2.3-1",
                title_keywords=("表2.3-1", "桥面系", "病害检查表"),
                structure_part="桥面系",
            ),
        ),
        disease_photo_pattern=DISEASE_PHOTO_PATTERN,
        any_caption_pattern=ANY_CAPTION_PATTERN,
    )
