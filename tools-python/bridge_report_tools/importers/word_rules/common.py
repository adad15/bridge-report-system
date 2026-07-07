from __future__ import annotations

import re


def normalize_rule_text(text: str | None) -> str:
    if text is None:
        return ""
    return re.sub(r"\s+", "", text.replace("　", " ")).strip()


def contains_all(text: str | None, keywords: tuple[str, ...]) -> bool:
    normalized = normalize_rule_text(text)
    return all(normalize_rule_text(keyword) in normalized for keyword in keywords)
