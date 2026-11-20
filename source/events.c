// ---------------------------------------------------------------------------
// Fear & Hunger 3DS port - event system (events.c)
//
// File layout:
//   1. Binary reader helpers
//   2. Lightweight JSON parsers (params array + page conditions)
//   3. Command executor with conditional branches (111/411/412)
//   4. Interpreter self-tests
//   5. Public file API (validate / active page / execute)
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"

// Maximum numeric parameters stored per command.
#define FH_MAX_PARAMS 16

// Maximum JSON blob (params or conditions) read into a stack buffer.
#define FH_MAX_JSON 1024

// One parsed event command. Parameters are stored inline so a whole
// page needs a single allocation and self-tests can build command
// lists on the stack.
typedef struct {
    uint16_t code;
    int16_t indent;
    int32_t params[FH_MAX_PARAMS];
    size_t paramCount;
} EventCommand;

// ---------------------------------------------------------------------------
// Section 1: binary reader helpers
// ---------------------------------------------------------------------------

static bool readExact(FILE *fp, void *buf, size_t n) {
    return fread(buf, 1, n, fp) == n;
}

static uint16_t readU16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t readU32(const unsigned char *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool skipBytes(FILE *fp, uint32_t n) {
    return fseek(fp, (long)n, SEEK_CUR) == 0;
}

// ---------------------------------------------------------------------------
// Section 2: lightweight JSON parsing
//
// The compiled file stores command parameters as JSON arrays such as
// "[9,9,0,0,1]" and page conditions as JSON objects such as
// {"switch1Valid":true,"switch1Id":1,...}. A full JSON library is far
// too heavy for the 3DS, so these parsers handle exactly the subset
// the compiler emits.
// ---------------------------------------------------------------------------

static const char *skipWs(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

// Skips a balanced JSON array or object. p must point at '[' or '{'.
// Returns a pointer just past the matching closing bracket, or NULL
// if the input ends prematurely.
static const char *skipBracketed(const char *p) {
    int depth = 1;

    p++;

    while (depth > 0 && *p != '\0') {
        if (*p == '"') {
            p++;

            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0')
                    p++;
                p++;
            }

            if (*p != '"')
                return NULL;

            p++;
            continue;
        }

        if (*p == '[' || *p == '{')
            depth++;
        else if (*p == ']' || *p == '}')
            depth--;

        p++;
    }

    return depth == 0 ? p : NULL;
}

// Reads a JSON string literal. p must point at the opening quote. The
// contents (unescaped, truncated) are copied to buf; buf may be NULL
// to only skip. Returns a pointer just past the closing quote, or
// NULL on error.
static const char *readJsonString(const char *p, char *buf, size_t bufSize) {
    if (*p != '"')
        return NULL;

    p++;

    size_t n = 0;

    while (*p != '\0' && *p != '"') {
        char c = *p;

        if (c == '\\' && p[1] != '\0') {
            p++;
            c = *p;
        }

        if (buf && n + 1 < bufSize)
            buf[n++] = c;

        p++;
    }

    if (*p != '"')
        return NULL;

    if (buf && bufSize > 0)
        buf[n] = '\0';

    return p + 1;
}

// Reads a JSON number (integer, negative allowed, fractional part
// tolerated and discarded). Returns a pointer past the number, or
// NULL if no digits were found.
static const char *readJsonNumber(const char *p, int32_t *out) {
    int negative = 0;
    int32_t value = 0;
    bool anyDigits = false;

    if (*p == '-') {
        negative = 1;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        anyDigits = true;
        p++;
    }

    if (*p == '.') {
        p++;

        while (*p >= '0' && *p <= '9')
            p++;
    }

    if (!anyDigits)
        return NULL;

    if (out)
        *out = negative ? -value : value;

    return p;
}

// Parses a JSON array such as "[9,9,0,0,1]" into out[]. Numeric
// elements are stored; strings (face names etc.), true/false/null
// literals and nested arrays/objects are skipped without taking a
// slot. Returns the number of values stored, or -1 if the input is
// malformed or holds more than maxOut numeric elements.
static int parseJsonArray(const char *json, int32_t *out, size_t maxOut) {
    if (!json || !out || maxOut == 0)
        return -1;

    const char *p = skipWs(json);

    if (*p != '[')
        return -1;

    p++;

    size_t count = 0;

    for (;;) {
        p = skipWs(p);

        while (*p == ',')
            p++;

        p = skipWs(p);

        if (*p == '\0')
            return -1;              // unterminated array

        if (*p == ']')
            return (int)count;      // done

        if (*p == '"') {
            p = readJsonString(p, NULL, 0);   // skip string element

            if (!p)
                return -1;

            continue;
        }

        if (*p == '[' || *p == '{') {
            p = skipBracketed(p);             // skip nested value

            if (!p)
                return -1;

            continue;
        }

        if (*p == 't' || *p == 'f' || *p == 'n') {
            while (*p >= 'a' && *p <= 'z')    // true/false/null
                p++;

            continue;
        }

        int32_t value = 0;

        p = readJsonNumber(p, &value);

        if (!p)
            return -1;

        if (count >= maxOut)
            return -1;              // too many numeric parameters

        out[count] = value;
        count++;
    }
}

// Parsed page conditions (RPG Maker MV event page "conditions").
typedef struct {
    bool switch1Valid;
    uint16_t switch1Id;
    bool switch2Valid;
    uint16_t switch2Id;
    bool variableValid;
    uint16_t variableId;
    int32_t variableValue;
    bool selfSwitchValid;
    char selfSwitchCh;
    bool itemValid;
    uint16_t itemId;
    bool actorValid;
    uint16_t actorId;
} PageConditions;

// Parses a page conditions JSON object. Unknown keys are ignored.
static bool parsePageConditions(const char *json, PageConditions *out) {
    memset(out, 0, sizeof(*out));

    const char *p = skipWs(json);

    if (*p != '{')
        return false;

    p++;

    for (;;) {
        p = skipWs(p);

        while (*p == ',')
            p++;

        p = skipWs(p);

        if (*p == '\0')
            return false;           // unterminated object

        if (*p == '}')
            return true;            // done

        char key[32];

        p = readJsonString(p, key, sizeof(key));

        if (!p)
            return false;

        p = skipWs(p);

        if (*p != ':')
            return false;

        p = skipWs(p + 1);

        if (*p == '"') {
            char str[8];

            p = readJsonString(p, str, sizeof(str));

            if (!p)
                return false;

            if (strcmp(key, "selfSwitchCh") == 0 &&
                str[0] >= 'A' && str[0] <= 'D') {
                out->selfSwitchCh = str[0];
            }
        } else if (*p == 't' || *p == 'f') {
            bool value = (*p == 't');

            while (*p >= 'a' && *p <= 'z')
                p++;

            if (strcmp(key, "switch1Valid") == 0)
                out->switch1Valid = value;
            else if (strcmp(key, "switch2Valid") == 0)
                out->switch2Valid = value;
            else if (strcmp(key, "variableValid") == 0)
                out->variableValid = value;
            else if (strcmp(key, "selfSwitchValid") == 0)
                out->selfSwitchValid = value;
            else if (strcmp(key, "itemValid") == 0)
                out->itemValid = value;
            else if (strcmp(key, "actorValid") == 0)
                out->actorValid = value;
        } else if (*p == 'n') {
            while (*p >= 'a' && *p <= 'z')   // null
                p++;
        } else if (*p == '[' || *p == '{') {
            p = skipBracketed(p);

            if (!p)
                return false;
        } else {
            int32_t value = 0;

            p = readJsonNumber(p, &value);

            if (!p)
                return false;

            if (strcmp(key, "switch1Id") == 0)
                out->switch1Id = (uint16_t)value;
            else if (strcmp(key, "switch2Id") == 0)
                out->switch2Id = (uint16_t)value;
            else if (strcmp(key, "variableId") == 0)
                out->variableId = (uint16_t)value;
            else if (strcmp(key, "variableValue") == 0)
                out->variableValue = value;
            else if (strcmp(key, "itemId") == 0)
                out->itemId = (uint16_t)value;
            else if (strcmp(key, "actorId") == 0)
                out->actorId = (uint16_t)value;
        }
    }
}

// Evaluates parsed page conditions against the game state. A page is
// active when every enabled condition passes (MV combines them with
// logical AND). Conditions we cannot model yet (self switch, item,
// actor) make the page unreachable for now.
static bool evaluatePageConditions(const char *json, GameState *state) {
    PageConditions cond;

    if (!parsePageConditions(json, &cond))
        return false;

    if (cond.switch1Valid &&
        !runtimeGetSwitch(state, cond.switch1Id))
        return false;

    if (cond.switch2Valid &&
        !runtimeGetSwitch(state, cond.switch2Id))
        return false;

    if (cond.variableValid &&
        runtimeGetVariable(state, cond.variableId) !=
            cond.variableValue)
        return false;

    if (cond.selfSwitchValid || cond.itemValid || cond.actorValid)
        return false;

    return true;
}
// ---------------------------------------------------------------------------
// Section 3: conditional branches (111 / 411 / 412) and the executor
// ---------------------------------------------------------------------------

// RPG Maker MV encodes branches as a flat command list with indent
// levels. An If (111) at indent N has its body at N+1, its Else (411)
// at N and its End (412) at N:
//
//   111 N   If
//     ...   then-branch (indent N+1)
//   411 N   Else      (optional)
//     ...   else-branch (indent N+1)
//   412 N   End
//
// Matching by (code, indent) is unambiguous because anything nested
// inside a body sits at a deeper indent. Execution rules:
//   - Condition true:  run the then-branch, then skip the else-branch.
//   - Condition false: skip to the Else and run the else-branch, or
//     skip to the End when no Else exists.
//   - Nested Ifs inside a skipped region never execute.

// Maximum branch nesting depth we track. 32 is far beyond anything
// the RPG Maker editor produces in practice.
#define FH_MAX_INDENT 32

// Evaluates the condition of a Conditional Branch (111). Types we
// cannot model yet (self switch, timer, actor, script, ...) evaluate
// to false, which safely skips their then-branches.
static bool evaluateBranchCondition(
    const EventCommand *cmd,
    GameState *state
) {
    if (cmd->paramCount < 1)
        return false;

    switch (cmd->params[0]) {
        case 0: { // Switch: [0, switchId, 0=ON / 1=OFF]
            if (cmd->paramCount < 3)
                return false;

            bool wantOn = (cmd->params[2] == 0);

            return runtimeGetSwitch(
                state, (uint16_t)cmd->params[1]
            ) == wantOn;
        }

        case 1: { // Variable: [1, varId, 0=const/1=var, operand, op]
            if (cmd->paramCount < 5)
                return false;

            int32_t left = runtimeGetVariable(
                state, (uint16_t)cmd->params[1]
            );

            int32_t right = (cmd->params[2] == 0)
                ? cmd->params[3]
                : runtimeGetVariable(
                    state, (uint16_t)cmd->params[3]
                );

            switch (cmd->params[4]) {
                case 0: return left == right;   // =
                case 1: return left >= right;  // >=
                case 2: return left <= right;  // <=
                case 3: return left >  right;  // >
                case 4: return left <  right;  // <
                case 5: return left != right;  // !=
                default: return false;
            }
        }

        default:
            return false;
    }
}

// From the If at ifIndex, finds the matching Else (411) or End (412)
// at the same indent. Returns the index, or commandCount when the
// branch is unterminated (malformed data).
static size_t findBranchTarget(
    const EventCommand *commands,
    size_t commandCount,
    size_t ifIndex
) {
    int16_t indent = commands[ifIndex].indent;

    for (size_t i = ifIndex + 1; i < commandCount; i++) {
        if (commands[i].indent == indent &&
            (commands[i].code == 411 || commands[i].code == 412)) {
            return i;
        }
    }

    return commandCount;
}

// From the Else at elseIndex, finds the matching End (412) at the
// same indent. Returns the index, or commandCount when unterminated.
static size_t findBranchEnd(
    const EventCommand *commands,
    size_t commandCount,
    size_t elseIndex
) {
    int16_t indent = commands[elseIndex].indent;

    for (size_t i = elseIndex + 1; i < commandCount; i++) {
        if (commands[i].indent == indent &&
            commands[i].code == 412) {
            return i;
        }
    }

    return commandCount;
}

// Runs one page's command list. Returns false only on structural
// errors (bad indent depth); unsupported commands are skipped so a
// single unimplemented feature cannot kill a whole event page.
// When enabled, the executor prints each command code as it runs.
static bool s_trace = false;

void eventsSetTrace(bool on) {
    s_trace = on;
}
static bool executeCommandList(
    
    const EventCommand *commands,
    size_t commandCount,
    GameState *state
) {
    if (!commands || !state)
        return false;

    // branchTrue[indent] remembers the result of the most recent If
    // executed at that indent, so the Else handler knows whether to
    // fall through or skip. An inner If always writes a deeper slot,
    // so an outer If's result is never clobbered.
    bool branchTrue[FH_MAX_INDENT];
    memset(branchTrue, 0, sizeof(branchTrue));

    for (size_t i = 0; i < commandCount; i++) {
        const EventCommand *cmd = &commands[i];
            if (s_trace)
            printf("  cmd %u indent %d params %zu\n",
                cmd->code, cmd->indent, cmd->paramCount
            );
        switch (cmd->code) {
            case 0: // End of list: stop executing this page
                return true;

            case 111: { // Conditional Branch
                int16_t indent = cmd->indent;

                if (indent < 0 || indent >= FH_MAX_INDENT)
                    return false; // malformed nesting depth

                bool condition =
                    evaluateBranchCondition(cmd, state);

                branchTrue[indent] = condition;

                if (!condition) {
                    // Skip the then-branch. Land on the matching
                    // Else (fall into the else-branch) or End; the
                    // loop increment steps past it.
                    size_t target = findBranchTarget(
                        commands, commandCount, i
                    );

                    if (target >= commandCount)
                        return true; // unterminated: stop cleanly

                    i = target;
                }

                break;
            }

            case 411: { // Else
                int16_t indent = cmd->indent;

                if (indent < 0 || indent >= FH_MAX_INDENT)
                    return false;

                if (branchTrue[indent]) {
                    // Condition was true: skip the else-branch and
                    // land on the matching End.
                    size_t end = findBranchEnd(
                        commands, commandCount, i
                    );

                    if (end >= commandCount)
                        return true;

                    i = end;
                }

                // Otherwise fall through into the else-branch.
                break;
            }

            case 412: // End of branch: nothing to do
                break;

            case 121: // Control Switches
            case 122: // Control Variables
                runtimeExecuteCommand(
                    state, cmd->code, cmd->params, cmd->paramCount
                );
                break;

            default:
                // Not implemented yet (text, movement, sound, ...):
                // ignore and keep going.
                break;
        }
    }

    return true;
}
// ---------------------------------------------------------------------------
// Section 4: interpreter self-tests
//
// eventsRunBranchSelfTests builds small command lists on the stack and
// runs them through the executor, verifying branch behavior without
// touching the compiled event file.
// ---------------------------------------------------------------------------

// Shared by all tests: GameState is ~40 KB, far too large for the
// 3DS thread stack (24 KB), so it lives in .bss like the one in main.c.
static GameState s_testState;

static void makeCommand(
    EventCommand *cmd,
    uint16_t code,
    int16_t indent,
    const int32_t *params,
    size_t paramCount
) {
    cmd->code = code;
    cmd->indent = indent;
    cmd->paramCount = paramCount < FH_MAX_PARAMS
        ? paramCount
        : FH_MAX_PARAMS;

    memset(cmd->params, 0, sizeof(cmd->params));

    if (params && cmd->paramCount > 0) {
        memcpy(
            cmd->params,
            params,
            cmd->paramCount * sizeof(int32_t)
        );
    }
}

// Control Variables: set varId to a constant.
static void makeSetVar(
    EventCommand *cmd,
    int16_t indent,
    int32_t varId,
    int32_t value
) {
    int32_t params[5] = { varId, varId, 0, 0, value };
    makeCommand(cmd, 122, indent, params, 5);
}

// Conditional Branch: variable equals a constant.
static void makeIfVarEq(
    EventCommand *cmd,
    int16_t indent,
    int32_t varId,
    int32_t value
) {
    int32_t params[5] = { 1, varId, 0, value, 0 };
    makeCommand(cmd, 111, indent, params, 5);
}

// Conditional Branch: variable vs constant with any operator.
static void makeIfVarCmp(
    EventCommand *cmd,
    int16_t indent,
    int32_t varId,
    int32_t operand,
    int32_t op
) {
    int32_t params[5] = { 1, varId, 0, operand, op };
    makeCommand(cmd, 111, indent, params, 5);
}

// Conditional Branch: switch is ON (0) or OFF (1).
static void makeIfSwitch(
    EventCommand *cmd,
    int16_t indent,
    int32_t switchId,
    int32_t onOff
) {
    int32_t params[3] = { 0, switchId, onOff };
    makeCommand(cmd, 111, indent, params, 3);
}

// 1. A true condition runs the then-branch.
static bool testIfTrueRunsThen(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 5);

    EventCommand list[4];
    makeIfVarEq(&list[0], 0, 1, 5);           // If var1 == 5 (true)
    makeSetVar(&list[1], 1, 2, 1);            //   var2 = 1
    makeCommand(&list[2], 412, 0, NULL, 0);  // End
    makeCommand(&list[3], 0, 0, NULL, 0);     // End of list

    if (!executeCommandList(list, 4, state))
        return false;

    return runtimeGetVariable(state, 2) == 1;
}

