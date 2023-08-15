#include "manipulation.hpp"
#include "opcode.h"
#include "pyutil.hpp"
#include "ext.hpp"
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

enum InstructionType: uint8_t {
    ABS_JUMP_INSTRUCTION,
    REL_JUMP_INSTRUCTION,
    YIELD_INSTRUCTION,
    OTHER_INSTRUCTION
};

struct Instruction {
    uint8_t opcode;
    uint32_t arg;
    uint8_t size;
    Instruction() = default;
    Instruction(uint8_t opcode) : Instruction(opcode, 0) {}
    Instruction(uint8_t opcode, uint32_t arg) : opcode(opcode), arg(arg), size(
        (arg & 0xFF000000) ?
            4 :
            (arg & 0xFF0000) ?
                3 :
                (arg & 0xFF00) ?
                    2 :
                    1
    ) {}
    Instruction(uint8_t opcode, uint32_t arg, uint8_t size) : opcode(opcode), arg(arg), size(size) {}
};

struct Insertion {
    uint32_t offset;
    int32_t size;
    Insertion(uint32_t offset, int32_t size) : offset(offset), size(size) {}
};

struct Breakpoint {
    bool isActive;
    int32_t injectionSize;
};

static std::unordered_map<int, Breakpoint> breakpoints;

static InstructionType getInstructionType(const Instruction& instruction) {
    switch (instruction.opcode) {
        case YIELD_FROM:
        case YIELD_VALUE:
            return YIELD_INSTRUCTION;
        // From dis.hasjabs.
        case JUMP_IF_FALSE_OR_POP:
        case JUMP_IF_TRUE_OR_POP:
        case JUMP_ABSOLUTE:
        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE:
        case JUMP_IF_NOT_EXC_MATCH:
            return ABS_JUMP_INSTRUCTION;
        // From dis.hasrel.
        case FOR_ITER:
        case JUMP_FORWARD:
        case SETUP_FINALLY:
        case SETUP_WITH:
        case SETUP_ASYNC_WITH:
            return REL_JUMP_INSTRUCTION;
        default:
            return OTHER_INSTRUCTION;
    }
}

static uint32_t getInstructionsSize(const std::vector<Instruction>& instructions) {
    uint32_t size = 0;
    for (auto& instruction : instructions) {
        size += instruction.size;
    }
    return size;
}

static bool insertInstructionsAtOffset(std::vector<Instruction>& instructions,
    const std::vector<Instruction>& newInstructions, uint32_t offset) {
    uint32_t newInstructionsOffset = 0;
    for (auto it = instructions.begin(); it != instructions.end(); ++it) {
        if (newInstructionsOffset == offset) {
            instructions.insert(it, newInstructions.begin(), newInstructions.end());
            return true;
        }
        newInstructionsOffset += it->size * 2;
    }
    return false;
}

static bool removeInstructionsAtOffset(std::vector<Instruction>& instructions, uint32_t offset,
    uint32_t numInstructions) {
    uint32_t instructionsOffset = 0;
    for (auto it = instructions.begin(); it != instructions.end(); ++it) {
        if (instructionsOffset == offset) {
            instructions.erase(it, it + numInstructions);
            return true;
        }
        instructionsOffset += it->size * 2;
    }
    return false;
}

static uint32_t getBranchTarget(Instruction& instruction, uint32_t offset) {
    switch (getInstructionType(instruction)) {
        case REL_JUMP_INSTRUCTION:
            return offset + (instruction.size + instruction.arg) * 2;
        case ABS_JUMP_INSTRUCTION:
            return instruction.arg * 2;
        default:
            throw std::runtime_error("getBranchTarget did not receive a branch instruction.");
    }
}

/**
 * @see https://devguide.python.org/internals/interpreter/
 */
static Instruction readInstruction(char*& bytecodeIterator) {
    _Py_CODEUNIT word;
    uint8_t opcode;
    uint32_t oparg = 0;
    uint8_t size = 0;
    do {
        word = *((_Py_CODEUNIT*) bytecodeIterator);
        opcode = _Py_OPCODE(word);
        oparg = (oparg << 8) | _Py_OPARG(word);
        ++size;
        bytecodeIterator += 2;
    } while (opcode == EXTENDED_ARG);
    return {opcode, oparg, size};
}

static std::vector<Instruction> readInstructions(PyCodeObject* code) {
    char* iterator;
    Py_ssize_t codeSize;
    PyBytes_AsStringAndSize(code->co_code, &iterator, &codeSize);
    char* endIterator = iterator + codeSize;
    std::vector<Instruction> instructions;
    while (iterator != endIterator) {
        instructions.push_back(readInstruction(iterator));
    }
    return instructions;
}

static void writeInstruction(char*& bytecodeIterator, const Instruction& instruction) {
    for (uint32_t i = instruction.size - 1; i > 0; --i) {
        (*bytecodeIterator++) = EXTENDED_ARG;
        uint32_t maskShift = i * 8;
        uint32_t mask = 0xFF << maskShift;
        (*bytecodeIterator++) = (instruction.arg & mask) >> maskShift;
    }
    (*bytecodeIterator++) = instruction.opcode;
    (*bytecodeIterator++) = instruction.arg & 0xFF;
}

static void writeInstructions(PyObject* bytecode, const std::vector<Instruction>& instructions) {
    char* iterator = PyBytes_AsString(bytecode);
    for (auto& instruction : instructions) {
        writeInstruction(iterator, instruction);
    }
}

