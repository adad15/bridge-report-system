"""上传模板的包级安全检查（设计 §23.1）。

这一层必须跑在任何解压之前：它只读 zip 目录项（infolist）就能判定路径穿越、异常压缩比
和宏，因此一个恶意包在被 python-docx 打开之前就会被拦下。校验通过后才允许下一步读内容。
"""

from __future__ import annotations

import posixpath
import re
import zipfile
from collections import Counter
from urllib.parse import unquote, urlsplit
from xml.etree.ElementTree import Element, TreeBuilder
from xml.parsers import expat
from dataclasses import dataclass
from pathlib import Path

from bridge_report_tools.reports.errors import ReportTemplateError
from bridge_report_tools.reports.issues import TemplateIssue, error, warning


CONTENT_TYPES_ENTRY = "[Content_Types].xml"
DOCUMENT_ENTRY = "word/document.xml"

MACRO_ENTRY_PATTERN = re.compile(r"^word/vbaProject\.bin$|^word/vbaData\.xml$", re.IGNORECASE)
MACRO_CONTENT_TYPE = "macroenabled"
EMBEDDINGS_PREFIX = "word/embeddings/"

RELATIONSHIP_NS = "http://schemas.openxmlformats.org/package/2006/relationships"
CONTENT_TYPES_NS = "http://schemas.openxmlformats.org/package/2006/content-types"
OFFICE_RELATIONSHIP_BASES = (
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/",
    "http://purl.oclc.org/ooxml/officeDocument/relationships/",
)
HYPERLINK_RELATIONSHIPS = {base + "hyperlink" for base in OFFICE_RELATIONSHIP_BASES}
REJECTED_RELATIONSHIPS = {
    base + name for base in OFFICE_RELATIONSHIP_BASES
    for name in ("attachedTemplate", "frame", "subDocument")
}


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


def _read_xml(archive: zipfile.ZipFile, entry: str, limit: int) -> Element:
    with archive.open(entry) as stream:
        content = stream.read(limit + 1)
    if len(content) > limit:
        raise ValueError("XML exceeds the entry limit")
    builder = TreeBuilder()
    parser = expat.ParserCreate(namespace_separator="}")
    depth = 0
    count = 0

    def tag(name: str) -> str:
        return "{" + name if "}" in name else name

    def start(name: str, attributes: dict[str, str]) -> None:
        nonlocal depth, count
        depth += 1
        count += 1
        if depth > 64 or count > 50000:
            raise ValueError("XML structure exceeds limits")
        builder.start(tag(name), {tag(k): v for k, v in attributes.items()})

    def end(name: str) -> None:
        nonlocal depth
        builder.end(tag(name))
        depth -= 1

    def reject(*_args):
        raise ValueError("DTD and entities are forbidden")

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = builder.data
    parser.StartDoctypeDeclHandler = reject
    parser.EntityDeclHandler = reject
    parser.ExternalEntityRefHandler = reject
    parser.Parse(content, True)
    return builder.close()


def _inspect_relationships(archive: zipfile.ZipFile, entry: str, limit: int) -> list[TemplateIssue]:
    issues: list[TemplateIssue] = []
    try:
        root = _read_xml(archive, entry, limit)
        if root.tag != f"{{{RELATIONSHIP_NS}}}Relationships":
            raise ValueError("Invalid relationships namespace/root")
        ids: set[str] = set()
        for relationship in root:
            if relationship.tag != f"{{{RELATIONSHIP_NS}}}Relationship" or len(relationship):
                raise ValueError("Invalid relationship element")
            attributes = relationship.attrib
            if set(attributes) - {"Id", "Type", "Target", "TargetMode"}:
                raise ValueError("Invalid relationship attributes")
            rid, kind, target = (attributes.get(k, "") for k in ("Id", "Type", "Target"))
            mode = attributes.get("TargetMode", "Internal")
            if not rid or rid in ids or not kind or not target or mode not in {"Internal", "External"}:
                raise ValueError("Invalid or duplicate relationship")
            if not urlsplit(kind).scheme:
                raise ValueError("Relationship type must be an absolute URI")
            ids.add(rid)
            if kind in REJECTED_RELATIONSHIPS:
                issues.append(error("template_relationship_rejected", "模板包含附加模板或外部文档依赖。", entry))
            elif mode == "External":
                if kind in HYPERLINK_RELATIONSHIPS:
                    issues.append(warning("template_external_hyperlink", "模板包含外部超链接，请确认是有意为之。", entry))
                else:
                    issues.append(error("template_external_relationship", "模板含不允许的外部文件关系。", entry))
            else:
                decoded = unquote(target).replace("\\\\", "/").replace("\\", "/")
                base = posixpath.dirname(posixpath.dirname(entry)) if "/_rels/" in entry else ""
                resolved = posixpath.normpath(posixpath.join(base, decoded.split("#", 1)[0]))
                if urlsplit(decoded).scheme or decoded.startswith(("/", "\\")) or resolved == ".." or resolved.startswith("../"):
                    issues.append(error("template_relationship_rejected", "包内关系目标越过文档边界。", entry))
    except (KeyError, OSError, RuntimeError, zipfile.BadZipFile, expat.ExpatError, ValueError):
        issues.append(error("template_rels_unreadable", f"关系文件不合法或不安全：{entry}", entry))
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
        canonical_names = Counter(posixpath.normpath(info.filename.replace("\\", "/")).casefold() for info in entries)
        if any(count > 1 for count in canonical_names.values()):
            issues.append(error("template_duplicate_entry", "模板包含重复或冲突的 ZIP 条目。"))

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

        # Do not read payloads after directory-level rejection.
        if not any(issue.severity == "error" for issue in issues):
            try:
                root = _read_xml(archive, CONTENT_TYPES_ENTRY, limits.max_entry_uncompressed_bytes)
                if root.tag != f"{{{CONTENT_TYPES_NS}}}Types":
                    raise ValueError("Invalid content types root")
                for item in root:
                    if item.tag not in {f"{{{CONTENT_TYPES_NS}}}Default", f"{{{CONTENT_TYPES_NS}}}Override"}:
                        raise ValueError("Invalid content type entry")
                    content_type = item.get("ContentType", "")
                    if not content_type:
                        raise ValueError("Missing content type")
                    if MACRO_CONTENT_TYPE in content_type.lower():
                        issues.append(error("template_macro_rejected", "模板声明为启用宏的文档类型。", CONTENT_TYPES_ENTRY))
            except (KeyError, OSError, RuntimeError, zipfile.BadZipFile, expat.ExpatError, ValueError):
                issues.append(error("template_content_types_invalid", "文档类型 XML 不合法或不安全。", CONTENT_TYPES_ENTRY))
            if not any(issue.severity == "error" for issue in issues):
                for info in entries:
                    if info.filename.lower().endswith(".rels"):
                        issues.extend(_inspect_relationships(archive, info.filename, limits.max_entry_uncompressed_bytes))

    return PackageInspection(
        issues=issues,
        entry_count=len(entries),
        total_uncompressed_bytes=total_uncompressed,
    )
