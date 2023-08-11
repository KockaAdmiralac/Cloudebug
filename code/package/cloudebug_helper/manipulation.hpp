#define PY_SSIZE_T_CLEAN
#include <Python.h>

void addBreakpoint(PyCodeObject* code, int line, int breakpointId);
void removeBreakpoint(PyCodeObject* code, int line, int breakpointId);
