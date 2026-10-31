from tree_sitter import Node
from typing import List
from .fieldParser import ParsedType, parseTypeNode
from .constantParser import ParsedConstant, expressionText

def parseEnumNode(node: Node) -> tuple[str, ParsedType, List[ParsedConstant]] | None:
    """
    enum M : uint32_t { A = 1u, B }; -> ("M", uint32_t, [("A", uint32_t, "1u"), ("B", uint32_t, "A + 1")]).
    An enumerator without a value is the previous one plus 1, the first is 0.
    Enums without a body or name are None.
    """
    name = node.child_by_field_name("name")
    body = node.child_by_field_name("body")
    if name is None or body is None:
        return None
    base = node.child_by_field_name("base")
    type = parseTypeNode(base) if base is not None else ParsedType("int32_t", False, None)
    constants: List[ParsedConstant] = []
    for enumerator in body.children:
        if enumerator.type != "enumerator":
            continue
        value = enumerator.child_by_field_name("value")
        if value is not None:
            text = expressionText(value)
        else:
            text = f"{constants[-1].name} + 1" if constants else "0"
        constants.append(ParsedConstant(enumerator.child_by_field_name("name").text.decode(), type, text))
    return name.text.decode(), type, constants
