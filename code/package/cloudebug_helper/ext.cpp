#include "ext.hpp"
#include <stdexcept>
#include "pyutil.hpp"
#include "manipulation.hpp"

static PyObject* BytecodeManipulationError;
PyObject* cloudebugModuleName;
PyObject* cloudebugBreakpointCallbackName;

static PyObject* cloudebugHelperBreakpoint(PyObject* self, PyObject* args) {
    PyObject* obj;
    int line;
    int id;
    // Parsing and checking of arguments.
    if (!PyArg_ParseTuple(args, "O|i|i", &obj, &line, &id)) {
        return NULL;
    }
    if (!PyCode_Check(obj)) {
        PyErr_SetString(BytecodeManipulationError, "Passed object is not a valid code object.");
        return NULL;
    }
    // Increment the reference count to the code object (just in case).
    Py_INCREF(obj);
    try {
        addBreakpoint((PyCodeObject*) obj, line, id);
    } catch (std::runtime_error& exc) {
        PyErr_SetString(BytecodeManipulationError, exc.what());
        return NULL;
    }
    // Decrement the reference count to the code object.
    Py_DECREF(obj);
    Py_RETURN_NONE;
}

static PyObject* cloudebugHelperRemoveBreakpoint(PyObject* self, PyObject* args) {
    PyObject* obj;
    int line;
    int id;
    // Parsing and checking of arguments.
    if (!PyArg_ParseTuple(args, "O|i|i", &obj, &line, &id)) {
        return NULL;
    }
    if (!PyCode_Check(obj)) {
        PyErr_SetString(BytecodeManipulationError, "Passed object is not a valid code object.");
        return NULL;
    }
    // Increment the reference count to the code object (just in case).
    Py_INCREF(obj);
    try {
        removeBreakpoint((PyCodeObject*) obj, line, id);
    } catch (std::runtime_error& exc) {
        PyErr_SetString(BytecodeManipulationError, exc.what());
        return NULL;
    }
    // Decrement the reference count to the code object.
    Py_DECREF(obj);
    Py_RETURN_NONE;
}

static struct PyModuleDef cloudebugHelperModule = {
    PyModuleDef_HEAD_INIT,
    "cloudebug_helper",
    "Cloud debugging utilities for my graduation work (bytecode manipulation C extension).",
    -1,
    (PyMethodDef[]) {
        {"breakpoint", cloudebugHelperBreakpoint, METH_VARARGS, "Injects a breakpoint."},
        {"remove_breakpoint", cloudebugHelperRemoveBreakpoint, METH_VARARGS, "Removes a breakpoint."},
        {NULL, NULL, 0, NULL}
    }
};

PyMODINIT_FUNC PyInit_cloudebug_helper(void) {
    PyObject *m = PyModule_Create(&cloudebugHelperModule);
    if (m == NULL) {
        return NULL;
    }
    BytecodeManipulationError = PyErr_NewException("cloudebug_helper.BytecodeManipulationError", NULL, NULL);
    cloudebugModuleName = PyUnicode_FromString("cloudebug");
    cloudebugBreakpointCallbackName = PyUnicode_FromString("breakpoint_callback");
    Py_XINCREF(BytecodeManipulationError);
    if (PyModule_AddObject(m, "BytecodeManipulationError", BytecodeManipulationError) < 0) {
        Py_XDECREF(BytecodeManipulationError);
        Py_CLEAR(BytecodeManipulationError);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