static void performInsertion(std::vector<Instruction>& instructions, std::vector<Insertion>& insertions,
    uint32_t offset) {
    for (size_t insertionIndex = 0; insertionIndex < insertions.size(); ++insertionIndex) {
        const auto insertion = insertions[insertionIndex];
        for (size_t updateIndex = insertionIndex + 1; updateIndex < insertions.size(); ++updateIndex) {
            if (insertions[updateIndex].offset >= insertion.offset) {
                insertions[updateIndex].size += insertion.size;
            }
        }
        uint32_t currentOffset = 0;
        for (auto& instruction : instructions) {
            auto instructionType = getInstructionType(instruction);
            uint32_t target;
            // We calculate this in advance because the instruction size can change.
            uint32_t nextJump = instruction.size * 2;
            Instruction newInstruction;
            switch (instructionType) {
                case ABS_JUMP_INSTRUCTION:
                case REL_JUMP_INSTRUCTION:
                    target = getBranchTarget(instruction, currentOffset);
                    if (
                        target <= insertion.offset ||
                        (
                            instructionType == REL_JUMP_INSTRUCTION &&
                            currentOffset >= insertion.offset
                        )
                    ) {
                        // There is nothing to update.
                        break;
                    }
                    instruction.arg += insertion.size;
                    newInstruction = Instruction(instruction.opcode, instruction.arg);
                    if (instruction.size < newInstruction.size) {
                        // The instruction size has increased, so we need to create a new insertion.
                        insertions.push_back({
                            currentOffset,
                            newInstruction.size - instruction.size
                        });
                        instruction.size = newInstruction.size;
                    }
                    break;
                case YIELD_INSTRUCTION:
                    throw std::runtime_error("Breakpoints in generator functions are not supported.");
                default:
                    // No action.
                    break;
            }
            currentOffset += nextJump;
        }
    }
}

static void adjustLineTable(PyCodeObject* code, std::vector<Insertion>& insertions) {
    for (auto& insertion : insertions) {
        char* entry = findLineEntry(code, insertion.offset);
        *entry += insertion.size * 2;
    }
}

static void replaceBytecode(PyCodeObject* code, const std::vector<Instruction>& instructions) {
    uint32_t numInstructions = getInstructionsSize(instructions);
    PyObject* newCode = PyBytes_FromStringAndSize(NULL, numInstructions * 2);
    writeInstructions(newCode, instructions);
    Py_DECREF(code->co_code);
    code->co_code = newCode;
}

void addBreakpoint(PyCodeObject* code, int line, int breakpointId) {
    auto& breakpoint = breakpoints[breakpointId];
    if (breakpoint.isActive) {
        throw std::runtime_error("Breakpoint is already active.");
    }
    breakpoint.isActive = true;
    // Find the bytecode offset of the given line in the code.
    uint32_t offset = findLineStart(code, line);
    // Extend the names tuple with names required to call the breakpoint callback.
    PyObject* namesToAppend[] = {
        cloudebugModuleName,
        cloudebugBreakpointCallbackName
    };
    int codeNamesIndices[2];
    PyObject* newNames = appendTuple(code->co_names, 2, namesToAppend, codeNamesIndices);
    code->co_names = newNames;
    // Extend the constants tuple with the constants used in the injected bytecode.
    PyObject* constsToAppend[] = {PyLong_FromLong(breakpointId)};
    int codeConstsIndices[2];
    PyObject* newConsts = appendTuple(code->co_consts, 1, constsToAppend, codeConstsIndices);
    code->co_consts = newConsts;
    // Inject the breakpoint callback.
    std::vector<Instruction> instructions = readInstructions(code);
    std::vector<Instruction> newInstructions {
        // Stack: ...
        {LOAD_GLOBAL, uint32_t(codeNamesIndices[0])},
        // Stack: ... [module cloudebug]
        {LOAD_METHOD, uint32_t(codeNamesIndices[1])},
        // Stack: ... [NULL] [function breakpoint]
        {LOAD_CONST, uint32_t(codeConstsIndices[0])},
        // Stack: ... [NULL] [function breakpoint] [int breakpointId]
        {CALL_METHOD, 1},
        // Stack: ... [None]
        {POP_TOP}
        // Stack: ...
    };
    if (!insertInstructionsAtOffset(instructions, newInstructions, offset)) {
        throw std::runtime_error("Failed to find an insertion point for new instructions.");
    }
    breakpoint.injectionSize = int32_t(getInstructionsSize(newInstructions));
    std::vector<Insertion> insertions {
        {offset, breakpoint.injectionSize}
    };
    performInsertion(instructions, insertions, offset);
    replaceBytecode(code, instructions);
    // Increment the stack size to make way for added elements.
    code->co_stacksize += 3;
    // Extend the line table to account for added instructions.
    adjustLineTable(code, insertions);
}

void removeBreakpoint(PyCodeObject* code, int line, int breakpointId) {
    auto& breakpoint = breakpoints[breakpointId];
    if (!breakpoint.isActive) {
        throw std::runtime_error("Breakpoint is not active.");
    }
    breakpoint.isActive = false;
    // Find the bytecode offset of the given line in the code.
    uint32_t offset = findLineStart(code, line);
    // Return the original bytecode.
    std::vector<Instruction> instructions = readInstructions(code);
    if (!removeInstructionsAtOffset(instructions, offset, 5)) {
        throw std::runtime_error("Failed to remove injected instructions.");
    }
    std::vector<Insertion> removals {
        {offset, -breakpoint.injectionSize}
    };
    performInsertion(instructions, removals, offset);
    replaceBytecode(code, instructions);
    // Decrement the stack size that was incremented previously.
    code->co_stacksize -= 3;
    // Adjust the line table to account for removed instructions.
    adjustLineTable(code, removals);
    // TODO: Remove the injected co_consts and co_names...
}
