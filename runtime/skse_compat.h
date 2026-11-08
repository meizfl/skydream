/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Independent, incomplete SKSE-compatible Papyrus API. Not the SKSE runtime. */
typedef struct {
    const char *class_name, *name;
    uint8_t result, argc, args[4];
    uint16_t operation;
} SkseNative;

const SkseNative *skse_native_table(unsigned *count);
/* Primitive values use the game's VMValue type numbers. Strings are UTF-8
 * byte strings, matching classic Papyrus, not Unicode character indices.
 * A returned string is allocated with malloc and belongs to the caller. */
int skse_native_eval(const SkseNative *fn, const uint32_t args[4],
                     const char *strings[4], uint32_t *value, char **string);

/* System operations are dispatched by the runtime, not the pure evaluator. */
enum { SKSE_SYSTEM_BASE=1000, S_UI_OPEN=1000, S_UI_GBOOL, S_UI_GINT, S_UI_GFLOAT, S_UI_GSTRING,
 S_UI_SBOOL,S_UI_SINT,S_UI_SFLOAT,S_UI_SSTRING,S_UI_IBOOL,S_UI_IINT,S_UI_IFLOAT,S_UI_ISTRING,
 S_UI_ABOOL,S_UI_AINT,S_UI_AFLOAT,S_UI_ASTRING,S_UI_TEXT,
 S_REG_MENU=1100,S_UNREG_MENU,S_UNREG_MENUS,S_REG_KEY,S_UNREG_KEY,S_UNREG_KEYS,
 S_REG_MOD,S_UNREG_MOD,S_UNREG_MODS,S_SEND_MOD,
 S_INPUT_CONTROL=1200,S_GAME_PAD,S_INI_FLOAT,S_INI_INT,S_SET_INI_FLOAT,
 S_METHOD=0x8000 };
