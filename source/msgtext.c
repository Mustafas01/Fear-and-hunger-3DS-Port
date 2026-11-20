#include "msgtext.h"
#include "gamedata.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

static void put(char *out, size_t outSize, size_t *n, const char *s) {
    while (*s && *n + 1 < outSize) out[(*n)++] = *s++;
}

// Reads "[123]" at p. Returns pointer past ']' and the value, or NULL.
static const char *readIndex(const char *p, int *value) {
    if (*p != '[') return NULL;
    p++;
    int v = 0;
    bool any = false;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; any = true; }
    if (*p != ']' || !any) return NULL;
    *value = v;
    return p + 1;
}

void msgFormat(const char *in, char *out, size_t outSize,
               char *speaker, size_t speakerSize, const GameState *state) {
    size_t n = 0;
    if (speaker && speakerSize) speaker[0] = '\0';
    if (!in) { if (outSize) out[0] = '\0'; return; }

    for (const char *p = in; *p && n + 1 < outSize; ) {
        if (*p == '<') {
            // YEP MessageCore layout tags.
            if (!strncasecmp(p, "<WordWrap>", 10)) { p += 10; continue; }
            if (!strncasecmp(p, "<br>", 4)) { put(out, outSize, &n, "\n"); p += 4; continue; }
            if (!strncasecmp(p, "<line break>", 12)) { put(out, outSize, &n, "\n"); p += 12; continue; }
        }
        if (*p != '\\') { out[n++] = *p++; continue; }

        p++; // after the backslash
        if (*p == '\\') { out[n++] = '\\'; p++; continue; }

        // Code name: letters only.
        char code[8];
        int c = 0;
        while (isalpha((unsigned char)*p) && c < 7) code[c++] = (char)toupper((unsigned char)*p++);
        code[c] = '\0';

        // Speaker name box: \n<Name>, \nc<Name>, \nr<Name>
        if ((!strcmp(code, "N") || !strcmp(code, "NC") || !strcmp(code, "NR")) && *p == '<') {
            const char *end = strchr(p, '>');
            if (end) {
                char tmp[128];
                size_t len = (size_t)(end - p - 1);
                if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
                memcpy(tmp, p + 1, len);
                tmp[len] = '\0';
                // The name itself may contain codes (e.g. \N[1]).
                if (speaker && speakerSize)
                    msgFormat(tmp, speaker, speakerSize, NULL, 0, state);
                p = end + 1;
                continue;
            }
        }

        int idx = 0;
        const char *after = readIndex(p, &idx);
        char buf[32];

        if (!strcmp(code, "V") && after) {
            snprintf(buf, sizeof(buf), "%ld", (long)runtimeGetVariable(state, (uint16_t)idx));
            put(out, outSize, &n, buf);
            p = after;
        } else if ((!strcmp(code, "N") || !strcmp(code, "P")) && after) {
            int id = idx;
            if (code[0] == 'P')                 // n-th party member (1-based)
                id = (state && idx >= 1 && idx <= state->partyCount)
                   ? state->partyOrder[idx - 1] : 0;
            const char *nm = (state && id >= 1 && id <= FH_MAX_ACTORS && state->actors[id].name[0])
                           ? state->actors[id].name : gamedataActorName(id);
            put(out, outSize, &n, nm);
            p = after;
        } else if (!strcmp(code, "G")) {
            put(out, outSize, &n, "G");
        } else if (after) {
            p = after;                 // \C[n], \I[n], \FS[n], ... dropped
        } else if (c == 0 && *p) {
            p++;                       // \. \| \! \> \< \^ \{ \} \$ dropped
        }
        // other letter-only codes without [n] are simply dropped
    }
    if (outSize) out[n < outSize ? n : outSize - 1] = '\0';
}

int msgWrap(const char *text, char (*lines)[MSG_COLS + 1], int maxLines) {
    int count = 0;
    const char *p = text;
    while (*p && count < maxLines) {
        // Take one hard line.
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        while (count < maxLines) {
            while (len > 0 && *p == ' ') { p++; len--; }
            if (len <= MSG_COLS) {
                memcpy(lines[count], p, len);
                lines[count][len] = '\0';
                count++;
                p += len;
                break;
            }
            size_t cut = MSG_COLS;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut == 0) cut = MSG_COLS;   // one very long word
            memcpy(lines[count], p, cut);
            lines[count][cut] = '\0';
            count++;
            p += cut;
            len -= cut;
        }
        if (eol && *p == '\n') p++;
        else if (!eol) break;
    }
    return count;
}

void msgReflow(char *text) {
    char *line = text;
    for (;;) {
        char *nl = strchr(line, '\n');
        if (!nl) break;
        size_t len = (size_t)(nl - line);
        bool nextHasText = nl[1] != '\0' && nl[1] != '\n';
        if (len > MSG_COLS && nextHasText)
            *nl = ' ';
        line = nl + 1;
    }
}
