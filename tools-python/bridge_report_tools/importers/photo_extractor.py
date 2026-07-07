from __future__ import annotations

import posixpath
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

from bridge_report_tools.contracts.annual_inspection import DefectCandidate, ExtractedPhotoFile, PhotoCandidate, SourceRef, WarningItem
from bridge_report_tools.importers.docx_reader import DocxBlocks
from bridge_report_tools.importers.word_rules import PhotoCaption, WordRuleSet


DOCUMENT_RELATIONSHIP_PATH = "word/_rels/document.xml.rels"
DOCUMENT_XML_PATH = "word/document.xml"
IMAGE_RELATIONSHIP_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
RELATIONSHIP_NAMESPACE = "{http://schemas.openxmlformats.org/package/2006/relationships}"
RELATIONSHIP_EMBED_ATTRIBUTE = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}embed"
A_BLIP_TAG = "{http://schemas.openxmlformats.org/drawingml/2006/main}blip"
WORD_CELL_TAG = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}tc"
WORD_PARAGRAPH_TAG = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}p"
WORD_TABLE_TAG = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}tbl"
WORD_TEXT_TAG = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}t"


def find_photo_captions(document: DocxBlocks, rule_set: WordRuleSet) -> list[PhotoCaption]:
    captions: list[PhotoCaption] = []
    for text in document.paragraph_texts:
        caption = rule_set.parse_photo_caption(text)
        if caption is not None:
            captions.append(caption)
    return captions


def find_image_photo_captions(
    docx_path: Path,
    document: DocxBlocks,
    rule_set: WordRuleSet,
) -> list[PhotoCaption | None]:
    captions = find_captions_for_image_occurrences(docx_path, rule_set)
    if any(caption is not None for caption in captions):
        return captions
    return find_photo_captions(document, rule_set)


def find_captions_for_image_occurrences(docx_path: Path, rule_set: WordRuleSet) -> list[PhotoCaption | None]:
    with zipfile.ZipFile(docx_path) as archive:
        archive_names = set(archive.namelist())
        if DOCUMENT_XML_PATH not in archive_names:
            return []

        document_root = ET.fromstring(archive.read(DOCUMENT_XML_PATH))
        parents = {child: parent for parent in document_root.iter() for child in parent}
        captions: list[PhotoCaption | None] = []
        for blip in document_root.iter(A_BLIP_TAG):
            if not blip.attrib.get(RELATIONSHIP_EMBED_ATTRIBUTE):
                continue
            captions.append(caption_for_image_blip(blip, parents, rule_set))
        return captions


def caption_for_image_blip(
    blip: ET.Element,
    parents: dict[ET.Element, ET.Element],
    rule_set: WordRuleSet,
) -> PhotoCaption | None:
    cell = nearest_ancestor(blip, parents, WORD_CELL_TAG)
    if cell is not None and len(image_relationship_ids(cell)) == 1:
        caption = parse_caption_from_element(cell, rule_set)
        if caption is not None:
            return caption

    table = nearest_ancestor(blip, parents, WORD_TABLE_TAG)
    if table is not None and len(image_relationship_ids(table)) == 1:
        caption = parse_caption_from_element(table, rule_set)
        if caption is not None:
            return caption

    paragraph = nearest_ancestor(blip, parents, WORD_PARAGRAPH_TAG)
    if paragraph is None:
        return None
    return parse_caption_from_element(paragraph, rule_set)


def nearest_ancestor(
    element: ET.Element,
    parents: dict[ET.Element, ET.Element],
    tag: str,
) -> ET.Element | None:
    current = parents.get(element)
    while current is not None:
        if current.tag == tag:
            return current
        current = parents.get(current)
    return None


def image_relationship_ids(element: ET.Element) -> list[str]:
    return [
        relationship_id
        for blip in element.iter(A_BLIP_TAG)
        if (relationship_id := blip.attrib.get(RELATIONSHIP_EMBED_ATTRIBUTE))
    ]


def parse_caption_from_element(element: ET.Element, rule_set: WordRuleSet) -> PhotoCaption | None:
    text = element_text(element)
    if not text:
        return None
    return rule_set.parse_photo_caption(text)


def element_text(element: ET.Element) -> str:
    return " ".join("".join(text.text or "" for text in element.iter(WORD_TEXT_TAG)).split())


