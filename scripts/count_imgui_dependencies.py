#!/usr/bin/env python3
"""Ravenの脱ImGui移行で残存依存を計測する読み取り専用ツール。"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

EXTENSIONS = {".h", ".hpp", ".cpp", ".c", ".cc", ".vcxproj", ".filters", ".props"}
PATTERN = re.compile(r"ImGui|imgui")
CALL_PATTERN = re.compile(r"\bImGui::[A-Za-z_]\w*\s*\(")


def collect(root: Path) -> dict:
    targets = [root / "Root" / "Root" / "Raven", root / "Root" / "Root" / "main.cpp",
               root / "Root" / "Root" / "Root.vcxproj",
               root / "Root" / "Root" / "Root.vcxproj.filters"]
    records = []
    for target in targets:
        if target.is_dir():
            files = sorted(p for p in target.rglob("*") if p.is_file() and p.suffix in EXTENSIONS)
        elif target.is_file():
            files = [target]
        else:
            continue
        for path in files:
            # vendorと生成物を除き、Raven製品コードとproject設定だけを数えます。
            if any(part in {"vendor", "x64", "Debug", "Release"} for part in path.parts):
                continue
            content = path.read_text(encoding="utf-8-sig", errors="replace")
            matches = PATTERN.findall(content)
            if matches:
                records.append({
                    "path": path.relative_to(root).as_posix(),
                    "references": len(matches),
                    "api_calls": len(CALL_PATTERN.findall(content)),
                })
    return {
        "files_with_references": len(records),
        "references": sum(record["references"] for record in records),
        "api_calls": sum(record["api_calls"] for record in records),
        "files": records,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="RavenのImGui依存をJSONで集計")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, help="計測結果を書き出すJSONファイル")
    args = parser.parse_args()
    root = args.root.resolve()
    if not (root / "Root" / "Root" / "Raven").is_dir():
        parser.error("Raven repository rootを--rootで指定してください")
    result = collect(root)
    report = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
