import re
from graphlib import TopologicalSorter
from pathlib import Path
from typing import List, NamedTuple

_MODULE_NAME = re.compile(r"^\s*export\s+module\s+([\w.:]+)\s*;", re.MULTILINE)
_IMPORT = re.compile(r"^\s*(export\s+)?import\s+([\w.:]+)\s*;", re.MULTILINE)

class ModuleImport(NamedTuple):
    name: str
    exported: bool  # export import

class ModuleHeader(NamedTuple):
    name: str | None  # None without an export module declaration
    imports: List[ModuleImport]

def readModuleHeader(source: str) -> ModuleHeader:
    """
    Module name and imports of a .ixx file, in declaration order.
    @param source text of the .ixx file
    """
    name = _MODULE_NAME.search(source)
    return ModuleHeader(
        name=name.group(1) if name is not None else None,
        imports=[ModuleImport(module, bool(export)) for export, module in _IMPORT.findall(source)],
    )

def orderByImports(headers: dict[Path, ModuleHeader]) -> List[Path]:
    """
    Sorts files by their module imports. Unrelated files are sorted in random order.
    @param headers module headers of the files to sort
    """
    modules: dict[str, Path] = {h.name: f for f, h in headers.items() if h.name is not None}
    # build import graph, file -> its imports
    graph: dict[Path, List[Path]] = {}
    for file, header in headers.items():
        graph[file] = []
        for imported in header.imports:
            if imported.name in modules:
                graph[file].append(modules[imported.name])
    return list(TopologicalSorter(graph).static_order())
