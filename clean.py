"""Deletes build outputs and baked assets, so the next build.py run rebuilds and rebakes everything."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import sys
from typing import Sequence

from build import (
    APPHOST_PROJECT,
    BuildConfig,
    REPO_ROOT,
    build_native,
    resolve_build_dir,
    run_step,
)

# Baked .pmma directories, relative to the build directory: bake output, and its copy next to app.dll.
BAKED_ASSET_DIRS: tuple[Path, ...] = (Path("BC") / "App" / "Assets", Path("bin") / "Assets")
CMAKE_CACHE_NAME: str = "CMakeCache.txt"


@dataclass(frozen=True)
class CleanOptions:
    config: BuildConfig
    build_dir: Path
    full: bool
    clean_avalonia: bool


def parse_args(argv: Sequence[str]) -> CleanOptions:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Delete native build outputs, baked assets and Avalonia AppHost outputs."
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Clean the Release configuration in cmake-build-release.",
    )
    parser.add_argument(
        "--build-dir",
        type=str,
        default=None,
        help="CMake build directory relative to the repo root.",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Delete the whole build directory, including the CMake cache and fetched dependencies.",
    )
    parser.add_argument(
        "--no-avalonia",
        action="store_true",
        help="Skip the dotnet clean step.",
    )

    ns: argparse.Namespace = parser.parse_args(argv)
    config: BuildConfig = "Release" if ns.release else "Debug"

    return CleanOptions(
        config=config,
        build_dir=resolve_build_dir(config, ns.build_dir),
        full=bool(ns.full),
        clean_avalonia=not bool(ns.no_avalonia),
    )


def remove_directory(directory: Path) -> None:
    if not directory.exists():
        return
    print(f"==> Deleting {directory}", flush=True)
    shutil.rmtree(directory)


def delete_build_dir(build_dir: Path) -> None:
    if build_dir == REPO_ROOT or REPO_ROOT not in build_dir.parents:
        raise SystemExit(f"Refusing to delete {build_dir}: not a directory inside {REPO_ROOT}")
    remove_directory(build_dir)


def clean_native(build_dir: Path) -> None:
    """
    Runs the CMake clean target, deletes the baked assets and marks the CMake cache as changed.
    Bake rules are chosen when CMake configures; a newer CMake cache makes the next build reconfigure.
    """
    cache: Path = build_dir / CMAKE_CACHE_NAME
    if not cache.exists():
        print(f"==> No CMake cache in {build_dir}, nothing to clean", flush=True)
        return
    build_native(build_dir, "clean")
    for relative in BAKED_ASSET_DIRS:
        remove_directory(build_dir / relative)
    cache.touch()


def clean_avalonia(config: BuildConfig) -> None:
    env: dict[str, str] = os.environ.copy()
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    run_step(
        f"Avalonia clean ({config})",
        ["dotnet", "clean", str(APPHOST_PROJECT), "-c", config],
        env=env,
    )


def main(argv: Sequence[str]) -> int:
    options: CleanOptions = parse_args(argv)
    if options.full:
        delete_build_dir(options.build_dir)
    else:
        clean_native(options.build_dir)
    if options.clean_avalonia:
        clean_avalonia(options.config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
