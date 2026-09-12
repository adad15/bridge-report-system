"""上传模板的包级安全检查（设计 §23.1）。

这一层必须跑在任何解压之前：它只读 zip 目录项（infolist）就能判定路径穿越、异常压缩比
和宏，因此一个恶意包在被 python-docx 打开之前就会被拦下。校验通过后才允许下一步读内容。
"""

from __future__ import annotations

import posixpath
import re
import zipfile
from dataclasses import dataclass
from pathlib import Path

from bridge_report_tools.reports.errors import ReportTemplateError
from bridge_report_tools.reports.issues import TemplateIssue, error, warning


CONTENT_TYPES_ENTRY = "[Content_Types].xml"
DOCUMENT_ENTRY = "word/document.xml"

MACRO_ENTRY_PATTERN = re.compile(r"^word/vbaProject\.bin$|^word/vbaData\.xml$", re.IGNORECASE)
MACRO_CONTENT_TYPE = "macroenabled"
EMBEDDINGS_PREFIX = "word/embeddings/"

EXTERNAL_TARGET_MODE = re.compile(r'TargetMode\s*=\s*"External"', re.IGNORECASE)
RELATIONSHIP_PATTERN = re.compile(r"<Relationship\b[^>]*>", re.IGNORECASE)
RELATIONSHIP_TYPE_PATTERN = re.compile(r'Type\s*=\s*"([^"]*)"', re.IGNORECASE)
RELATIONSHIP_TARGET_PATTERN = re.compile(r'Target\s*=\s*"([^"]*)"', re.IGNORECASE)

#: 外部关系里唯一放行的类型。外部图片、OLE 对象、附加模板都会让模板在生成时去读
#: 服务器之外的文件，必须拒绝。
HYPERLINK_RELATIONSHIP = "hyperlink"
#: 即使指向包内也拒绝：附加模板会改变文档的样式来源，使生成结果不可复现。
ALWAYS_REJECTED_RELATIONSHIPS = frozenset({"attachedTemplate", "frame", "subDocument"})


@dataclass(frozen=True)
class PackageLimits:
    max_archive_bytes: int = 20 * 1024 * 1024
    max_total_uncompressed_bytes: int = 100 * 1024 * 1024
    max_entry_uncompressed_bytes: int = 20 * 1024 * 1024
    max_entry_count: int = 2000
    #: 单条目压缩比上限。XML 本身压缩率就高，所以只有体积也超过下面的门槛才判定异常。
    max_compression_ratio: int = 500
    compression_ratio_floor_bytes: int = 1024 * 1024


DEFAULT_LIMITS = PackageLimits()


@dataclass(frozen=True)
class PackageInspection:
    issues: list[TemplateIssue]
    entry_count: int
    total_uncompressed_bytes: int

    @property
    def is_safe(self) -> bool:
        return not any(issue.severity == "error" for issue in self.issues)


def _is_unsafe_entry_name(name: str) -> bool:
    if name.startswith("/") or name.startswith("\\"):
        return True
    if re.match(r"^[A-Za-z]:", name):
        return True
    normalized = posixpath.normpath(name.replace("\\", "/"))
    return normalized.startswith("../") or normalized == ".."


def _inspect_relationships(archive: zipfile.ZipFile, entry: str) -> list[TemplateIssue]:
    issues: list[TemplateIssue] = []
    try:
        content = archive.read(entry).decode("utf-8", errors="replace")
    except (KeyError, zipfile.BadZipFile) as exc:
        return [error("template_rels_unreadable", f"关系文件无法读取：{entry}（{exc}）", entry)]

    for raw in RELATIONSHIP_PATTERN.findall(content):
        type_match = RELATIONSHIP_TYPE_PATTERN.search(raw)
        relationship_type = type_match.group(1).rsplit("/", 1)[-1] if type_match else ""
        target_match = RELATIONSHIP_TARGET_PATTERN.search(raw)
        target = target_match.group(1) if target_match else ""

        if relationship_type in ALWAYS_REJECTED_RELATIONSHIPS:
            issues.append(
                error(
                    "template_relationship_rejected",
                    f"模板包含被禁止的关系类型 {relationship_type}（目标 {target}）；"
                    "它会让生成结果依赖模板之外的文档。",
                    entry,
                )
            )
            continue

        if not EXTERNAL_TARGET_MODE.search(raw):
            continue
        if relationship_type == HYPERLINK_RELATIONSHIP:
            issues.append(
                warning(
                    "template_external_hyperlink",
                    f"模板含指向外部的超链接：{target}。生成结果会保留该链接，请确认是有意为之。",
                    entry,
                )
            )
            continue
        issues.append(
            error(
                "template_external_relationship",
                f"模板含外部文件关系 {relationship_type}（目标 {target}）；"
                "生成时不允许读取模板包之外的文件。",
                entry,
            )
        )
    return issues


