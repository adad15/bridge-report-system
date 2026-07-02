$ErrorActionPreference = "Stop"

Set-Location tools-python

uv sync --extra dev

uv run uvicorn bridge_report_tools.main:app --host 127.0.0.1 --port 18081
