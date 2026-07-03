# Bridge Report System

Local web system for bridge inspection report archiving, defect review, historical comparison, and formal Word report generation.

## First Module Scope

This skeleton proves the local service shape:

- C++ Drogon main service on `127.0.0.1:18080`
- Python FastAPI tool service on `127.0.0.1:18081`
- React/Vite frontend on `127.0.0.1:5173`
- PostgreSQL reserved as the future fact database
- `archive/` reserved as the local binary file archive

## Development Order

Read these documents first:

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
- `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
- `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`

## Module 02 Database Check

Module 02 creates the PostgreSQL core schema and archive metadata foundation.

Set `BRIDGE_REPORT_DATABASE_URL` if your local database differs from the default:

```powershell
$env:BRIDGE_REPORT_DATABASE_URL = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
```

If `psql` is not on `PATH`, point `PSQL_EXE` to the local PostgreSQL client:

```powershell
$env:PSQL_EXE = "D:\PostgreSQL\18\bin\psql.exe"
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
```

The check applies `database/migrations/002_core_schema_and_archive.sql` and runs the rollback-only smoke test in `database/tests/002_core_schema_smoke.sql`.
