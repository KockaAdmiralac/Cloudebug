from dis import findlinestarts
from importlib.abc import InspectLoader
from os.path import abspath, dirname
from pathlib import Path
from sys import argv, modules
from types import CodeType, FunctionType, ModuleType
from typing import List, Optional

from cloudebug_helper import breakpoint, remove_breakpoint

def get_module_by_path(base: Path, rel_path: str) -> Optional[ModuleType]:
    for _, module in modules.items():
        if hasattr(module, '__file__') and isinstance(module.__file__, str) and Path(module.__file__) == (base / rel_path):
            return module

def get_func_from_module_and_line(module: ModuleType, line: int) -> Optional[CodeType]:
    if not isinstance(module.__loader__, InspectLoader):
        return None
    code_objects: List[CodeType] = []
    for _, module_item in module.__dict__.items():
        if isinstance(module_item, FunctionType) and isinstance(module_item.__code__, CodeType):
            code_objects.append(module_item.__code__)
    candidate_code = None
    min_lines = 1000000000000000000000000
    while code_objects:
        code_object = code_objects.pop()
        lines = [line for _, line in findlinestarts(code_object)]
        first_line = min(lines)
        last_line = max(lines)
        if first_line <= line <= last_line:
            num_lines = last_line - first_line + 1
            if num_lines < min_lines:
                min_lines = num_lines
                candidate_code = code_object
        for const in code_object.co_consts:
            if isinstance(const, CodeType):
                code_objects.append(const)
    return candidate_code

def get_base_path() -> Path:
    return Path(dirname(abspath(argv[0])))

def inject(base: Path, path: str, line: int, id: int):
    mod = get_module_by_path(base, path)
    if mod is None:
        return -1
    func = get_func_from_module_and_line(mod, line)
    if func is None:
        return -2
    mod.__setattr__('cloudebug', modules['cloudebug'])
    breakpoint(func, line, id)

def deject(base: Path, path: str, line: int, id: int):
    mod = get_module_by_path(base, path)
    if mod is None:
        return -1
    func = get_func_from_module_and_line(mod, line)
    if func is None:
        return -2
    remove_breakpoint(func, line, id)
