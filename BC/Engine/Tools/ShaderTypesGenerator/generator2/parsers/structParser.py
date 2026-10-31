from tree_sitter import Node
from typing import List, NamedTuple
from .fieldParser import ParsedField, parseFieldDeclaration

class ParsedStruct(NamedTuple):
    name: str
    fields: List[ParsedField]

def parseStructNode(node: Node) -> ParsedStruct | None:
    name_node = node.child_by_field_name("name")
    if name_node is None:
        return None

    # template<typename T> struct X { ... }: the struct is the template_declaration's child
    if node.parent is not None and node.parent.type == "template_declaration":
        return None

    fields = []
    body = node.child_by_field_name("body")
    if body is not None:
        if body.has_error:
            bad = next(c for c in body.children if c.has_error)
            raise ValueError(f"unparsable field in struct {name_node.text.decode()} at line {bad.start_point[0] + 1}: {bad.text.decode()}")
        for child in body.named_children:
            if child.type == "field_declaration":
                fields.extend(parseFieldDeclaration(child))

    return ParsedStruct(name_node.text.decode(), fields)