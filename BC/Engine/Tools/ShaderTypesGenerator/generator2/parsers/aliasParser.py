from tree_sitter import Node
from .fieldParser import ParsedType, parseTypeNode

def parseAliasNode(node: Node) -> tuple[str, ParsedType] | None:

    if node.parent is not None and node.parent.type == "template_declaration":
        return None
    name = node.child_by_field_name("name").text.decode()
    return name, parseTypeNode(node.child_by_field_name("type").child_by_field_name("type"))