// 2. A false condition skips the then-branch (no Else present).
static bool testIfFalseSkipsThen(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 5);

    EventCommand list[4];
    makeIfVarEq(&list[0], 0, 1, 99);          // If var1 == 99 (false)
    makeSetVar(&list[1], 1, 2, 1);            //   var2 = 1 (must not run)
    makeCommand(&list[2], 412, 0, NULL, 0);  // End
    makeCommand(&list[3], 0, 0, NULL, 0);

    if (!executeCommandList(list, 4, state))
        return false;

    return runtimeGetVariable(state, 2) == 0;
}

// 3. A false condition runs the else-branch.
static bool testIfFalseRunsElse(void) {
    GameState *state = &s_testState;
    runtimeInit(state);

    EventCommand list[6];
    makeIfVarEq(&list[0], 0, 1, 99);          // If var1 == 99 (false)
    makeSetVar(&list[1], 1, 2, 1);            //   var2 = 1 (must not run)
    makeCommand(&list[2], 411, 0, NULL, 0);  // Else
    makeSetVar(&list[3], 1, 3, 1);            //   var3 = 1 (must run)
    makeCommand(&list[4], 412, 0, NULL, 0);  // End
    makeCommand(&list[5], 0, 0, NULL, 0);

    if (!executeCommandList(list, 6, state))
        return false;

    return runtimeGetVariable(state, 2) == 0 &&
           runtimeGetVariable(state, 3) == 1;
}

