#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FH_MAX_ID 8192
#define FH_SELF_SWITCHES 4   // A, B, C, D

typedef struct {
    uint8_t switches[FH_MAX_ID + 1];
    int32_t variables[FH_MAX_ID + 1];
    uint8_t selfSwitches[FH_MAX_ID + 1][FH_SELF_SWITCHES];
} GameState;

void runtimeInit(GameState *state);

bool runtimeGetSwitch(const GameState *state, uint16_t id);
void runtimeSetSwitch(GameState *state, uint16_t id, bool value);

// Self-switches are per-event, per-letter (0=A 1=B 2=C 3=D).
bool runtimeGetSelfSwitch(const GameState *state, uint16_t eventId, int letter);
void runtimeSetSelfSwitch(GameState *state, uint16_t eventId, int letter, bool value);

int32_t runtimeGetVariable(const GameState *state, uint16_t id);
void runtimeSetVariable(GameState *state, uint16_t id, int32_t value);

// Executes RPG Maker commands 121 and 122.
bool runtimeExecuteCommand(
    GameState *state,
    uint16_t commandCode,
    const int32_t *params,
    size_t paramCount
);

#endif