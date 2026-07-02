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