// 4. A true condition skips the else-branch.
static bool testIfTrueSkipsElse(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 5);

    EventCommand list[6];
    makeIfVarEq(&list[0], 0, 1, 5);           // If var1 == 5 (true)
    makeSetVar(&list[1], 1, 2, 1);            //   var2 = 1 (must run)
    makeCommand(&list[2], 411, 0, NULL, 0);   // Else
    makeSetVar(&list[3], 1, 3, 1);            //   var3 = 1 (must not run)
    makeCommand(&list[4], 412, 0, NULL, 0);  // End
    makeCommand(&list[5], 0, 0, NULL, 0);

    if (!executeCommandList(list, 6, state))
        return false;

    return runtimeGetVariable(state, 2) == 1 &&
           runtimeGetVariable(state, 3) == 0;
}

// 5. Switch conditions: ON and OFF forms.
static bool testSwitchConditions(void) {
    GameState *state = &s_testState;
    runtimeInit(state); // switch 3 starts OFF

    EventCommand list[7];
    makeIfSwitch(&list[0], 0, 3, 0);          // If switch3 ON (false)
    makeSetVar(&list[1], 1, 4, 1);            //   var4 = 1 (must not run)
    makeCommand(&list[2], 412, 0, NULL, 0);  // End
    makeIfSwitch(&list[3], 0, 3, 1);          // If switch3 OFF (true)
    makeSetVar(&list[4], 1, 5, 1);            //   var5 = 1 (must run)
    makeCommand(&list[5], 412, 0, NULL, 0);  // End
    makeCommand(&list[6], 0, 0, NULL, 0);     // End of list

    if (!executeCommandList(list, 7, state))
        return false;

    return runtimeGetVariable(state, 4) == 0 &&
           runtimeGetVariable(state, 5) == 1;
}

