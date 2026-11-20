#ifndef MSGTEXT_H
#define MSGTEXT_H

#include <stddef.h>
#include "runtime.h"

#define MSG_COLS 50   // characters per line on the bottom screen (6px font)

// Expands RPG Maker / YEP message codes in `in`:
//   \V[n] variable, \N[n] actor name, \P[n] party member, \G currency,
//   \\ backslash, \n<Name> / \nc<Name> / \nr<Name> speaker name box.
// Colour, icon, size, timing codes (\C[n] \I[n] \{ \} \. \| \! \> \< \^ ...)
// are dropped. The speaker name (if any) goes to `speaker`.
void msgFormat(const char *in, char *out, size_t outSize,
               char *speaker, size_t speakerSize, const GameState *state);

// RPG Maker lines are broken for a ~800px window; on the narrower bottom
// screen a long line would wrap into a ragged short tail. This joins a
// line that is longer than MSG_COLS to the next one (in place), so the
// text flows. Short, intentional lines are left alone.
void msgReflow(char *text);

// Word-wraps text ('\n' = hard break) into lines of at most MSG_COLS.
// Returns the number of lines written (<= maxLines).
int msgWrap(const char *text, char (*lines)[MSG_COLS + 1], int maxLines);

#endif
