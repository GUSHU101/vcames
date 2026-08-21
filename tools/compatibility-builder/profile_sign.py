#!/usr/bin/env python3
"""Canonicalize and sign public Profile metadata with an offline Ed25519 key."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def canonical_bytes(path: Path) -> bytes:
    value = json.loads(path.read_text(encoding="utf-8"))
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def run_openssl(arguments: list[str]) -> None:
    result = subprocess.run(["openssl", *arguments], check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())


def main() -> int:
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="command", required=True)
    canonicalize = subcommands.add_parser("canonicalize")
    canonicalize.add_argument("input", type=Path)
    canonicalize.add_argument("output", type=Path)
    sign = subcommands.add_parser("sign")
    sign.add_argument("input", type=Path)
    sign.add_argument("private_key", type=Path)
    sign.add_argument("signature", type=Path)
    verify = subcommands.add_parser("verify")
    verify.add_argument("input", type=Path)
    verify.add_argument("public_key", type=Path)
    verify.add_argument("signature", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "canonicalize":
            args.output.write_bytes(canonical_bytes(args.input))
        elif args.command == "sign":
            if args.input.read_bytes() != canonical_bytes(args.input):
                raise RuntimeError("input must be canonicalized before signing")
            run_openssl(["pkeyutl", "-sign", "-rawin", "-inkey", str(args.private_key),
                         "-in", str(args.input), "-out", str(args.signature)])
        else:
            run_openssl(["pkeyutl", "-verify", "-pubin", "-rawin",
                         "-inkey", str(args.public_key), "-in", str(args.input),
                         "-sigfile", str(args.signature)])
    except (OSError, json.JSONDecodeError, RuntimeError) as exc:
        print(f"Profile signing operation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