// 6. Nested branches: an inner If inside a skipped then-branch must
// not run even when its own condition is true; an inner If inside an
// executed region runs normally; execution continues after the End.
static bool testNestedBranches(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 5);

    EventCommand list[13];
    makeIfVarEq(&list[0], 0, 1, 99);          // If var1 == 99 (false)
    makeIfVarEq(&list[1], 1, 1, 5);           //   If var1 == 5 (true)
    makeSetVar(&list[2], 2, 2, 1);            //     var2 = 1 (must not run)
    makeCommand(&list[3], 412, 1, NULL, 0);  //   End
    makeCommand(&list[4], 412, 0, NULL, 0);  // End
    makeIfVarEq(&list[5], 0, 1, 5);           // If var1 == 5 (true)
    makeIfVarEq(&list[6], 1, 1, 5);           //   If var1 == 5 (true)
    makeSetVar(&list[7], 2, 3, 1);            //     var3 = 1 (must run)
    makeCommand(&list[8], 412, 1, NULL, 0);  //   End
    makeSetVar(&list[9], 1, 4, 1);            //   var4 = 1 (must run)
    makeCommand(&list[10], 412, 0, NULL, 0); // End
    makeSetVar(&list[11], 0, 5, 1);           // var5 = 1 (must run)
    makeCommand(&list[12], 0, 0, NULL, 0);    // End of list

    if (!executeCommandList(list, 13, state))
        return false;

    return runtimeGetVariable(state, 2) == 0 &&
           runtimeGetVariable(state, 3) == 1 &&
           runtimeGetVariable(state, 4) == 1 &&
           runtimeGetVariable(state, 5) == 1;
}

