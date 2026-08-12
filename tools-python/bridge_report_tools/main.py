from pathlib import Path

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict

from bridge_report_tools import __version__
from bridge_report_tools.config import get_settings
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.source_db.context import (
    SourceImportRequest,
    parse_source_import,
)
from bridge_report_tools.importers.source_db.reader import (
    SourceDatabaseError,
    load_task_summaries,
    open_source_db,
)
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


@app.post("/imports/source/parse", response_model=WordImportResponse)
def parse_source(request: SourceImportRequest) -> WordImportResponse:
    """从来源软件的本机离线库导入。

    响应形状与 /imports/word/parse 完全相同——契约是两条路的解耦层，后端只认它。
    """
    try:
        return parse_source_import(request)
    except SourceDatabaseError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": exc.code,
                "message": exc.message,
            },
        ) from exc


class SourceTasksRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    source_db_path: Path


class SourceTaskRow(BaseModel):
    task_id: str
    name: str
    check_date: str | None
    defect_count: int
    photo_count: int


class SourceTasksResponse(BaseModel):
    tasks: list[SourceTaskRow]


@app.post("/imports/source/tasks", response_model=SourceTasksResponse)
def list_source_tasks(request: SourceTasksRequest) -> SourceTasksResponse:
    """列出离线库里有哪些检测任务。

    taskId 是厂商库里的 UUID，用户不可能知道；导入界面得先拿这张表让人选。
    """
    try:
        with open_source_db(request.source_db_path) as db:
            summaries = load_task_summaries(db)
    except SourceDatabaseError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
    return SourceTasksResponse(tasks=[
        SourceTaskRow(
            task_id=summary.id,
            name=summary.name,
            check_date=summary.check_date,
            defect_count=summary.defect_count,
            photo_count=summary.photo_count,
        )
        for summary in summaries
    ])
