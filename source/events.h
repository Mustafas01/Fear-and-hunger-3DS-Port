
#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "runtime.h"

typedef struct {
    uint16_t mapId;
    uint32_t eventCount;
    uint32_t pageCount;
    uint32_t commandCount;
} EventFileInfo;

bool eventsValidate(const char *path, EventFileInfo *info);
bool eventsEventAt(const char *path, int x, int y, uint16_t *outEventId);
void eventsSetTrace(bool on);
void eventsDumpEventPositions(const char *path);
int eventsGetActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
);

bool eventsExecutePage(
    const char *path,
    uint16_t eventId,
    uint16_t pageIndex,
    GameState *state
);

bool eventsExecuteActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
);

// Runs isolated interpreter tests for IF, ELSE and nested branches.
bool eventsRunBranchSelfTests(void);

#endif

#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "runtime.h"

typedef struct {
    uint16_t mapId;
    uint32_t eventCount;
    uint32_t pageCount;
    uint32_t commandCount;
} EventFileInfo;

bool eventsValidate(const char *path, EventFileInfo *info);

int eventsGetActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
);

bool eventsExecutePage(
    const char *path,
    uint16_t eventId,
    uint16_t pageIndex,
    GameState *state
);

bool eventsExecuteActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
);

// Runs isolated interpreter tests for IF, ELSE and nested branches.
bool eventsRunBranchSelfTests(void);

#endif