// 7. All six comparison operators (0==, 1>=, 2<=, 3>, 4<, 5!=)
// against both an equal and a smaller operand.
static bool testComparisonOperators(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 5);

    // Expected results comparing var1 (5) against 5 and 3.
    static const int32_t expectVs5[6] = { 1, 1, 1, 0, 0, 0 };
    static const int32_t expectVs3[6] = { 0, 1, 0, 1, 0, 1 };

    for (int op = 0; op < 6; op++) {
        for (int operand = 0; operand < 2; operand++) {
            int32_t cmpValue = operand ? 5 : 3;

            runtimeSetVariable(state, 2, 0);

            EventCommand list[4];
            makeIfVarCmp(&list[0], 0, 1, cmpValue, op);
            makeSetVar(&list[1], 1, 2, 1);
            makeCommand(&list[2], 412, 0, NULL, 0);
            makeCommand(&list[3], 0, 0, NULL, 0);

            if (!executeCommandList(list, 4, state))
                return false;

            int32_t expected = operand
                ? expectVs5[op]
                : expectVs3[op];

            if (runtimeGetVariable(state, 2) != expected)
                return false;
        }
    }

    return true;
}

// 8. Variable operand: comparing a variable against another variable
// (operand type 1, params[2] == 1).
static bool testVariableOperand(void) {
    GameState *state = &s_testState;
    runtimeInit(state);
    runtimeSetVariable(state, 1, 7);
    runtimeSetVariable(state, 2, 7);

    // If var1 == var2
    int32_t params[5] = { 1, 1, 1, 2, 0 };

    EventCommand list[4];
    makeCommand(&list[0], 111, 0, params, 5);
    makeSetVar(&list[1], 1, 3, 1);
    makeCommand(&list[2], 412, 0, NULL, 0);
    makeCommand(&list[3], 0, 0, NULL, 0);

    if (!executeCommandList(list, 4, state))
        return false;

    return runtimeGetVariable(state, 3) == 1;
}

