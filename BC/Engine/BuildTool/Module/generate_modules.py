"""
Writes the CMake for every .module.toml under a directory.
Usage: generate_modules.py <root> <list variable> [<output .cmake>]
Without an output path the CMake is printed.

The target also gets every .ixx (interface) and .cpp (implementation unit)
under the descriptor's directory, minus subdirectories holding their own .module.toml.
Every value is copied into the CMake verbatim, so ${VAR} and $ENV{VAR} expand at configure time.
"""
import sys
import tomllib
from pathlib import Path
from typing import Any, Literal, NamedTuple, NoReturn, Required, TypedDict, cast

#what files define each module
DESCRIPTOR_SUFFIX: str = ".module.toml"

# Absolute path with forward slashes, as written into the CMake.
CMakePath = str


class Descriptor(TypedDict, total=False):
    """
    Contents of .module.toml; 
    """
    name: Required[str]
    depends: list[str]
    private_depends: list[str]
    object: bool
    whole_archive: bool
    extra_module_interfaces: list[str]
    extra_translation_units: list[str]
    alias: str
    definitions: list[str]
    private_definitions: list[str]
    include_dirs: list[str]
    private_include_dirs: list[str]
    options: list[str]
    private_options: list[str]

#.module.toml file and its parsed descriptor
class DescriptorFile(NamedTuple):
    path: Path
    descriptor: Descriptor

#sources under each module
class ModuleSources(NamedTuple):
    interfaces: list[CMakePath]
    implementations: list[CMakePath]


TargetPropertyKey = Literal[
    "definitions", "private_definitions",
    "include_dirs", "private_include_dirs",
    "options", "private_options",
]

# Descriptor key -> (CMake command, visibility).
TARGET_PROPERTIES: dict[TargetPropertyKey, tuple[str, str]] = {
    "definitions": ("target_compile_definitions", "PUBLIC"),
    "private_definitions": ("target_compile_definitions", "PRIVATE"),
    "include_dirs": ("target_include_directories", "PUBLIC"),
    "private_include_dirs": ("target_include_directories", "PRIVATE"),
    "options": ("target_compile_options", "PUBLIC"),
    "private_options": ("target_compile_options", "PRIVATE"),
}
KEYS: frozenset[str] = frozenset(Descriptor.__annotations__)


def fail(message: str) -> NoReturn:
    sys.exit(f"generate_modules.py: {message}")


def call(command: str, *arguments: str | list[str]) -> str:
    """
    One CMake command.
    A str argument is written as is, a list argument as one quoted value per line.
    Empty arguments are dropped.
    """
    text = command + "("
    after_list = False
    for argument in arguments:
        if not argument:
            continue
        if isinstance(argument, list):
            text += "".join(f'\n        "{value}"' for value in argument)
            after_list = True
        else:
            text += ("\n        " if after_list else "" if text.endswith("(") else " ") + argument
            after_list = False
    return text + ")"

#parse .module.toml into a Descriptor
def load_descriptor(path: Path) -> Descriptor:
    with path.open("rb") as file:
        contents: dict[str, Any] = tomllib.load(file)
    unknown = contents.keys() - KEYS
    if unknown:
        fail(f"{path}: unknown keys {sorted(unknown)}")
    if "name" not in contents:
        fail(f"{path}: no name")
    return cast(Descriptor, contents)


def collect_sources(root: Path, module_dirs: list[Path]) -> dict[Path, ModuleSources]:
    """
    Every .ixx and .cpp under root, grouped by the nearest module directory above it.
    """
    sources = {module_dir: ModuleSources([], []) for module_dir in module_dirs}
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".ixx", ".cpp"):
            continue
        owner = next((parent for parent in path.parents if parent in sources), None)
        if owner is None:
            continue
        group = sources[owner].interfaces if path.suffix == ".ixx" else sources[owner].implementations
        group.append(path.as_posix())
    return sources


def existing_file(target: str, file: str) -> list[str]:
    """
    @return the CMake lines setting _MODULE_FILE to the normalized file, failing the configure when it is missing
    """
    return [
        f'cmake_path(SET _MODULE_FILE NORMALIZE "{file}")',
        'if (NOT EXISTS "${_MODULE_FILE}")',
        f'  message(FATAL_ERROR "{target}: no file \'${{_MODULE_FILE}}\'")',
        "endif ()",
    ]


