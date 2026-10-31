"""
Generates a Slang module from each C++ .ixx module of shader types.
"""

import re
import sys
import tree_sitter_cpp
from tree_sitter import Language, Parser
from . import removeList
from . import coreTypes
from .parsers.structParser import parseStructNode, ParsedStruct
from .parsers.fieldParser import ParsedField, ParsedType
from .parsers.aliasParser import parseAliasNode
from .parsers.constantParser import ParsedConstant, parseConstantNode
from .parsers.enumParser import parseEnumNode
from .evaluators.constantEvaluator import evaluateInteger
from typing import List
from pathlib import Path
from .moduleOrder import orderByImports, readModuleHeader
from tree_sitter import Node

parser = Parser(Language(tree_sitter_cpp.language()))


# Collected across files in import order
aliases:dict[str, ParsedType] = {}  # alias name -> target type, aliases of aliases resolved
constants:dict[str, ParsedConstant] = {}
structLayouts:dict[str, tuple[int, int]] = {}  # name -> (size, alignment) in bytes

def resolveType(t:ParsedType) -> ParsedType:
    """
    Type an alias names, or t itself when it is a template or no alias.
    """
    return t if t.is_template else aliases.get(t.type, t)

def typeToSlang(t:ParsedType, name:str) -> str:
    """
    Slang spelling of a type.
    @param name field or constant the type belongs to, for the error message
    """
    core = coreTypes.coreType(t)
    if not t.is_template:
        return core[0] if core is not None else t.type
    if core is None:
        raise ValueError(f"Unknown template type {t.type} on {name}")
    return typeToSlang(ParsedType(t.template_type, False, None), name) + core[0]

def fieldToSlang(f:ParsedField) -> str:
    array = f"[{f.array_size}]" if f.array_size is not None else ""
    return f"{typeToSlang(f.type, f.name)} {f.name}{array}"

def slangValue(c:ParsedConstant) -> str:
    value = c.value
    for cpp, slang in coreTypes.ConstantExpressionList.items():
        value = value.replace(cpp, slang)
    return value

def constantToSlang(c:ParsedConstant) -> str:
    return f"static const {typeToSlang(c.type, c.name)} {c.name} = {slangValue(c)};"

def constantValue(name:str) -> int:
    """
    Integer value of a parsed constant.
    """
    constant = constants.get(name)
    if constant is None:
        raise ValueError(f"Unknown constant {name}")
    return evaluateInteger(slangValue(constant), constantValue)

#Structs size is summ of the fields size, aligned to the largest field alignment.

def alignUp(offset:int, alignment:int) -> int:
    return (offset + alignment - 1) // alignment * alignment

def fieldLayout(f:ParsedField) -> tuple[int, int]:
    """
    Size and alignment in bytes of the field in the slang natural layout.
    Size covers every element of an array field.
    """
    core = coreTypes.coreType(f.type)
    if core is not None:
        _, size, alignment = core
    elif not f.type.is_template and f.type.type in structLayouts:
        size, alignment = structLayouts[f.type.type]
    else:
        raise ValueError(f"Unknown size of type {f.type.type} on field {f.name}")
    if f.array_size is None:
        return size, alignment
    if f.array_size == "":
        raise ValueError(f"Unsized array field {f.name} has no size")
    return size * evaluateInteger(f.array_size, constantValue), alignment

def paddingField(index:int, byteCount:int) -> str:
    """
    Slang declaration of byteCount bytes of padding, made of uints.
    @param index padding number within the struct
    """
    if byteCount % 4 != 0:
        raise ValueError(f"Padding of {byteCount} bytes is not a multiple of 4")
    count = byteCount // 4
    return f"uint padding_{index}" + (f"[{count}]" if count > 1 else "")

def structToSlang(s:ParsedStruct) -> str:
    """
    Slang declaration of the struct, with padding fields wherever the natural layout leaves a gap.
    Records the struct's size and alignment in structLayouts.
    """
    offset = 0
    structAlignment = 1
    lines:List[str] = []
    paddingCount = 0
    for f in s.fields:
        size, alignment = fieldLayout(f)
        start = alignUp(offset, alignment)
        if start > offset:
            lines.append(f"    public {paddingField(paddingCount, start - offset)};")
            paddingCount += 1
        lines.append(f"    public {fieldToSlang(f)};")
        offset = start + size
        structAlignment = max(structAlignment, alignment)
    alignedSize = alignUp(offset, structAlignment)
    if alignedSize > offset:
        lines.append(f"    public {paddingField(paddingCount, alignedSize - offset)};")
    structLayouts[s.name] = (alignedSize, structAlignment)
    return f"public struct {s.name} {{\n" + "".join(f"{l}\n" for l in lines) + "};"

