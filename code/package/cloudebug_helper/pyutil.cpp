#include "pyutil.hpp"
#include <stdexcept>

uint32_t findLineStart(PyCodeObject* code, int32_t line, char** outLineTablePtr) {
    if (!PyCode_Check(code)) {
        throw std::runtime_error("Object passed to appendTuple is not a code object.");
    }
    PyObject* lineTable = code->co_linetable;
    Py_ssize_t lineTableSize = PyBytes_Size(lineTable);
    char* lineTablePtr = PyBytes_AsString(lineTable);
    char* lineTableLimit = lineTablePtr + lineTableSize;
    int32_t currentLine = code->co_firstlineno;
    int32_t currentStartOffset = -1;
    int32_t currentEndOffset = 0;
    bool hasLine = true;
    while (lineTablePtr < lineTableLimit) {
        currentStartOffset = currentEndOffset;
        do {
            int bytecodeDelta = ((unsigned char*) lineTablePtr)[0];
            int lineDelta = ((unsigned char*) lineTablePtr)[1];
            currentEndOffset += bytecodeDelta;
            lineTablePtr += 2;
            hasLine = lineDelta != -128;
            if (hasLine) {
                currentLine += lineDelta;
            }
        } while (currentStartOffset == currentEndOffset && lineTablePtr < lineTableLimit);
        if (hasLine && currentLine >= line) {
            if (outLineTablePtr != NULL) {
                *outLineTablePtr = lineTablePtr - 2;
            }
            return currentStartOffset;
        }
    }
    throw std::runtime_error("Failed to locate the given line in the code.");
}

PyObject* appendTuple(PyObject* tuple, uint32_t count, PyObject** items, int32_t* indices) {
    if (!PyTuple_Check(tuple)) {
        throw std::runtime_error("Object passed to appendTuple is not a tuple.");
    }
    for (unsigned i = 0; i < count; ++i) {
        indices[i] = -1;
    }
    Py_ssize_t oldSize = PyTuple_Size(tuple);
    Py_ssize_t newSize = oldSize + count;
    // Find whether the reference was already in the tuple.
    for (Py_ssize_t i = 0; i < oldSize; ++i) {
        PyObject* item = PyTuple_GET_ITEM(tuple, i);
        for (unsigned j = 0; j < count; ++j) {
            if (items[j] == item && indices[j] == -1) {
                // One less reference to insert into the tuple.
                indices[j] = (int) i;
                --newSize;
            }
        }
    }
    if (newSize == oldSize) {
        // We already appended everything we wanted.
        return tuple;
    }
    // Extend the tuple to the required number of objects.
    if (Py_REFCNT(tuple) == 1) {
        // Our tuple is referenced only once, so we can simply resize it.
        if (_PyTuple_Resize(&tuple, newSize) == -1) {
            throw std::runtime_error("appendTuple failed to resize tuple.");
        }
    } else {
        // CPython can reference the same tuple from multiple functions.
        PyObject* oldTuple = tuple;
        PyObject* newTuple = PyTuple_New(newSize);
        if (newTuple == NULL) {
            throw std::runtime_error("appendTuple failed to allocate tuple.");
        }
        for (Py_ssize_t i = 0; i < oldSize; ++i) {
            PyTuple_SET_ITEM(newTuple, i, PyTuple_GET_ITEM(oldTuple, i));
        }
        Py_DECREF(oldTuple);
        tuple = newTuple;
    }
    // Store the new items in the tuple.
    Py_ssize_t indexToStore = oldSize;
    for (unsigned i = 0; i < count; ++i) {
        if (indices[i] == -1) {
            indices[i] = (int) indexToStore;
            PyTuple_SET_ITEM(tuple, indexToStore++, items[i]);
        }
    }
    return tuple;
}
