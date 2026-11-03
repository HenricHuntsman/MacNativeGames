#!/usr/bin/env python3
"""Game-free regression for the Linux totk_xlate --serve line protocol.

This deliberately supplies invalid requests: it proves service recovery, NOT
shader translation, SPIR-V correctness, or Tears of the Kingdom gameplay.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_shader_service_protocol.py <totk_xlate-binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        print(f"missing translator binary: {binary}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="dragonhub-shader-protocol-") as folder:
        temp = Path(folder)
        out = temp / "output"
        def invalid_job(key):
            return json.dumps({
                "job": {"key": key, "stage": "cs", "code": str(temp / (key + ".missing"))},
                "out": str(out),
            })
        requests = [
            "{not valid JSON",
            invalid_job("missing_one"),
            json.dumps({"out": str(out)}),  # missing job
            invalid_job("missing_two"),
        ]
        try:
            result = subprocess.run(
                [str(binary), "--serve"],
                input="\n".join(requests) + "\n",
                text=True,
                capture_output=True,
                timeout=30,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL: shader service did not complete: {exc}", file=sys.stderr)
            return 1

        replies = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        expected_prefixes = [
            "fail request ",
            "fail missing_one ",
            "fail request ",
            "fail missing_two ",
        ]
        problems = []
        if result.returncode != 0:
            problems.append(f"unexpected process exit: {result.returncode}")
        if len(replies) != len(expected_prefixes):
            problems.append(f"expected {len(expected_prefixes)} reply lines; got {len(replies)}")
        for idx, prefix in enumerate(expected_prefixes):
            if idx < len(replies) and not replies[idx].startswith(prefix):
                problems.append(f"reply {idx + 1} expected prefix {prefix!r}, got {replies[idx]!r}")
        if out.exists() and any(out.rglob("*.spv")):
            problems.append("failed synthetic jobs unexpectedly emitted SPIR-V")
        if problems:
            for issue in problems:
                print("FAIL:", issue, file=sys.stderr)
            print("stdout:", result.stdout, file=sys.stderr)
            print("stderr:", result.stderr, file=sys.stderr)
            return 1

        print("PASS: malformed JSON and missing-job payloads return 'fail request'")
        print("PASS: missing synthetic shader files return 'fail <key>'")
        print("PASS: service continues after each failure, replies 4/4 and exits 0 at EOF")
        print("LIMIT: does not prove a Maxwell shader was translated into SPIR-V")
        return 0


if __name__ == "__main__":
    sys.exit(main())
