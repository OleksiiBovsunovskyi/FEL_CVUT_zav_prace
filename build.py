"""Builds the native app DLL and Avalonia AppHost."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
from typing import Literal, Sequence

type BuildConfig = Literal["Debug", "Release"]
type TargetName = str

REPO_ROOT: Path = Path(__file__).resolve().parent
BUILD_CMD: Path = REPO_ROOT / "build.cmd"
APPHOST_PROJECT: Path = REPO_ROOT / "BC" / "Avalonia" / "AppHost" / "AppHost.csproj"
APPHOST_TFM: str = "net10.0"
CLANG64_BIN: Path = Path(r"C:\msys64\clang64\bin")
DEFAULT_DEBUG_BUILD_DIR: str = "cmake-build-mingw"
DEFAULT_RELEASE_BUILD_DIR: str = "cmake-build-release"
DEFAULT_NATIVE_TARGET: TargetName = "app"


@dataclass(frozen=True)
class BuildOptions:
    config: BuildConfig
    build_dir: Path
    native_target: TargetName
    build_native: bool
    build_avalonia: bool


def resolve_build_dir(config: BuildConfig, override: str | None) -> Path:
    default_dir_name: str = (
        DEFAULT_RELEASE_BUILD_DIR if config == "Release" else DEFAULT_DEBUG_BUILD_DIR
    )
    build_dir_name: str = override if override is not None else default_dir_name
    build_dir: Path = Path(build_dir_name)
    if not build_dir.is_absolute():
        build_dir = REPO_ROOT / build_dir
    return build_dir


def apphost_exe_path(config: BuildConfig) -> Path:
    return APPHOST_PROJECT.parent / "bin" / config / APPHOST_TFM / "AppHost.exe"


def parse_args(argv: Sequence[str]) -> BuildOptions:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Build native app.dll and Avalonia AppHost."
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Build Release configuration into cmake-build-release.",
    )
    parser.add_argument(
        "--build-dir",
        type=str,
        default=None,
        help="CMake build directory relative to the repo root.",
    )
    parser.add_argument(
        "--target",
        type=str,
        default=DEFAULT_NATIVE_TARGET,
        help=f"CMake target passed to build.cmd (default: {DEFAULT_NATIVE_TARGET}).",
    )
    parser.add_argument(
        "--no-native",
        action="store_true",
        help="Skip the CMake native build step.",
    )
    parser.add_argument(
        "--no-avalonia",
        action="store_true",
        help="Skip the dotnet build step.",
    )

    ns: argparse.Namespace = parser.parse_args(argv)
    config: BuildConfig = "Release" if ns.release else "Debug"

    return BuildOptions(
        config=config,
        build_dir=resolve_build_dir(config, ns.build_dir),
        native_target=str(ns.target),
        build_native=not bool(ns.no_native),
        build_avalonia=not bool(ns.no_avalonia),
    )


def run_step(
    label: str,
    command: Sequence[str],
    env: dict[str, str] | None = None,
) -> None:
    print(f"==> {label}", flush=True)
    result: subprocess.CompletedProcess[bytes] = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=env,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def build_native(build_dir: Path, target: TargetName) -> None:
    try:
        build_dir_arg: str = str(build_dir.relative_to(REPO_ROOT))
    except ValueError:
        build_dir_arg = str(build_dir)
    run_step(
        f"Native CMake build ({build_dir_arg}, target={target})",
        ["cmd", "/c", str(BUILD_CMD), build_dir_arg, target],
    )


def build_avalonia(config: BuildConfig) -> None:
    env: dict[str, str] = os.environ.copy()
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    run_step(
        f"Avalonia build ({config})",
        ["dotnet", "build", str(APPHOST_PROJECT), "-c", config],
        env=env,
    )


def main(argv: Sequence[str]) -> int:
    options: BuildOptions = parse_args(argv)
    if options.build_native:
        build_native(options.build_dir, options.native_target)
    if options.build_avalonia:
        build_avalonia(options.config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
