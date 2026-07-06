from __future__ import annotations

from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError


def parse_word_import(request: WordImportRequest) -> WordImportResponse:
    raise WordImportError(
        code="word_importer_not_implemented",
        message="Word importer orchestration has not been implemented yet.",
    )