def module_cmake(descriptor_path: Path, descriptor: Descriptor, sources: ModuleSources) -> list[str]:
    """
    @return the CMake lines creating the module's target
    """
    name = descriptor["name"]
    interfaces, implementations = sources
    lines = [f"# {name}: {descriptor_path.as_posix()}"]

    kind = "OBJECT" if descriptor.get("object") else "STATIC"
    lines.append(call("add_library", name, kind, implementations))

    if interfaces:
        base_dir = descriptor_path.parent.as_posix()
        lines.append(call("target_sources", name, f'PUBLIC FILE_SET CXX_MODULES BASE_DIRS "{base_dir}" FILES', interfaces))

    extra_interfaces = descriptor.get("extra_module_interfaces", [])
    for file in extra_interfaces:
        lines += existing_file(name, file) + [
            "cmake_path(GET _MODULE_FILE PARENT_PATH _MODULE_DIR)",
            f'target_sources({name} PUBLIC FILE_SET CXX_MODULES BASE_DIRS "${{_MODULE_DIR}}" FILES "${{_MODULE_FILE}}")',
        ]

    extra_units = descriptor.get("extra_translation_units", [])
    for file in extra_units:
        lines += existing_file(name, file) + [f'target_sources({name} PRIVATE "${{_MODULE_FILE}}")']

    depends = descriptor.get("depends", [])
    private_depends = descriptor.get("private_depends", [])
    if interfaces or extra_interfaces:
        private_depends = ["module_flags", *private_depends]
    lines.append(call("target_link_libraries", name,
                      "PUBLIC" if depends else "", depends,
                      "PRIVATE" if private_depends else "", private_depends))
    lines.append(call("set_target_properties", name, "PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON"))
    if not extra_interfaces and not extra_units:
        # Unhandled enum values in a switch are errors.
        lines.append(call("target_compile_options", name, "PRIVATE -Werror=switch"))

    for key, (command, visibility) in TARGET_PROPERTIES.items():
        if key in descriptor:
            lines.append(call(command, name, visibility, descriptor[key]))
    if "alias" in descriptor:
        lines.append(f"add_library({descriptor['alias']} ALIAS {name})")
    return lines


def generate(root: Path, list_variable: str) -> str:
    """
    @return the CMake for every .module.toml under root, ending with set(<list_variable> <targets>)
    """
    #Gather all .module.toml files under root, and parse them into descriptors.
    descriptor_paths = sorted(root.rglob("*" + DESCRIPTOR_SUFFIX)) if root.is_dir() else []
    descriptors: dict[Path, DescriptorFile] = {}
    for path in descriptor_paths:
        if path.parent in descriptors:
            fail(f"two descriptors in {path.parent}")
        descriptors[path.parent] = DescriptorFile(path, load_descriptor(path))

    #Check for duplicate module names.
    names = [file.descriptor["name"] for file in descriptors.values()]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        fail(f"duplicate module names {duplicates} under {root}")

    #gather sources under each module
    sources = collect_sources(root, list(descriptors))
    lines = [f"# Generated by generate_modules.py from the .module.toml files under {root.as_posix()}.", ""]
    targets: list[str] = []
    #add source files, and dependencies for each module
    for module_dir, (path, descriptor) in descriptors.items():
        lines += module_cmake(path, descriptor, sources[module_dir])
        lines.append("")
        name = descriptor["name"]
        targets.append(f"$<LINK_LIBRARY:WHOLE_ARCHIVE,{name}>" if descriptor.get("whole_archive") else name)
    lines.append(call("set", list_variable, targets))
    return "\n".join(lines) + "\n"


def main() -> None:
    if len(sys.argv) not in (3, 4):
        fail("usage: generate_modules.py <root> <list variable> [<output .cmake>]")
    #generate cmake
    cmake = generate(Path(sys.argv[1]).resolve(), sys.argv[2])
    #if no output path, print cmake
    if len(sys.argv) == 3:
        print(cmake, end="")
        return
    #write cmake to output path
    output = Path(sys.argv[3])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(cmake, encoding="utf-8")


if __name__ == "__main__":
    main()
