from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from bridge_report_tools import __version__
from bridge_report_tools.config import get_settings
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_importer import parse_word_import


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    host: str
    port: int


app = FastAPI(
    title="Bridge Report Python Tools",
    version=__version__,
)


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    settings = get_settings()
    return HealthResponse(
        status="ok",
        service="bridge-report-python-tools",
        version=__version__,
        host=settings.host,
        port=settings.port,
    )


@app.post("/imports/word/parse", response_model=WordImportResponse)
def parse_word(request: WordImportRequest) -> WordImportResponse:
    try:
        return parse_word_import(request)
    except WordImportError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": exc.code,
                "message": exc.message,
            },
        ) from exc
