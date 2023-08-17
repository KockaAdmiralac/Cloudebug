from dis import opmap
from enum import IntEnum
from sys import setprofile, settrace
from traceback import format_exception
from types import CodeType, FrameType
from typing import Any, Dict, Optional, Tuple

cached_expressions: Dict[str, Tuple[Optional[str], Any]] = {}

allowed_c_funcs = {
    'abs',
    'all',
    'any',
    'apply',
    'bin',
    'bool',
    'bytearray',
    'chr',
    'cmp',
    'coerce',
    'complex',
    'dict',
    'dir',
    'divmod',
    'enumerate',
    'filter',
    'float',
    'format',
    'frozenset',
    'getattr',
    'globals',
    'hasattr',
    'hash',
    'hex',
    'id',
    'int',
    'isinstance',
    'issubclass',
    'iter',
    'len',
    'list',
    'locals',
    'long',
    'map',
    'max',
    'min',
    'next',
    'object',
    'oct',
    'ord',
    'pow',
    'range',
    'reduce',
    'repr',
    'reversed',
    'round',
    'set',
    'slice',
    'sorted',
    'str',
    'sum',
    'super',
    'tuple',
    'type',
    'unichr',
    'unicode',
    'vars',
    'xrange',
    'zip'
}

disallowed_names = {
    '__call__',
    '__del__',
    '__delattr__',
    '__delete__',
    '__delitem__',
    '__delslice__',
    '__new__',
    '__set__',
    '__setattr__',
    '__setitem__',
    '__setslice__'
}

opcode_mutability_names = {
    'POP_TOP': False,
    'ROT_TWO': False,
    'ROT_THREE': False,
    'DUP_TOP': False,
    'NOP': False,
    'UNARY_POSITIVE': False,
    'UNARY_NEGATIVE': False,
    'UNARY_INVERT': False,
    'BINARY_POWER': False,
    'BINARY_MULTIPLY': False,
    'BINARY_MODULO': False,
    'BINARY_ADD': False,
    'BINARY_SUBTRACT': False,
    'BINARY_SUBSCR': False,
    'BINARY_FLOOR_DIVIDE': False,
    'BINARY_TRUE_DIVIDE': False,
    'INPLACE_FLOOR_DIVIDE': False,
    'INPLACE_TRUE_DIVIDE': False,
    'INPLACE_ADD': False,
    'INPLACE_SUBTRACT': False,
    'INPLACE_MULTIPLY': False,
    'INPLACE_MODULO': False,
    'BINARY_LSHIFT': False,
    'BINARY_RSHIFT': False,
    'BINARY_AND': False,
    'BINARY_XOR': False,
    'INPLACE_POWER': False,
    'GET_ITER': False,
    'INPLACE_LSHIFT': False,
    'INPLACE_RSHIFT': False,
    'INPLACE_AND': False,
    'INPLACE_XOR': False,
    'INPLACE_OR': False,
    'RETURN_VALUE': False,
    'YIELD_VALUE': False,
    'POP_BLOCK': False,
    'UNPACK_SEQUENCE': False,
    'FOR_ITER': False,
    'LOAD_CONST': False,
    'LOAD_NAME': False,
    'BUILD_TUPLE': False,
    'BUILD_LIST': False,
    'BUILD_SET': False,
    'BUILD_MAP': False,
    'LOAD_ATTR': False,
    'COMPARE_OP': False,
    'JUMP_FORWARD': False,
    'JUMP_IF_FALSE_OR_POP': False,
    'JUMP_IF_TRUE_OR_POP': False,
    'POP_JUMP_IF_TRUE': False,
    'POP_JUMP_IF_FALSE': False,
    'LOAD_GLOBAL': False,
    'LOAD_FAST': False,
    'STORE_FAST': False,
    'DELETE_FAST': False,
    'CALL_FUNCTION': False,
    'MAKE_FUNCTION': False,
    'BUILD_SLICE': False,
    'LOAD_DEREF': False,
    'CALL_FUNCTION_KW': False,
    'EXTENDED_ARG': False,
    'DUP_TOP_TWO': False,
    'BINARY_MATRIX_MULTIPLY': False,
    'INPLACE_MATRIX_MULTIPLY': False,
    'GET_YIELD_FROM_ITER': False,
    'YIELD_FROM': False,
    'UNPACK_EX': False,
    'CALL_FUNCTION_EX': False,
    'LOAD_CLASSDEREF': False,
    'LIST_TO_TUPLE': False,
    'IS_OP': False,
    'CONTAINS_OP': False,
    'JUMP_IF_NOT_EXC_MATCH': False,
    'FORMAT_VALUE': False,
    'BUILD_CONST_KEY_MAP': False,
    'BUILD_STRING': False,
    'LOAD_METHOD': False,
    'CALL_METHOD': False,
    'ROT_FOUR': False,
    'COPY_DICT_WITHOUT_KEYS': False,
    'GET_LEN': False,
    'MATCH_MAPPING': False,
    'MATCH_SEQUENCE': False,
    'MATCH_KEYS': False,
    'MATCH_CLASS': False,
    'ROT_N': False,
    'PRINT_EXPR': True,
    'STORE_GLOBAL': True,
    'DELETE_GLOBAL': True,
    'IMPORT_STAR': True,
    'IMPORT_NAME': True,
    'IMPORT_FROM': True,
    'SETUP_FINALLY': True,
    'STORE_SUBSCR': True,
    'DELETE_SUBSCR': True,
    'STORE_NAME': True,
    'DELETE_NAME': True,
    'STORE_ATTR': True,
    'DELETE_ATTR': True,
    'LIST_APPEND': True,
    'SET_ADD': True,
    'MAP_ADD': True,
    'STORE_DEREF': True,
    'RAISE_VARARGS': True,
    'SETUP_WITH': True,
    'LOAD_CLOSURE': True,
    'GET_AITER': True,
    'GET_ANEXT': True,
    'BEFORE_ASYNC_WITH': True,
    'LOAD_BUILD_CLASS': True,
    'GET_AWAITABLE': True,
    'SETUP_ANNOTATIONS': True,
    'POP_EXCEPT': True,
    'DELETE_DEREF': True,
    'SETUP_ASYNC_WITH': True,
    'END_ASYNC_FOR': True,
    'DICT_MERGE': True,
    'DICT_UPDATE': True,
    'LIST_EXTEND': True,
    'SET_UPDATE': True,
    'RERAISE': True,
    'WITH_EXCEPT_START': True,
    'LOAD_ASSERTION_ERROR': True,
    'GEN_START': True
}

