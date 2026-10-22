#!/usr/bin/env python3
"""Generate immutable, versioned sandbox regression scripts; no kernel mutation."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import sys

TEMPLATE = Path(__file__).resolve().parent / "templates/sandbox_regression.py"


def generate(output, seed=None):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    if seed is None:
        seed = secrets.randbits(63)
    if not 0 <= seed < 2**63:
        raise ValueError("Seed deve estar entre 0 e 2^63-1")
    template = TEMPLATE.read_text(encoding="utf-8")
    if template.count("REVISION = None") != 1:
        raise ValueError("Template inválido")
    numbers = [int(m[1]) for p in output.iterdir()
               if (m := re.fullmatch(r"sandbox_regression_v(\d{4,})\.py", p.name))]
    number = max(numbers, default=0) + 1
    revision = {"schema": 1, "revision": number, "seed": seed,
                "templateSha256": hashlib.sha256(template.encode()).hexdigest(),
                "purpose": "isolation-and-consistency-regression",
                "kernelModified": False, "undetectabilityClaimed": False}
    source = template.replace("REVISION = None", "REVISION = " + repr(revision))
    path = output / f"sandbox_regression_v{number:04d}.py"
    # Exclusive creation prevents overwriting another revision, including concurrent generators.
    with path.open("x", encoding="utf-8", newline="\n") as f:
        f.write(source)
    return {**revision, "script": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest="action", required=True)
    g = commands.add_parser("generate")
    g.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "sandbox_revisions")
    g.add_argument("--seed", type=int)
    args = p.parse_args()
    print(json.dumps(generate(args.output, args.seed), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("Erro:", e, file=sys.stderr)
        sys.exit(1)
