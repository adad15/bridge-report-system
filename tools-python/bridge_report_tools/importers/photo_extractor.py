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


def find_photo_captions(document: DocxBlocks, rule_set: WordRuleSet) -> list[PhotoCaption]:
    captions: list[PhotoCaption] = []
    for text in document.paragraph_texts:
        caption = rule_set.parse_photo_caption(text)
        if caption is not None:
            captions.append(caption)
    return captions


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
    captions = find_photo_captions(document, rule_set)
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
