from __future__ import annotations

import re
import zipfile
from pathlib import Path

from bridge_report_tools.contracts.annual_inspection import DefectCandidate, ExtractedPhotoFile, PhotoCandidate, SourceRef, WarningItem
from bridge_report_tools.importers.docx_reader import DocxBlocks


CAPTION_PATTERN = re.compile(r"(?:照片|图)?\s*(?P<number>\d+(?:\.\d+)?-\d+)")


def find_photo_captions(document: DocxBlocks) -> list[tuple[str, str]]:
    captions: list[tuple[str, str]] = []
    for text in document.paragraph_texts:
        match = CAPTION_PATTERN.search(text)
        if match:
            captions.append((match.group("number"), text))
    return captions


def media_members(docx_path: Path) -> list[str]:
    with zipfile.ZipFile(docx_path) as archive:
        return sorted(
            name
            for name in archive.namelist()
            if name.startswith("word/media/") and not name.endswith("/")
        )


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
) -> tuple[list[PhotoCandidate], list[str], list[WarningItem]]:
    temporary_files = extract_media(docx_path, output_dir)
    captions = find_photo_captions(document)
    defect_mapping = defect_by_photo_number(defects)
    photos: list[PhotoCandidate] = []

    for index, temporary_file in enumerate(temporary_files, start=1):
        caption_number: str | None = None
        caption_text: str | None = None
        if index <= len(captions):
            caption_number, caption_text = captions[index - 1]
        else:
            caption_number = f"unmatched-{index:04d}"
            caption_text = None

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
                defect.warnings.append(
                    WarningItem(
                        code="photo_number_unmatched",
                        message=f"病害行引用照片编号 {photo_number}，但未在 Word 图片区找到对应图片。",
                        severity="warning",
                        target_candidate_id=defect.candidate_id,
                    )
                )

    return photos, temporary_files, []
