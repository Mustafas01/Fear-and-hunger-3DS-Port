#include "runtime.h"

#include <string.h>
#include <limits.h>

void runtimeInit(GameState *state) {
    if (!state) return;
    memset(state, 0, sizeof(GameState));
}

bool runtimeGetSwitch(const GameState *state, uint16_t id) {
    if (!state || id > FH_MAX_ID) return false;
    return state->switches[id] != 0;
}

void runtimeSetSwitch(GameState *state, uint16_t id, bool value) {
    if (!state || id > FH_MAX_ID) return;
    state->switches[id] = value ? 1 : 0;
}

int32_t runtimeGetVariable(const GameState *state, uint16_t id) {
    if (!state || id > FH_MAX_ID) return 0;
    return state->variables[id];
}

void runtimeSetVariable(GameState *state, uint16_t id, int32_t value) {
    if (!state || id > FH_MAX_ID) return;
    state->variables[id] = value;
}

static int32_t clampInt64(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}

static bool applyVariableOperation(
    GameState *state,
    uint16_t id,
    int operation,
    int32_t operand
) {
    int32_t current = runtimeGetVariable(state, id);
    int64_t result = current;

    switch (operation) {
        case 0: result = operand; break;           // Set
        case 1: result = (int64_t)current + operand; break;
        case 2: result = (int64_t)current - operand; break;
        case 3: result = (int64_t)current * operand; break;

        case 4:
            if (operand == 0) return false;
            result = current / operand;
            break;

        case 5:
            if (operand == 0) return false;
            result = current % operand;
            break;

        default:
            return false;
    }

    runtimeSetVariable(state, id, clampInt64(result));
    return true;
}

bool runtimeExecuteCommand(
    GameState *state,
    uint16_t commandCode,
    const int32_t *params,
    size_t paramCount
) {
    if (!state || !params) return false;

    // RPG Maker MV: Control Switches (121)
    // [start ID, end ID, operation]
    // operation: 0 = ON, 1 = OFF (Standard MV logic)
    if (commandCode == 121) {
        if (paramCount < 3) return false;

        int32_t start = params[0];
        int32_t end = params[1];
        int32_t operation = params[2];

        if (start < 1 || end < start ||
            end > FH_MAX_ID ||
            (operation != 0 && operation != 1)) {
            return false;
        }

        for (int32_t id = start; id <= end; id++) {
            runtimeSetSwitch(state, (uint16_t)id, operation == 0);
        }

        return true;
    }

    // RPG Maker MV: Control Variables (122)
    // [start ID, end ID, operation, operand type, operand]
    if (commandCode == 122) {
        if (paramCount < 5) return false;

        int32_t start = params[0];
        int32_t end = params[1];
        int32_t operation = params[2];
        int32_t operandType = params[3];
        int32_t operandValue = params[4];

        if (start < 1 || end < start || end > FH_MAX_ID)
            return false;

        if (operation < 0 || operation > 5)
            return false;

        if (operandType != 0 && operandType != 1)
            return false;

        if (operandType == 1 &&
            (operandValue < 1 || operandValue > FH_MAX_ID)) {
            return false;
        }

        for (int32_t id = start; id <= end; id++) {
            int32_t operand = operandType == 0
                ? operandValue
                : runtimeGetVariable(state, (uint16_t)operandValue);

            if (!applyVariableOperation(
                    state,
                    (uint16_t)id,
                    operation,
                    operand)) {
                return false;
            }
        }

        return true;
    }

    // Unsupported command.
    return false;
}bool runtimeGetSelfSwitch(const GameState *state, uint16_t eventId, int letter) {
    if (!state || letter < 0 || letter >= FH_SELF_SWITCHES) return false;
    return state->selfSwitches[eventId][letter] != 0;
}

void runtimeSetSelfSwitch(GameState *state, uint16_t eventId, int letter, bool value) {
    if (!state || letter < 0 || letter >= FH_SELF_SWITCHES) return;
    state->selfSwitches[eventId][letter] = value ? 1 : 0;
}