class OpcodeMutability(IntEnum):
    IMMUTABLE = 0
    MAYBE_MUTABLE = 1
    MUTABLE = 2

opcode_mutability: Dict[int, OpcodeMutability] = {}
ignored_frame: Optional[FrameType] = None

for op_name, value in opmap.items():
    if op_name in opcode_mutability_names:
        opcode_mutability[value] = OpcodeMutability.MUTABLE if opcode_mutability_names[op_name] else OpcodeMutability.IMMUTABLE
    else:
        opcode_mutability[value] = OpcodeMutability.MAYBE_MUTABLE

class ImmutabilityError(Exception):
    def __init__(self):
        super().__init__('The evaluated expression is not immutable.')

def check_code_object(code: CodeType):
    for name in code.co_names:
        if name in disallowed_names:
            raise ImmutabilityError()

def check_opcodes(code: CodeType, curr_line: int):
    start_offset = -1
    end_offset = -1
    for start, end, line in code.co_lines():
        if line == curr_line:
            start_offset = start
            end_offset = end
    if start_offset == -1:
        # No such line?
        return
    bytecode = code.co_code
    for offset in range(start_offset, end_offset, 2):
        opcode = bytecode[offset]
        mutability = opcode_mutability[opcode]
        if mutability == OpcodeMutability.IMMUTABLE:
            continue
        elif mutability == OpcodeMutability.MAYBE_MUTABLE and opcode == opmap['JUMP_ABSOLUTE']:
            if bytecode[offset + 1] == offset:
                # Infinite loop
                raise ImmutabilityError()
        else:
            raise ImmutabilityError()

def check_c_call(c_func):
    if c_func.__name__ not in allowed_c_funcs:
        raise ImmutabilityError()

# Based on Google Cloud Debugger's immutability tracer:
# https://github.com/GoogleCloudPlatform/cloud-debug-python/blob/main/src/googleclouddebugger/immutability_tracer.cc
def immutability_tracer(frame: FrameType, event: str, arg):
    if frame.f_code == evaluate_expression.__code__:
        # We don't want to capture what happens in evaluate_expression
        return
    if event == 'call':
        check_code_object(frame.f_code)
    elif event == 'line':
        check_opcodes(frame.f_code, frame.f_lineno)
    elif event == 'c_call':
        check_c_call(arg)
    return immutability_tracer

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
        settrace(immutability_tracer)
        setprofile(immutability_tracer)
        value = eval(compiled_expr, frame.f_globals, frame.f_locals)
        settrace(None)
        setprofile(None)
        return None, value
    except Exception as error:
        settrace(None)
        setprofile(None)
        return f'<Evaluation error: {format_error(error)}>', None