def inspect_package(path: Path, limits: PackageLimits = DEFAULT_LIMITS) -> PackageInspection:
    """只读 zip 目录和关系文件，判定这个包能不能安全地进入下一步解析。"""
    if not path.is_file():
        raise ReportTemplateError("template_file_missing", f"模板文件不存在：{path}")

    archive_bytes = path.stat().st_size
    if archive_bytes > limits.max_archive_bytes:
        raise ReportTemplateError(
            "template_too_large",
            f"模板文件 {archive_bytes} 字节，超过上限 {limits.max_archive_bytes} 字节。",
        )

    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as exc:
        raise ReportTemplateError(
            "template_not_docx", f"模板不是标准 .docx（无法作为 zip 打开）：{exc}"
        ) from exc

    issues: list[TemplateIssue] = []
    total_uncompressed = 0
    with archive:
        entries = archive.infolist()
        names = {info.filename for info in entries}

        if CONTENT_TYPES_ENTRY not in names or DOCUMENT_ENTRY not in names:
            raise ReportTemplateError(
                "template_not_docx",
                f"模板缺少 {CONTENT_TYPES_ENTRY} 或 {DOCUMENT_ENTRY}，不是 Word 文档包。",
            )

        if len(entries) > limits.max_entry_count:
            issues.append(
                error(
                    "template_entry_count_exceeded",
                    f"模板包含 {len(entries)} 个条目，超过上限 {limits.max_entry_count}。",
                )
            )

        for info in entries:
            name = info.filename
            if _is_unsafe_entry_name(name):
                issues.append(
                    error("template_entry_path_unsafe", f"模板条目路径不安全：{name}", name)
                )
                continue

            total_uncompressed += info.file_size
            if info.file_size > limits.max_entry_uncompressed_bytes:
                issues.append(
                    error(
                        "template_entry_too_large",
                        f"条目解压后 {info.file_size} 字节，"
                        f"超过单条目上限 {limits.max_entry_uncompressed_bytes} 字节。",
                        name,
                    )
                )
            if (
                info.file_size > limits.compression_ratio_floor_bytes
                and info.compress_size > 0
                and info.file_size / info.compress_size > limits.max_compression_ratio
            ):
                issues.append(
                    error(
                        "template_compression_ratio_suspicious",
                        f"条目压缩比 {info.file_size // info.compress_size}:1 异常，"
                        "拒绝解压。",
                        name,
                    )
                )

            if MACRO_ENTRY_PATTERN.match(name):
                issues.append(
                    error("template_macro_rejected", f"模板包含宏工程：{name}", name)
                )
            if name.lower().startswith(EMBEDDINGS_PREFIX):
                issues.append(
                    error("template_embedded_object_rejected", f"模板包含嵌入对象：{name}", name)
                )

        if total_uncompressed > limits.max_total_uncompressed_bytes:
            issues.append(
                error(
                    "template_uncompressed_size_exceeded",
                    f"模板解压后共 {total_uncompressed} 字节，"
                    f"超过上限 {limits.max_total_uncompressed_bytes} 字节。",
                )
            )

        content_types = archive.read(CONTENT_TYPES_ENTRY).decode("utf-8", errors="replace")
        if MACRO_CONTENT_TYPE in content_types.lower():
            issues.append(
                error(
                    "template_macro_rejected",
                    "模板声明为启用宏的文档类型（.docm）；只接受标准 .docx。",
                    CONTENT_TYPES_ENTRY,
                )
            )

        for info in entries:
            if info.filename.lower().endswith(".rels"):
                issues.extend(_inspect_relationships(archive, info.filename))

    return PackageInspection(
        issues=issues,
        entry_count=len(entries),
        total_uncompressed_bytes=total_uncompressed,
    )