// 9. Command 0 (End of list) stops execution, and an unterminated
// branch (missing End) stops the page cleanly instead of crashing.
static bool testUnterminatedBranch(void) {
    GameState *state = &s_testState;
    runtimeInit(state);

    // Part A: commands after End of list must not run.
    EventCommand listA[3];
    makeCommand(&listA[0], 0, 0, NULL, 0);    // End of list
    makeSetVar(&listA[1], 0, 2, 1);           // must NOT run
    makeSetVar(&listA[2], 0, 3, 1);           // must NOT run

    if (!executeCommandList(listA, 3, state))
        return false;

    if (runtimeGetVariable(state, 2) != 0 ||
        runtimeGetVariable(state, 3) != 0)
        return false;

    // Part B: a false If with no matching End/Else stops cleanly.
    EventCommand listB[3];
    makeIfVarEq(&listB[0], 0, 1, 99);         // If var1 == 99 (false)
    makeSetVar(&listB[1], 1, 4, 1);           //   must NOT run
    makeCommand(&listB[2], 0, 0, NULL, 0);    // End of list (no 412)

    if (!executeCommandList(listB, 3, state))
        return false;

    return runtimeGetVariable(state, 4) == 0;
}

// Runs every interpreter self-test. Returns true when all pass.
// Failures are printed to the console (bottom screen on the 3DS).
bool eventsRunBranchSelfTests(void) {
    typedef bool (*TestFn)(void);

    static const TestFn tests[] = {
        testIfTrueRunsThen,
        testIfFalseSkipsThen,
        testIfFalseRunsElse,
        testIfTrueSkipsElse,
        testSwitchConditions,
        testNestedBranches,
        testComparisonOperators,
        testVariableOperand,
        testUnterminatedBranch
    };

    static const char *names[] = {
        "if-true runs then",
        "if-false skips then",
        "if-false runs else",
        "if-true skips else",
        "switch conditions",
        "nested branches",
        "comparison operators",
        "variable operand",
        "end-of-list + unterminated"
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    for (int i = 0; i < total; i++) {
        if (tests[i]())
            passed++;
        else
            printf("self-test FAILED: %s\n", names[i]);
    }

    printf("self-tests: %d/%d passed\n", passed, total);

    return passed == total;
}// ---------------------------------------------------------------------------
// Section 5: public file API
// ---------------------------------------------------------------------------

// Reads one page's commands into a single malloc'd array. Each
// command's params JSON is parsed into the inline params[] (up to
// FH_MAX_PARAMS numeric values; strings are skipped). Returns NULL on
// any read or parse error. The caller frees the array.
static EventCommand *readPageCommands(
    FILE *fp,
    uint16_t commandCount
) {
    // Allocate at least one slot so a zero-command page still returns
    // a valid pointer (executeCommandList handles a count of 0 fine).
    EventCommand *commands = malloc(
        (commandCount ? commandCount : 1) * sizeof(EventCommand)
    );

    if (!commands)
        return NULL;

    for (uint16_t c = 0; c < commandCount; c++) {
        unsigned char commandHeader[8];

        if (!readExact(fp, commandHeader, 8)) {
            free(commands);
            return NULL;
        }

        EventCommand *cmd = &commands[c];

        cmd->code = readU16(commandHeader);
        cmd->indent = (int16_t)readU16(commandHeader + 2);
        cmd->paramCount = 0;

        uint32_t paramsLength = readU32(commandHeader + 4);

        // Empty params, or params too large for the stack buffer
        // (long text/script strings): keep zero params and skip the
        // bytes. Those commands are not executable yet anyway.
        if (paramsLength == 0 || paramsLength >= FH_MAX_JSON) {
            if (paramsLength && !skipBytes(fp, paramsLength)) {
                free(commands);
                return NULL;
            }

            memset(cmd->params, 0, sizeof(cmd->params));
            continue;
        }

        char json[FH_MAX_JSON];

        if (!readExact(fp, json, paramsLength)) {
            free(commands);
            return NULL;
        }

        json[paramsLength] = '\0';

        int count = parseJsonArray(json, cmd->params, FH_MAX_PARAMS);

        if (count < 0) {
            free(commands);
            return NULL;
        }

        cmd->paramCount = (size_t)count;
    }

    return commands;
}

// Walks the entire file, verifying the signature and that every
// event/page/command record is intact. Fills info (may be NULL) with
// the map ID and event/page/command totals.
bool eventsValidate(const char *path, EventFileInfo *info) {
    if (info)
        memset(info, 0, sizeof(*info));

    if (!path)
        return false;

    FILE *fp = fopen(path, "rb");

    if (!fp)
        return false;

    unsigned char header[12];

    if (!readExact(fp, header, 12) ||
        memcmp(header, "FHEC", 4) != 0) {
        fclose(fp);
        return false;
    }

    // header+4 is the version field: read but not enforced.
    uint16_t mapId = readU16(header + 6);
    uint32_t eventCount = readU32(header + 8);

    uint32_t pageCount = 0;
    uint32_t commandCount = 0;
    bool valid = true;

    for (uint32_t e = 0; e < eventCount && valid; e++) {
        unsigned char eventHeader[8];

        if (!readExact(fp, eventHeader, 8)) {
            valid = false;
            break;
        }

        uint16_t eventPages = readU16(eventHeader + 6);

        for (uint16_t p = 0; p < eventPages && valid; p++) {
            unsigned char pageHeader[8];

            if (!readExact(fp, pageHeader, 8)) {
                valid = false;
                break;
            }

            uint16_t pageCommands = readU16(pageHeader + 2);
            uint32_t conditionsLength = readU32(pageHeader + 4);

            if (!skipBytes(fp, conditionsLength)) {
                valid = false;
                break;
            }

            for (uint16_t c = 0; c < pageCommands; c++) {
                unsigned char commandHeader[8];

                if (!readExact(fp, commandHeader, 8)) {
                    valid = false;
                    break;
                }

                uint32_t paramsLength = readU32(commandHeader + 4);

                if (!skipBytes(fp, paramsLength)) {
                    valid = false;
                    break;
                }

                commandCount++;
            }

            if (valid)
                pageCount++;
        }
    }

    fclose(fp);

    if (info) {
        info->mapId = mapId;
        info->eventCount = eventCount;
        info->pageCount = pageCount;
        info->commandCount = commandCount;
    }

    return valid;
}

int eventsGetActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
) {
    if (!path || !state)
        return -1;

    FILE *fp = fopen(path, "rb");

    if (!fp)
        return -1;

    unsigned char header[12];

    if (!readExact(fp, header, 12) ||
        memcmp(header, "FHEC", 4) != 0) {
        fclose(fp);
        return -1;
    }

    uint32_t eventCount = readU32(header + 8);
    int activePage = -1;

    for (uint32_t e = 0; e < eventCount; e++) {
        unsigned char eventHeader[8];

        if (!readExact(fp, eventHeader, 8))
            break;

        uint16_t currentEventId = readU16(eventHeader);
        uint16_t pageCount = readU16(eventHeader + 6);

        bool isTargetEvent = (currentEventId == eventId);

        for (uint16_t p = 0; p < pageCount; p++) {
            unsigned char pageHeader[8];

            if (!readExact(fp, pageHeader, 8)) {
                fclose(fp);
                return activePage;
            }

            uint16_t pageIndex = readU16(pageHeader);
            uint16_t commandCount = readU16(pageHeader + 2);
            uint32_t conditionsLength = readU32(pageHeader + 4);

            if (isTargetEvent) {
                char condBuffer[1024];

                if (conditionsLength < sizeof(condBuffer)) {
                    if (!readExact(fp, condBuffer, conditionsLength)) {
                        fclose(fp);
                        return activePage;
                    }

                    condBuffer[conditionsLength] = '\0';

                    if (evaluatePageConditions(condBuffer, state))
                        activePage = pageIndex;
                }
                else {
                    if (!skipBytes(fp, conditionsLength)) {
                        fclose(fp);
                        return activePage;
                    }
                }
            }
            else {
                if (!skipBytes(fp, conditionsLength)) {
                    fclose(fp);
                    return activePage;
                }
            }

            for (uint16_t c = 0; c < commandCount; c++) {
                unsigned char commandHeader[8];

                if (!readExact(fp, commandHeader, 8)) {
                    fclose(fp);
                    return activePage;
                }

                uint32_t paramsLength = readU32(commandHeader + 4);

                if (!skipBytes(fp, paramsLength)) {
                    fclose(fp);
                    return activePage;
                }
            }
        }
    }

    fclose(fp);
    return activePage;
}

