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
}
// key layout: map(15 bits) | event(15 bits) | letter(2 bits), +1 so 0 = empty
static uint32_t selfKey(uint16_t mapId, uint16_t eventId, int letter) {
    return (((uint32_t)(mapId & 0x7FFF) << 17) |
            ((uint32_t)(eventId & 0x7FFF) << 2) |
            (uint32_t)letter) + 1;
}

static int selfFind(const GameState *state, uint32_t key, bool *found) {
    uint32_t h = (key * 2654435761u) & (FH_SELF_SLOTS - 1);
    for (int n = 0; n < FH_SELF_SLOTS; n++) {
        uint32_t k = state->selfKeys[h];
        if (k == key) { *found = true; return (int)h; }
        if (k == 0)   { *found = false; return (int)h; }
        h = (h + 1) & (FH_SELF_SLOTS - 1);
    }
    *found = false;
    return -1; // table full
}

bool runtimeGetSelfSwitch(const GameState *state, uint16_t mapId,
                          uint16_t eventId, int letter) {
    if (!state || letter < 0 || letter >= FH_SELF_SWITCHES) return false;
    bool found;
    selfFind(state, selfKey(mapId, eventId, letter), &found);
    return found;
}

void runtimeSetSelfSwitch(GameState *state, uint16_t mapId,
                          uint16_t eventId, int letter, bool value) {
    if (!state || letter < 0 || letter >= FH_SELF_SWITCHES) return;
    uint32_t key = selfKey(mapId, eventId, letter);
    bool found;
    int slot = selfFind(state, key, &found);
    if (slot < 0) return;
    if (value) {
        state->selfKeys[slot] = key;
        return;
    }
    if (!found) return;
    // Delete with backward-shift so later probes still find their keys.
    int hole = slot;
    int j = slot;
    for (;;) {
        j = (j + 1) & (FH_SELF_SLOTS - 1);
        uint32_t k = state->selfKeys[j];
        if (k == 0) break;
        int home = (int)((k * 2654435761u) & (FH_SELF_SLOTS - 1));
        bool between = (hole <= j) ? (hole < home && home <= j)
                                   : (hole < home || home <= j);
        if (!between) {
            state->selfKeys[hole] = k;
            hole = j;
        }
    }
    state->selfKeys[hole] = 0;
}

int runtimeGetOwned(const GameState *state, DbKind kind, int id) {
    if (!state || kind > DB_ARMOR || id < 1 || id > FH_MAX_DB) return 0;
    return state->owned[kind][id];
}

void runtimeAddOwned(GameState *state, DbKind kind, int id, int delta) {
    if (!state || kind > DB_ARMOR || id < 1 || id > FH_MAX_DB) return;
    int v = state->owned[kind][id] + delta;
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    state->owned[kind][id] = (int16_t)v;
}

void runtimePartyAdd(GameState *state, int actorId) {
    if (!state || actorId < 1 || actorId > FH_MAX_ACTORS || state->inParty[actorId]) return;
    if (state->partyCount >= sizeof(state->partyOrder)) return;
    state->partyOrder[state->partyCount++] = (uint8_t)actorId;
    state->inParty[actorId] = 1;
}

void runtimePartyRemove(GameState *state, int actorId) {
    if (!state || actorId < 1 || actorId > FH_MAX_ACTORS || !state->inParty[actorId]) return;
    int j = 0;
    for (int i = 0; i < state->partyCount; i++)
        if (state->partyOrder[i] != actorId) state->partyOrder[j++] = state->partyOrder[i];
    state->partyCount = (uint8_t)j;
    state->inParty[actorId] = 0;
}

int runtimePartyLeader(const GameState *state) {
    return (state && state->partyCount) ? state->partyOrder[0] : 0;
}
