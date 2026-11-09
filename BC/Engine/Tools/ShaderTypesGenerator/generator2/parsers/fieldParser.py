from tree_sitter import Node
from typing import List, NamedTuple

class ParsedType(NamedTuple):
    type: str
    is_template: bool
    template_type: str | None

class ParsedField(NamedTuple):
    name: str
    type: ParsedType
    array_size: str | None  # "" for an unsized array
    bit_width: str | None = None  # width expression of a bitfield

def parseTypeNode(type_node: Node) -> ParsedType:
    """
    Type named by a type node.
    GpuPtr<GPUVertex> -> ("GpuPtr", True, "GPUVertex")
    float -> ("float", False, None).
    """
    if type_node.type != "template_type":
        return ParsedType(type_node.text.decode(), False, None)
    # one nesting level: only the first template argument is kept
    template_type = type_node.child_by_field_name("arguments").named_children[0].text.decode()
    return ParsedType(type_node.child_by_field_name("name").text.decode(), True, template_type)

def unwrap(decl: Node) -> tuple[str | None, str, str | None]:
    """
    Field name, type suffix and array size carried by its declarator.
    float a[4] -> ("a", "", "4"), T* p -> ("p", "*", None).
    Name is None for a method declarator.
    Array size is "" for an unsized array.
    Multi-dimensional arrays raise ValueError.
    """
    if decl.type == "field_identifier":
        return decl.text.decode(), "", None
    if decl.type == "function_declarator":
        return None, "", None
    if decl.type == "array_declarator":
        name, suffix, inner_size = unwrap(decl.child_by_field_name("declarator"))
        if inner_size is not None:
            raise ValueError(f"multi-dimensional array field: {decl.text.decode()}")
        size = decl.child_by_field_name("size")
        return name, suffix, size.text.decode() if size is not None else ""
    if decl.type in ("pointer_declarator", "reference_declarator"):
        # reference_declarator has no "declarator" field; its inner declarator is the last named child
        inner = decl.child_by_field_name("declarator") or decl.named_children[-1]
        name, suffix, size = unwrap(inner)
        return name, ("*" if decl.type == "pointer_declarator" else "&") + suffix, size
    raise ValueError(f"unhandled field declarator {decl.type}: {decl.text.decode()}")

def parseFieldDeclaration(node: Node) -> List[ParsedField]:
    """
    Fields declared by one field_declaration node.
    GpuPtr<GPUVertex> v, w[2]; -> two fields.
    uint32_t a : 16, b : 16; -> two fields with bit widths "16".
    Static members yield no fields.
    """
    if any(c.type == "storage_class_specifier" and c.text == b"static" for c in node.children):
        return []
    parsed = parseTypeNode(node.child_by_field_name("type"))
    declarators = {d.id for d in node.children_by_field_name("declarator")}
    fields = []
    for child in node.children:
        if child.type == "bitfield_clause" and fields:
            width = next(c for c in child.named_children if c.type != "comment")
            fields[-1] = fields[-1]._replace(bit_width=width.text.decode())
        elif child.id in declarators:
            field_name, suffix, array_size = unwrap(child)
            if field_name is None:  # method
                continue
            fields.append(ParsedField(field_name, parsed._replace(type=parsed.type + suffix), array_size))
    return fields