bool eventsExecutePage(
    const char *path,
    uint16_t eventId,
    uint16_t pageIndex,
    GameState *state
) {
    if (!path || !state)
        return false;

    FILE *fp = fopen(path, "rb");

    if (!fp)
        return false;

    unsigned char header[12];

    if (!readExact(fp, header, 12) ||
        memcmp(header, "FHEC", 4) != 0) {
        fclose(fp);
        return false;
    }

    uint32_t eventCount = readU32(header + 8);

    for (uint32_t e = 0; e < eventCount; e++) {
        unsigned char eventHeader[8];

        if (!readExact(fp, eventHeader, 8))
            break;

        uint16_t currentEventId = readU16(eventHeader);
        uint16_t pageCount = readU16(eventHeader + 6);

        bool isTargetEvent = (currentEventId == eventId);

        for (uint16_t p = 0; p < pageCount; p++) {
            unsigned char pageHeader[8];

            if (!readExact(fp, pageHeader, 8)) {
                fclose(fp);
                return false;
            }

            uint16_t currentPageIdx = readU16(pageHeader);
            uint16_t commandCount = readU16(pageHeader + 2);
            uint32_t conditionsLength = readU32(pageHeader + 4);

            bool isTargetPage =
                isTargetEvent && (currentPageIdx == pageIndex);

            if (!skipBytes(fp, conditionsLength)) {
                fclose(fp);
                return false;
            }

            if (isTargetPage) {
                EventCommand *commands = readPageCommands(
                    fp,
                    commandCount
                );

                if (!commands) {
                    fclose(fp);
                    return false;
                }

                bool result = executeCommandList(
                    commands,
                    commandCount,
                    state
                );

                free(commands);
                fclose(fp);

                return result;
            }

            for (uint16_t c = 0; c < commandCount; c++) {
                unsigned char commandHeader[8];

                if (!readExact(fp, commandHeader, 8)) {
                    fclose(fp);
                    return false;
                }

                uint32_t paramsLength = readU32(commandHeader + 4);

                if (!skipBytes(fp, paramsLength)) {
                    fclose(fp);
                    return false;
                }
            }
        }
    }

    fclose(fp);
    return false;
}

