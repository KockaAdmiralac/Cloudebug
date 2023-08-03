#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>
#include "opcode.h"

static int findLineStart(PyCodeObject* codeObject, int line, char** outLineTablePtr) {
    PyObject* lineTable = codeObject->co_linetable;
    Py_ssize_t lineTableSize = PyBytes_Size(lineTable);
    char* lineTablePtr = PyBytes_AsString(lineTable);
    char* lineTableLimit = lineTablePtr + lineTableSize;
    int currentLine = codeObject->co_firstlineno;
    int currentStartOffset = -1;
    int currentEndOffset = 0;
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
    return -1;
}

static PyObject* appendTuple(PyObject* tuple, unsigned count, PyObject** items, int* indices) {
    if (!PyTuple_Check(tuple)) {
        // Invalid tuple to resize.
        return NULL;
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
            // Failed to resize tuple.
            return NULL;
        }
    } else {
        // CPython can reference the same tuple from multiple functions.
        PyObject* oldTuple = tuple;
        PyObject* newTuple = PyTuple_New(newSize);
        if (newTuple == NULL) {
            // Failed to allocate tuple.
            return NULL;
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

static PyObject* insertBytesAt(PyObject* bytes, unsigned count, const char* newContent, unsigned offset) {
    if (!PyBytes_Check(bytes)) {
        // Not a bytes object.
        return NULL;
    }
    Py_ssize_t oldBytesSize = PyBytes_Size(bytes);
    Py_ssize_t newBytesSize = oldBytesSize + count;
    if (offset > oldBytesSize) {
        // Invalid offset.
        return NULL;
    }
    char* oldBytesBuffer = PyBytes_AsString(bytes);
    char* newBytesBuffer = (char*) malloc(newBytesSize);
    // Copy bytes prior to the injection point.
    memcpy(newBytesBuffer, oldBytesBuffer, offset);
    // Copy injected bytes.
    memcpy(newBytesBuffer + offset, newContent, count);
    // Copy bytes after the injection point.
    memcpy(newBytesBuffer + offset + count, oldBytesBuffer + offset, oldBytesSize - offset);
    // Initialize a new bytes object (set to NULL on failure).
    PyObject* newBytes = PyBytes_FromStringAndSize(newBytesBuffer, newBytesSize);
    free(newBytesBuffer);
    return newBytes;
}

static PyObject* removeBytesAt(PyObject* bytes, unsigned count, unsigned offset) {
    if (!PyBytes_Check(bytes)) {
        // Not a bytes object.
        return NULL;
    }
    Py_ssize_t oldBytesSize = PyBytes_Size(bytes);
    Py_ssize_t newBytesSize = oldBytesSize - count;
    if (offset > oldBytesSize) {
        // Invalid offset.
        return NULL;
    }
    char* oldBytesBuffer = PyBytes_AsString(bytes);
    char* newBytesBuffer = (char*) malloc(newBytesSize);
    // Copy bytes prior to the injection point.
    memcpy(newBytesBuffer, oldBytesBuffer, offset);
    // Copy bytes after the injection point.
    memcpy(newBytesBuffer + offset, oldBytesBuffer + offset + count, oldBytesSize - offset);
    // Initialize a new bytes object (set to NULL on failure).
    PyObject* newBytes = PyBytes_FromStringAndSize(newBytesBuffer, newBytesSize);
    free(newBytesBuffer);
    return newBytes;
}

static unsigned int jabsLength(unsigned int addr) {
    if (addr == 0) {
        return 2;
    }
    int numberOfBits = log2(addr) + 1;
    int numberOfBytes = ceil((float)numberOfBits / 8.0f);
    return numberOfBytes * 2;
}

static unsigned int effectiveInstructionLength(char* instruction) {
    unsigned int instructionLength = 2;
    while (*instruction == EXTENDED_ARG) {
        instruction += 2;
        instructionLength += 2;
    }
    return instructionLength;
}

static void writeAbsoluteJump(char* instructionOffset, unsigned int jumpTarget) {
    unsigned int jumpInstructionSize = jabsLength(jumpTarget);
    for (unsigned int i = 0; i < jumpInstructionSize - 2; i += 2) {
        unsigned int numJumpBytes = jumpInstructionSize / 2;
        unsigned int currentIndex = i / 2;
        unsigned int currentByte = numJumpBytes - currentIndex;
        unsigned int currentByteContent = (jumpTarget & (0xFF << ((currentByte - 1) * 8))) >> ((currentByte - 1) * 8);
        instructionOffset[i] = EXTENDED_ARG;
        instructionOffset[i + 1] = currentByteContent;
    }
    instructionOffset[jumpInstructionSize - 2] = JUMP_ABSOLUTE;
    instructionOffset[jumpInstructionSize - 1] = (jumpTarget & 0xFF) / 2;
}

static PyObject* cloudebugModuleName;
static PyObject* cloudebugBreakpointCallbackName;
static PyObject* BytecodeManipulationError;

// TODO: Use a proper hashmap
#define CLOUDEBUG_MAX_BREAKPOINTS 800
char cloudebugOriginalInstructions[CLOUDEBUG_MAX_BREAKPOINTS * 16];
unsigned int cloudebugOriginalInstructionSize[CLOUDEBUG_MAX_BREAKPOINTS];

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
    // Find the bytecode offset of the given line in the code.
    PyCodeObject* codeObject = (PyCodeObject*) obj;
    int lineStart = findLineStart(codeObject, line, NULL);
    if (lineStart == -1) {
        PyErr_SetString(BytecodeManipulationError, "Failed to locate the given line in the code.");
        return NULL;
    }
    // Extend the names tuple with names required to call the breakpoint callback.
    PyObject* namesToAppend[] = {cloudebugModuleName, cloudebugBreakpointCallbackName};
    int codeNamesIndices[2];
    PyObject* newNames = appendTuple(codeObject->co_names, 2, namesToAppend, codeNamesIndices);
    if (newNames == NULL) {
        PyErr_SetString(BytecodeManipulationError, "Failed to modify co_names.");
        return NULL;
    }
    codeObject->co_names = newNames;
    // Extend the constants tuple with the constants used in the injected bytecode.
    PyObject* constsToAppend[] = {PyLong_FromLong(id)};
    int codeConstsIndices[2];
    PyObject* newConsts = appendTuple(codeObject->co_consts, 1, constsToAppend, codeConstsIndices);
    if (newConsts == NULL) {
        PyErr_SetString(BytecodeManipulationError, "Failed to modify co_consts.");
        return NULL;
    }
    codeObject->co_consts = newConsts;
    // Inject the breakpoint callback.
    char* codeBytesBuffer = PyBytes_AsString(codeObject->co_code);
    Py_ssize_t codeSize = PyBytes_Size(codeObject->co_code);
    // JUMP_ABSOLUTE and its target
    unsigned int jumpTarget = codeSize;
    unsigned int jumpInstructionSize = jabsLength(jumpTarget / 2);
    unsigned int currentInstructionSize = effectiveInstructionLength(codeBytesBuffer + lineStart);
    unsigned int bytesToCopy = (jumpInstructionSize > currentInstructionSize) ? jumpInstructionSize : currentInstructionSize;
    cloudebugOriginalInstructionSize[id] = bytesToCopy;
    memcpy(cloudebugOriginalInstructions + id * 8, codeBytesBuffer + lineStart, bytesToCopy);
    writeAbsoluteJump(codeBytesBuffer + lineStart, jumpTarget);
    if (currentInstructionSize > jumpInstructionSize) {
        for (unsigned int i = lineStart + jumpInstructionSize; i < lineStart + currentInstructionSize; i += 2) {
            codeBytesBuffer[i] = NOP;
        }
    }
    const char breakpointCallInstructions[] = {
        char(LOAD_GLOBAL),
        char(codeNamesIndices[0]),  // global name index
        char(LOAD_METHOD),
        char(codeNamesIndices[1]),  // method name index
        char(LOAD_CONST),
        char(codeConstsIndices[0]), // constant index to load (breakpoint ID)
        char(CALL_METHOD),
        1,                          // number of positional arguments
        POP_TOP,
        0                           // (unused)
    };
    unsigned int injectedInstructionSize = sizeof(breakpointCallInstructions) + bytesToCopy + jabsLength(lineStart);
    char* instructions = (char*) malloc(injectedInstructionSize);
    memcpy(instructions, breakpointCallInstructions, sizeof(breakpointCallInstructions));
    memcpy(instructions + sizeof(breakpointCallInstructions), cloudebugOriginalInstructions + id * 8, bytesToCopy);
    writeAbsoluteJump(instructions + sizeof(breakpointCallInstructions) + bytesToCopy, lineStart + 2);
    PyObject* newCode = insertBytesAt(codeObject->co_code, injectedInstructionSize, instructions, codeSize);
    if (newCode == NULL) {
        PyErr_SetString(BytecodeManipulationError, "Failed to modify co_code.");
        return NULL;
    }
    Py_DECREF(codeObject->co_code);
    codeObject->co_code = newCode;
    // Increment the stack size to make way for 1 method and its argument.
    codeObject->co_stacksize += 256;
    // TODO: Extend the line table.
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
    // Find the bytecode offset of the given line in the code.
    PyCodeObject* codeObject = (PyCodeObject*) obj;
    int lineStart = findLineStart(codeObject, line, NULL);
    if (lineStart == -1) {
        PyErr_SetString(BytecodeManipulationError, "Failed to locate the given line in the code.");
        return NULL;
    }
    // Return the original bytecode.
    char* codeBytesBuffer = PyBytes_AsString(codeObject->co_code);
    unsigned int bytesToCopy = cloudebugOriginalInstructionSize[id];
    memcpy(codeBytesBuffer + lineStart, cloudebugOriginalInstructions + id * 8, bytesToCopy);
    // Decrement the stack size that was incremented previously.
    codeObject->co_stacksize -= 256;
    // TODO: Remove the appended bytecode.
    // TODO: Remove the injected co_consts and co_names...
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
        PyObject_Print(Py_None, stdout, 0);
        return NULL;
    }
    BytecodeManipulationError = PyErr_NewException("cloudebug_helper.error", NULL, NULL);
    cloudebugModuleName = PyUnicode_FromString("cloudebug");
    cloudebugBreakpointCallbackName = PyUnicode_FromString("breakpoint_callback");
    Py_XINCREF(BytecodeManipulationError);
    if (PyModule_AddObject(m, "error", BytecodeManipulationError) < 0) {
        Py_XDECREF(BytecodeManipulationError);
        Py_CLEAR(BytecodeManipulationError);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
