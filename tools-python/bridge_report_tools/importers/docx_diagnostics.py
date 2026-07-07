from __future__ import annotations

import zipfile
from pathlib import Path


TEXT_XML_MEMBERS = (
    "word/document.xml",
    "word/footnotes.xml",
    "word/endnotes.xml",
)


def find_text_locations(path: Path, needles: list[str]) -> dict[str, list[str]]:
    locations: dict[str, list[str]] = {needle: [] for needle in needles}
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for member in TEXT_XML_MEMBERS:
            if member not in names:
                continue
            xml_text = archive.read(member).decode("utf-8", errors="ignore")
            for needle in needles:
                if needle in xml_text:
                    locations[needle].append(member)
    return locations
