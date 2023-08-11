#define PY_SSIZE_T_CLEAN
#include <Python.h>

uint32_t findLineStart(PyCodeObject* code, int32_t line, char** outLineTablePtr);
PyObject* appendTuple(PyObject* tuple, uint32_t count, PyObject** items, int32_t* indices);
