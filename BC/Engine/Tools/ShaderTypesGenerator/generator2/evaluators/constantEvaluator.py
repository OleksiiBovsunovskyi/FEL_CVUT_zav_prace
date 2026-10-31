import ast
import operator
import re
from typing import Callable

_INTEGER_SUFFIX = re.compile(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b")

_BINARY: dict[type, Callable[[int, int], int]] = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}

_UNARY: dict[type, Callable[[int], int]] = {
    ast.USub: operator.neg,
    ast.UAdd: operator.pos,
    ast.Invert: operator.invert,
}

def evaluateInteger(expression: str, lookup: Callable[[str], int]) -> int:
    """
    Value of a C++ integer constant expression.
    Integer literals, named constants and + - * / % << >> | & ^ ~ are accepted.
    / is integer division.
    @param lookup value of a named constant
    @note Raises ValueError on anything else, floating-point literals included.
    """
    source = _INTEGER_SUFFIX.sub(r"\1", expression)
    try:
        tree = ast.parse(source, mode="eval")
    except SyntaxError:
        raise ValueError(f"Not an integer expression: {expression}")

    def visit(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            return lookup(node.id)
        if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
            return _BINARY[type(node.op)](visit(node.left), visit(node.right))
        if isinstance(node, ast.UnaryOp) and type(node.op) in _UNARY:
            return _UNARY[type(node.op)](visit(node.operand))
        raise ValueError(f"Not an integer expression: {expression}")

    return visit(tree.body)
