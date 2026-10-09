"""Check a draft/session/result record without claiming physical qualification."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[1]


def reject_constant(value: str) -> None:
    raise ValueError(f"Non-finite JSON constant is not permitted: {value}")


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)


def validate(record: dict[str, Any]) -> None:
    def finite(value: Any) -> None:
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("Non-finite calibration value")
        if isinstance(value, dict):
            for item in value.values():
                finite(item)
        elif isinstance(value, list):
            for item in value:
                finite(item)

    finite(record)
    schemas = {
        "pitrac-camera-distortion-session-v1": "session.schema.json",
        "pitrac-camera-distortion-result-v1": "result.schema.json",
    }
    if record.get("schema") not in schemas:
        raise ValueError("Unknown calibration record schema")
    schema = load_json(ROOT / "schemas" / schemas[record["schema"]])
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(record)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record", type=Path)
    args = parser.parse_args()
    record = load_json(args.record)
    validate(record)
    print(f"Record structure valid: {args.record}. This does not verify the physical setup.")


if __name__ == "__main__":
    main()
