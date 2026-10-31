"""
Generates <root>/Public/Generated from each shader root's Shared/ .ixx files,
then compiles every root's Public/ into <runtime-dir>/Shaders.
"""

import argparse
import sys
from pathlib import Path
from ShaderTypesGenerator.generator2 import generateShaders
import build_shaders


def UpdateShaders(Roots:list[Path], RuntimeDir:Path, Slangc:Path) -> bool:
    """
    @param Roots shader roots;
    @param RuntimeDir folder of the executable
    @param Slangc slangc executable of the Slang runtime to copy
    @return False when generation or compilation failed
    """
    for InputDir in Roots:
        GeneratedDir = InputDir/"Public"/"Generated"
        print(f"Updating shaders to {GeneratedDir}")
        print("Clearing output directory...")
        #clear output directory
        if GeneratedDir.exists():
            for file in GeneratedDir.rglob("*"):
                if file.is_file():
                    file.unlink()
        print("Generating shaders...")
        #generate shaders
        generationResult:bool = generateShaders(InputDir/"Shared", GeneratedDir)
        if not generationResult:
            print("Shader generation failed")
            return False
        print("Shader generation succeeded")
    #compile shaders
    print("Compiling shaders and copying slang runtime...")
    build_result:bool = build_shaders.buildShaders(Roots, RuntimeDir, Slangc)
    if not build_result:
        print("Shader compilation failed")
        return False
    print("Shader compilation succeeded")
    print("Shader update completed successfully")
    return True


if __name__ == "__main__":
    #Arguments passed from cmake
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--root", type=Path, action="append", required=True, help="shader root, repeatable")
    arguments.add_argument("--runtime-dir", type=Path, required=True, help="folder of the executable")
    arguments.add_argument("--slangc", type=Path, required=True, help="slangc executable of the Slang runtime to copy")
    args = arguments.parse_args()
    roots = [root.resolve() for root in args.root]
    sys.exit(0 if UpdateShaders(roots, args.runtime_dir, args.slangc) else 1)