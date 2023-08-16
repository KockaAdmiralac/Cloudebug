from traceback import format_exception
from types import FrameType
from typing import Any, Dict, Optional, Tuple

cached_expressions: Dict[str, Tuple[Optional[str], Any]] = {}

def format_error(error: Exception) -> str:
    return ' '.join([line.strip() for line in format_exception(error)])

def evaluate_expression(expr: str, frame: FrameType) -> Tuple[Optional[str], Any]:
    if expr in cached_expressions:
        error, compiled_expr = cached_expressions[expr]
        if error is not None:
            return error, None
    else:
        try:
            compiled_expr = compile(expr, '<Cloudebug expression>', 'eval')
            cached_expressions[expr] = None, compiled_expr
        except Exception as error:
            error_str = f'<Compilation error: {format_error(error)}>'
            cached_expressions[expr] = error_str, None
            return error_str, None
    try:
        value = eval(compiled_expr, frame.f_globals, frame.f_locals)
        return None, value
    except Exception as error:
        return f'<Evaluation error: {format_error(error)}>', None
