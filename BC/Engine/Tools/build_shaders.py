"""
Compiles every <root>/Public .slang file in a temporary folder,
and replaces <runtime-dir>/Shaders with that folder on success.
"""

import glob
import shutil
import sys
import tempfile
from pathlib import Path

import slangpy as sp


class ShaderBuildError(Exception):
    pass


_SLANG_RUNTIME_FILES = ("slang.dll", "slang-compiler.dll", "slang-glslang.dll", "slang-glsl-module.dll")


def copy_sources(roots: list[Path], directory: Path) -> dict[Path, Path]:
    """
    Copies every Public/ .slang file of roots into directory, at its path under Public/.
    @param roots shader roots; 
    @return source file of each copy
    """
    sources: dict[Path, Path] = {}
    for root in roots:
        public = root / "Public"
        for filename in glob.iglob(str(public / "**" / "*.slang"), recursive=True):
            copy = directory / Path(filename).relative_to(public)
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(filename, copy)
            sources[copy] = Path(filename)
    return sources

def compile_all(sources: dict[Path, Path], directory: Path) -> None:
    options = sp.SlangCompilerOptions()
    options.include_paths = [
        directory,
        *sorted(p for p in directory.rglob("*") if p.is_dir()),
    ]
    options.matrix_layout = sp.SlangMatrixLayout.column_major

 
    device = sp.Device(type=sp.DeviceType.vulkan, compiler_options=options,
                   enable_hot_reload=False, enable_debug_layers=True)
    

    failed = []
    for copy, original in sources.items():
        try:
            #Handle only module creating, linking is skipped
            module = device.load_module(str(copy))
        except (sp.SlangCompileError, RuntimeError) as error:
            print(f"{original}:\n{error}", file=sys.stderr)
            failed.append(str(original))
    if failed:
        raise ShaderBuildError("Slang rejected:\n  " + "\n  ".join(failed))
        

def copy_slang_runtime(slangc: Path, runtime_dir: Path) -> None:
    for name in _SLANG_RUNTIME_FILES:
        shutil.copy2(slangc.parent / name, runtime_dir / name)
    for module_dir in slangc.parent.glob("slang-standard-module-*"):
        shutil.copytree(module_dir, runtime_dir / module_dir.name, dirs_exist_ok=True)


def buildShaders(roots: list[Path], runtimeDir: Path, slangc: Path) -> bool:
    """
    Compiles every Public/ .slang file of roots, then replaces runtimeDir/Shaders with them and copies the Slang runtime.
    @param roots shader roots; a later root's file replaces an earlier root's of the same path
    @param runtimeDir folder of the executable
    @param slangc slangc executable of the Slang runtime to copy
    @return False when a shader failed to compile; the errors are printed to stderr and runtimeDir is not changed
    """
    #make roots absolute paths
    roots = [Path(root).resolve() for root in roots]
    runtimeDir = Path(runtimeDir)

    try:
        #copy shaders to the temporary folder, compile them
        with tempfile.TemporaryDirectory() as directory:
            sources = copy_sources(roots, Path(directory))
            compile_all(sources, Path(directory))
            #replace the runtime folder with the shaders if compilation succeeded
            shaders = runtimeDir / "Shaders"
            shutil.rmtree(shaders, ignore_errors=True)
            shutil.copytree(directory, shaders)
        #copy slang runtime so the game can compile shaders at runtime
        copy_slang_runtime(Path(slangc), runtimeDir)
    except ShaderBuildError as error:
        #shader compilation failed
        print(error, file=sys.stderr)
        return False
    return True