bool eventsExecuteActivePage(
    const char *path,
    uint16_t eventId,
    GameState *state
) {
    int pageIndex = eventsGetActivePage(path, eventId, state);

    if (pageIndex < 0)
        return false;

    return eventsExecutePage(
        path,
        eventId,
        (uint16_t)pageIndex,
        state
    );
}// Returns true and fills outEventId when an event occupies tile (x, y).
bool eventsEventAt(
    const char *path,
    int x,
    int y,
    uint16_t *outEventId
) {
    if (!path)
        return false;

    FILE *fp = fopen(path, "rb");

    if (!fp)
        return false;

    unsigned char header[12];

    if (!readExact(fp, header, 12) ||
        memcmp(header, "FHEC", 4) != 0) {
        fclose(fp);
        return false;
    }

    uint32_t eventCount = readU32(header + 8);

    for (uint32_t e = 0; e < eventCount; e++) {
        unsigned char eventHeader[8];

        if (!readExact(fp, eventHeader, 8))
            break;

        uint16_t eventId = readU16(eventHeader);
        int ex = readU16(eventHeader + 2);   // ASSUMED: x
        int ey = readU16(eventHeader + 4);   // ASSUMED: y
        uint16_t pageCount = readU16(eventHeader + 6);

        if (ex == x && ey == y) {
            fclose(fp);

            if (outEventId)
                *outEventId = eventId;

            return true;
        }

        for (uint16_t p = 0; p < pageCount; p++) {
            unsigned char pageHeader[8];

            if (!readExact(fp, pageHeader, 8)) {
                fclose(fp);
                return false;
            }

            uint16_t commandCount = readU16(pageHeader + 2);
            uint32_t conditionsLength = readU32(pageHeader + 4);

            if (!skipBytes(fp, conditionsLength)) {
                fclose(fp);
                return false;
            }

            for (uint16_t c = 0; c < commandCount; c++) {
                unsigned char commandHeader[8];

                if (!readExact(fp, commandHeader, 8)) {
                    fclose(fp);
                    return false;
                }

                uint32_t paramsLength = readU32(commandHeader + 4);

                if (!skipBytes(fp, paramsLength)) {
                    fclose(fp);
                    return false;
                }
            }
        }
    }

    fclose(fp);
    return false;
}// Debug: prints every event's id, position, and page count.
void eventsDumpEventPositions(const char *path) {
    FILE *fp = fopen(path, "rb");

    if (!fp) {
        printf("dump: open failed\n");
        return;
    }

    unsigned char header[12];

    if (!readExact(fp, header, 12) ||
        memcmp(header, "FHEC", 4) != 0) {
        fclose(fp);
        printf("dump: bad header\n");
        return;
    }

    uint32_t eventCount = readU32(header + 8);

    printf("dump: %lu events\n", (unsigned long)eventCount);

    for (uint32_t e = 0; e < eventCount; e++) {
        unsigned char eventHeader[8];

        if (!readExact(fp, eventHeader, 8))
            break;

        printf("event %u: x=%u y=%u pages=%u\n",
            readU16(eventHeader),
            readU16(eventHeader + 2),
            readU16(eventHeader + 4),
            readU16(eventHeader + 6)
        );

        uint16_t pageCount = readU16(eventHeader + 6);

        for (uint16_t p = 0; p < pageCount; p++) {
            unsigned char pageHeader[8];

            if (!readExact(fp, pageHeader, 8)) {
                fclose(fp);
                return;
            }

            uint16_t commandCount = readU16(pageHeader + 2);
            uint32_t conditionsLength = readU32(pageHeader + 4);

            if (!skipBytes(fp, conditionsLength)) {
                fclose(fp);
                return;
            }

            for (uint16_t c = 0; c < commandCount; c++) {
                unsigned char commandHeader[8];

                if (!readExact(fp, commandHeader, 8)) {
                    fclose(fp);
                    return;
                }

                uint32_t paramsLength = readU32(commandHeader + 4);

                if (!skipBytes(fp, paramsLength)) {
                    fclose(fp);
                    return;
                }
            }
        }
    }

    fclose(fp);
}