def media_members(docx_path: Path) -> list[str]:
    with zipfile.ZipFile(docx_path) as archive:
        archive_names = set(archive.namelist())
        if DOCUMENT_XML_PATH not in archive_names or DOCUMENT_RELATIONSHIP_PATH not in archive_names:
            return []

        relationships = document_image_relationships(archive)
        document_root = ET.fromstring(archive.read(DOCUMENT_XML_PATH))
        members: list[str] = []
        for element in document_root.iter():
            relationship_id = element.attrib.get(RELATIONSHIP_EMBED_ATTRIBUTE)
            if not relationship_id:
                continue
            member = relationships.get(relationship_id)
            if member and member in archive_names and member.startswith("word/media/") and not member.endswith("/"):
                members.append(member)
        return members


def document_image_relationships(archive: zipfile.ZipFile) -> dict[str, str]:
    root = ET.fromstring(archive.read(DOCUMENT_RELATIONSHIP_PATH))
    relationships: dict[str, str] = {}
    for relationship in root.findall(f"{RELATIONSHIP_NAMESPACE}Relationship"):
        if relationship.attrib.get("Type") != IMAGE_RELATIONSHIP_TYPE:
            continue
        if relationship.attrib.get("TargetMode") == "External":
            continue
        relationship_id = relationship.attrib.get("Id")
        target = relationship.attrib.get("Target")
        if not relationship_id or not target:
            continue
        member = posixpath.normpath(posixpath.join("word", target.lstrip("/")))
        relationships[relationship_id] = member
    return relationships


def extract_media(docx_path: Path, output_dir: Path) -> list[str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    written_files: list[str] = []
    members = media_members(docx_path)
    with zipfile.ZipFile(docx_path) as archive:
        for index, member in enumerate(members, start=1):
            extension = Path(member).suffix.lower() or ".bin"
            file_name = f"photo_{index:04d}{extension}"
            (output_dir / file_name).write_bytes(archive.read(member))
            written_files.append(file_name)
    return written_files


def defect_by_photo_number(defects: list[DefectCandidate]) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for defect in defects:
        for photo_number in defect.photo_numbers:
            if photo_number not in mapping:
                mapping[photo_number] = defect.candidate_id
    return mapping


def extract_and_match_photos(
    docx_path: Path,
    document: DocxBlocks,
    defects: list[DefectCandidate],
    output_dir: Path,
    rule_set: WordRuleSet,
) -> tuple[list[PhotoCandidate], list[str], list[WarningItem]]:
    temporary_files = extract_media(docx_path, output_dir)
    captions = find_image_photo_captions(docx_path, document, rule_set)
    defect_mapping = defect_by_photo_number(defects)
    photos: list[PhotoCandidate] = []

    for index, temporary_file in enumerate(temporary_files, start=1):
        caption: PhotoCaption | None = captions[index - 1] if index <= len(captions) else None
        if caption is None or not caption.is_defect_photo:
            continue

        caption_number = caption.number
        caption_text = caption.raw_text
        candidate_id = f"photo_{index:04d}"
        linked_defect_id = defect_mapping.get(caption_number)
        warnings: list[WarningItem] = []
        match_status = "高置信候选" if linked_defect_id else "未关联"
        confidence = 0.95 if linked_defect_id else 0.6
        if not linked_defect_id:
            warnings.append(
                WarningItem(
                    code="photo_not_referenced_by_defect",
                    message=f"Word 图片区存在照片编号 {caption_number}，但病害表未引用。",
                    severity="warning",
                    target_candidate_id=candidate_id,
                )
            )

        photos.append(
            PhotoCandidate(
                candidate_id=candidate_id,
                photo_number=caption_number,
                linked_defect_candidate_id=linked_defect_id,
                extracted_file=ExtractedPhotoFile(
                    temporary_file_name=temporary_file,
                    original_caption=caption_text,
                    archive_relative_path=None,
                ),
                match_status=match_status,
                source_ref=SourceRef(
                    chapter="第二章",
                    photo_area_caption=caption_text,
                ),
                confidence=confidence,
                review_status="待确认",
                warnings=warnings,
            )
        )

    matched_numbers = {photo.photo_number for photo in photos}
    for defect in defects:
        for photo_number in defect.photo_numbers:
            if photo_number not in matched_numbers:
                has_existing_warning = any(
                    warning.code == "photo_number_unmatched"
                    and warning.message == f"病害行引用照片编号 {photo_number}，但未在 Word 图片区找到对应图片。"
                    and warning.target_candidate_id == defect.candidate_id
                    for warning in defect.warnings
                )
                if not has_existing_warning:
                    defect.warnings.append(
                        WarningItem(
                            code="photo_number_unmatched",
                            message=f"病害行引用照片编号 {photo_number}，但未在 Word 图片区找到对应图片。",
                            severity="warning",
                            target_candidate_id=defect.candidate_id,
                        )
                    )

    return photos, temporary_files, []
