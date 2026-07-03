from __future__ import annotations

import json
from pathlib import Path

from .annual_inspection import BridgeAnnualInspectionData


SCHEMA_URI = "https://json-schema.org/draft/2020-12/schema"
DEFAULT_SCHEMA_PATH = (
    Path(__file__).resolve().parents[3]
    / "contracts"
    / "bridge_annual_inspection_data.schema.json"
)


def export_bridge_annual_inspection_schema(target_path: str | Path | None = None) -> Path:
    output_path = Path(target_path) if target_path is not None else DEFAULT_SCHEMA_PATH
    schema = BridgeAnnualInspectionData.model_json_schema()
    schema["$schema"] = SCHEMA_URI

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(schema, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return output_path


def main() -> None:
    export_bridge_annual_inspection_schema()


if __name__ == "__main__":
    main()
