#include "manipulation.hpp"
#include <cmath>
#include "opcode.h"
#include "pyutil.hpp"
#include "ext.hpp"
#include <stdexcept>

// TODO: Use a proper hashmap
#define CLOUDEBUG_MAX_BREAKPOINTS 800
char cloudebugOriginalInstructions[CLOUDEBUG_MAX_BREAKPOINTS * 16];
unsigned int cloudebugOriginalInstructionSize[CLOUDEBUG_MAX_BREAKPOINTS];

static PyObject* insertBytesAt(PyObject* bytes, unsigned count, const char* newContent, unsigned offset) {
    if (!PyBytes_Check(bytes)) {
        throw std::runtime_error("Object passed to insertBytesAt is not a bytes object.");
    }
    Py_ssize_t oldBytesSize = PyBytes_Size(bytes);
    Py_ssize_t newBytesSize = oldBytesSize + count;
    if (offset > oldBytesSize) {
        throw std::runtime_error("Invalid offset passed to insertBytesAt.");
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

void addBreakpoint(PyCodeObject* code, int line, int breakpointId) {
    // Find the bytecode offset of the given line in the code.
    int lineStart = findLineStart(code, line, NULL);
    // Extend the names tuple with names required to call the breakpoint callback.
    PyObject* namesToAppend[] = {cloudebugModuleName, cloudebugBreakpointCallbackName};
    int codeNamesIndices[2];
    PyObject* newNames = appendTuple(code->co_names, 2, namesToAppend, codeNamesIndices);
    code->co_names = newNames;
    // Extend the constants tuple with the constants used in the injected bytecode.
    PyObject* constsToAppend[] = {PyLong_FromLong(breakpointId)};
    int codeConstsIndices[2];
    PyObject* newConsts = appendTuple(code->co_consts, 1, constsToAppend, codeConstsIndices);
    code->co_consts = newConsts;
    // Inject the breakpoint callback.
    char* codeBytesBuffer = PyBytes_AsString(code->co_code);
    Py_ssize_t codeSize = PyBytes_Size(code->co_code);
    // JUMP_ABSOLUTE and its target
    unsigned int jumpTarget = codeSize;
    unsigned int jumpInstructionSize = jabsLength(jumpTarget / 2);
    unsigned int currentInstructionSize = effectiveInstructionLength(codeBytesBuffer + lineStart);
    unsigned int bytesToCopy = (jumpInstructionSize > currentInstructionSize) ? jumpInstructionSize : currentInstructionSize;
    cloudebugOriginalInstructionSize[breakpointId] = bytesToCopy;
    memcpy(cloudebugOriginalInstructions + breakpointId * 8, codeBytesBuffer + lineStart, bytesToCopy);
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
    memcpy(instructions + sizeof(breakpointCallInstructions), cloudebugOriginalInstructions + breakpointId * 8, bytesToCopy);
    writeAbsoluteJump(instructions + sizeof(breakpointCallInstructions) + bytesToCopy, lineStart + 2);
    PyObject* newCode = insertBytesAt(code->co_code, injectedInstructionSize, instructions, codeSize);
    Py_DECREF(code->co_code);
    code->co_code = newCode;
    // Increment the stack size to make way for 1 method and its argument.
    code->co_stacksize += 256;
    // TODO: Extend the line table.
}

void removeBreakpoint(PyCodeObject* code, int line, int breakpointId) {
    // Find the bytecode offset of the given line in the code.
    int lineStart = findLineStart(code, line, NULL);
    // Return the original bytecode.
    char* codeBytesBuffer = PyBytes_AsString(code->co_code);
    unsigned int bytesToCopy = cloudebugOriginalInstructionSize[breakpointId];
    memcpy(codeBytesBuffer + lineStart, cloudebugOriginalInstructions + breakpointId * 8, bytesToCopy);
    // Decrement the stack size that was incremented previously.
    code->co_stacksize -= 256;
    // TODO: Remove the appended bytecode.
    // TODO: Remove the injected co_consts and co_names...
}