def docComment(node:Node) -> str:
    """
    /** */ comment directly above the namespace-scope declaration holding node, with a newline.
    Empty without one.
    """
    while node.parent is not None and node.parent.type not in ("translation_unit", "declaration_list"):
        node = node.parent
    prev = node.prev_sibling
    if prev is None or prev.type != "comment" or not prev.text.startswith(b"/**"):
        return ""
    return prev.text.decode() + "\n"

def externalStructs(typeName:str) -> List[str]:
    """
    Slang declarations of the ExternalStructList struct named typeName, when it is not generated yet.
    """
    source = coreTypes.ExternalStructList.get(typeName)
    if source is None or typeName in structLayouts:
        return []
    root = parser.parse(source.encode()).root_node
    struct = next(parseStructNode(n) for n in root.children if n.type == "struct_specifier")
    return [structToSlang(struct)]

def walk(node:Node, declarations:List[str]) -> None:
    """
    Appends the Slang declarations under node, in source order.
    """
    if node.type == "declaration":
        for c in parseConstantNode(node):
            constants[c.name] = c
            declarations.append("public " + constantToSlang(c))
    elif node.type == "alias_declaration":
        alias = parseAliasNode(node)
        if alias is not None:
            name, target = alias
            target = resolveType(target)
            aliases[name] = target
            declarations.extend(externalStructs(target.type))
            declarations.append(f"public typealias {name} = {typeToSlang(target, name)};")
    elif node.type == "enum_specifier":
        enum = parseEnumNode(node)
        if enum is not None:
            name, aliases[name], enumerators = enum
            constants.update((c.name, c) for c in enumerators)
            declarations.append("\n".join(f"public {constantToSlang(c)}" for c in enumerators))
    elif node.type == "struct_specifier":
        struct = parseStructNode(node)
        # core types are spelled by coreTypes, their C++ wrapper structs are not generated
        if struct is not None and struct.name not in coreTypes.SimpleTypeList and struct.name not in coreTypes.TemplateTypeList:
            fields = [f._replace(type=resolveType(f.type)) for f in struct.fields]
            for f in fields:
                declarations.extend(externalStructs(f.type.type))
            declarations.append(docComment(node) + structToSlang(struct._replace(fields=fields)))
    for child in node.children:
        walk(child, declarations)

def fileHeader(path:Path) -> str:
    return (f"/// Generated from {path.as_posix()}\n"
            "/// Do not edit this file directly, edit the .ixx file instead.\n"
            "/// All changes to this file may be discarded anytime.\n"
            "/// ***************************************************************\n")

def generateShaders(InputDir:Path, OutputDir:Path) -> bool:
    """
    Writes OutputDir/<module>.slang for each .ixx file under InputDir.
    @param InputDir directory searched recursively for .ixx files
    @param OutputDir directory the .slang files are written to
    @return False when a .ixx file could not be translated; the error is printed to stderr
    """
    aliases.clear()
    constants.clear()
    structLayouts.clear()
    try:
        writeModules(Path(InputDir), Path(OutputDir))
    except ValueError as error:
        print(error, file=sys.stderr)
        return False
    return True

def writeModules(InputDir:Path, OutputDir:Path) -> None:
    generatedModules:set[str] = set()  # modules with Slang output
    sources = {p: p.read_text(encoding="utf-8-sig") for p in sorted(InputDir.rglob("*.ixx"))}
    headers = {p: readModuleHeader(s) for p, s in sources.items()}
    for path in orderByImports(headers):
        header = headers[path]
        if header.name is None:
            raise ValueError(f"{path}: no export module declaration")

        #blank unparsable tokens, keeping byte offsets
        src = sources[path].encode()
        for tok in removeList.removeList:
            src = re.sub(rb"\b" + re.escape(tok.encode()) + rb"\b", lambda m: b" " * len(m[0]), src)

        declarations:List[str] = []
        walk(parser.parse(src).root_node, declarations)
        # modules with no Slang output, such as GPUCoreTypes, are not imported
        imports = [f"{'__exported import' if i.exported else 'import'} {i.name};"
                   for i in header.imports if i.name in generatedModules]
        if not declarations and not imports:
            continue
        generatedModules.add(header.name)

        text = fileHeader(path) + f"\nmodule {header.name};\n"
        if imports:
            text += "\n" + "\n".join(imports) + "\n"
        text += "".join(f"\n{d}\n" for d in declarations)
        output = OutputDir / f"{header.name}.slang"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")
        print(f"Wrote {output}")