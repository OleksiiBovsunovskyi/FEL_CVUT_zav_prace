"""Runs the built Avalonia AppHost against the built native app.dll."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
from typing import Sequence

from build import (
    BuildConfig,
    CLANG64_BIN,
    REPO_ROOT,
    apphost_exe_path,
    resolve_build_dir,
)


@dataclass(frozen=True)
class RunOptions:
    config: BuildConfig
    build_dir: Path


def parse_args(argv: Sequence[str]) -> RunOptions:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Run the built Avalonia AppHost."
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Run Release AppHost against cmake-build-release/bin.",
    )
    parser.add_argument(
        "--build-dir",
        type=str,
        default=None,
        help="CMake build directory relative to the repo root.",
    )

    ns: argparse.Namespace = parser.parse_args(argv)
    config: BuildConfig = "Release" if ns.release else "Debug"
    return RunOptions(
        config=config,
        build_dir=resolve_build_dir(config, ns.build_dir),
    )


def env_with_clang64_path() -> dict[str, str]:
    env: dict[str, str] = os.environ.copy()
    clang64_str: str = str(CLANG64_BIN)
    current_path: str = env.get("PATH", "")
    path_entries: list[str] = current_path.split(os.pathsep) if current_path else []
    if not any(entry.lower() == clang64_str.lower() for entry in path_entries):
        env["PATH"] = f"{clang64_str}{os.pathsep}{current_path}" if current_path else clang64_str
    return env


def main(argv: Sequence[str]) -> int:
    options: RunOptions = parse_args(argv)
    exe_path: Path = apphost_exe_path(options.config)
    data_root: Path = options.build_dir / "bin"
    native_dll: Path = data_root / "app.dll"

    if not exe_path.is_file():
        print(f"AppHost executable not found: {exe_path} (run build.py first)", file=sys.stderr)
        return 1
    if not native_dll.is_file():
        print(f"Native DLL not found: {native_dll} (run build.py first)", file=sys.stderr)
        return 1

    result: subprocess.CompletedProcess[bytes] = subprocess.run(
        [str(exe_path), str(data_root)],
        cwd=REPO_ROOT,
        env=env_with_clang64_path(),
        check=False,
    )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
