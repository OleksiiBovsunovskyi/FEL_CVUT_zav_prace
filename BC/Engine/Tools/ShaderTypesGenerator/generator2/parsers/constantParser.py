from tree_sitter import Node
from typing import List, NamedTuple
from .fieldParser import ParsedType, parseTypeNode

class ParsedConstant(NamedTuple):
    name: str
    type: ParsedType
    value: str 

def expressionText(node: Node) -> str:
    """
    Source text of an expression node, with every whitespace run collapsed to one space.
    """
    return " ".join(node.text.decode().split())

def parseConstantNode(node: Node) -> List[ParsedConstant]:
    """
    constexpr uint32_t A = 4; -> [("A", ("uint32_t", False, None), "4")].
    Empty for a declaration that is not constexpr, or sits in a function, struct or template.
    """
    if node.parent is None or node.parent.type not in ("translation_unit", "declaration_list"):
        return []
    if not any(c.type == "type_qualifier" and c.text == b"constexpr" for c in node.children):
        return []
    parsed = parseTypeNode(node.child_by_field_name("type"))
    constants = []
    for decl in node.children_by_field_name("declarator"):
        if decl.type != "init_declarator":
            raise ValueError(f"constexpr without initializer: {decl.text.decode()}")
        constants.append(ParsedConstant(
            name=decl.child_by_field_name("declarator").text.decode(),
            type=parsed,
            value=expressionText(decl.child_by_field_name("value")),
        ))
    return constants
