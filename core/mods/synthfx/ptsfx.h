/* Modular-Trackular Plus: Synth FX plugins (.ptsfx). SPDX-License-Identifier: MIT
 *
 * A Synth FX plugin is a synth effect in a file on the card, in /SynthFX/. Each synth part
 * has two effect slots, FX1 and FX2, and each picks one plugin. The firmware loads the
 * plugin's code into its own memory and calls it in the audio interrupt, once per block of
 * 128 samples, on the part's audio, in place. docs/synthfx_plugins.md has the rules and a
 * walk-through; ptsfx_builder.py builds a plugin from a C or C++ file.
 *
 * The rules, because the code runs in the audio interrupt, beside the synth engines:
 *   - Single precision: float, sinf, 0.5f. A double is done in software and is slow.
 *   - No globals that change: all of a plugin's state is in the memory the host gives each
 *     instance (state_size bytes, zeroed). One plugin's code serves every slot and voice
 *     that uses it. Constant tables are fine. The builder refuses .data and .bss.
 *   - No heap, no files, no screen, nothing that waits.
 *   - process() must finish quickly: over its share of a block (a quarter) for 8 blocks
 *     running, the host bypasses the slot. Output that is NaN or infinite becomes 0, and
 *     the output is held within +-4.
 *   - The code must run wherever it is loaded: the builder compiles it position
 *     independent and checks that it is.
 *
 * Firmware copy: core/mods/synthfx/ptsfx.h is the same file (a host test checks).
 */

#ifndef PTSFX_H_
#define PTSFX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTSFX_MAGIC         0x46535450u     /* "PTSF", little-endian */
#define PTSFX_VERSION       1u
#define PTSFX_MAX_PARAMS    12u
#define PTSFX_BLOCK         128u            /* frames per process() call */
#define PTSFX_RATE          44100.0f
#define PTSFX_NO_ENTRY      0xFFFFFFFFu     /* an optional entry left out */

/* The plugin's flags */
enum
{
    PTSFX_FLAG_WIDENS = 0x0001,             /* makes stereo from mono (a chorus): on a mono part,
                                               process() gets L in and writes L and R */
    PTSFX_FLAG_PRE    = 0x0002,             /* may run in each voice, before the filter (WAVE's
                                               Placement Pre): mono only, R is null */
};

/* A knob's display */
enum
{
    PTSFX_PARAM_FLOAT = 'f',                /* the value with one decimal */
    PTSFX_PARAM_INT   = 'd',                /* the value as a whole number */
    PTSFX_PARAM_LIST  = 'l',                /* whole steps from min to max, each shown by format() */
};

/* One knob: 32 bytes. The host keeps a knob on its steps: its value is always min + i * step
 * for a whole i from 0 up to max, rounded once to single precision (fmaf(i, step, min)). */
typedef struct __attribute__((packed, aligned(4)))
{
    char    label[11];                      /* up to 10 characters */
    uint8_t display_type;                   /* PTSFX_PARAM_* */
    float   min_val;
    float   max_val;
    float   default_val;                    /* on a step */
    float   step;                           /* one encoder tick, over 0 */
    char    unit[4];                        /* up to 3 characters, after the value */
} PtsfxParam;

/* The file's header: 476 bytes, then the code. The plugin fills in the first part
 * (PTSFX_INFO); the builder fills in magic, version, the code's place and the entries'
 * offsets (from the code's start; Thumb bit clear). */
typedef struct __attribute__((packed, aligned(4)))
{
    uint32_t   magic;                       /* PTSFX_MAGIC */
    uint16_t   format_version;              /* PTSFX_VERSION */
    uint16_t   flags;                       /* PTSFX_FLAG_* */
    char       name[32];                    /* shown in the slot's list */
    char       author[16];
    uint8_t    num_params;                  /* 0 to PTSFX_MAX_PARAMS */
    uint8_t    reserved[3];
    uint32_t   state_size;                  /* bytes of state per instance (zeroed by the host) */
    uint32_t   code_offset;                 /* where the code starts in the file */
    uint32_t   code_size;
    uint32_t   init_offset;
    uint32_t   set_param_offset;
    uint32_t   process_offset;
    uint32_t   reset_offset;
    uint32_t   format_offset;               /* PTSFX_NO_ENTRY if there is none */
    PtsfxParam params[PTSFX_MAX_PARAMS];
} PtsfxHeader;

/* What the host gives a plugin: its rate and block, and functions it may call anywhere,
 * process() included. */
typedef struct
{
    uint32_t version;                       /* 1 */
    float    sample_rate;                   /* 44100 */
    uint32_t block;                         /* 128 */
    float    (*sinf)(float);
    float    (*cosf)(float);
    float    (*tanf)(float);
    float    (*tanhf)(float);
    float    (*expf)(float);
    float    (*logf)(float);
    float    (*powf)(float, float);
    float    (*sqrtf)(float);
    float    (*fabsf)(float);
    float    (*fminf)(float, float);
    float    (*fmaxf)(float, float);
    uint32_t (*random)(void);               /* 32 random bits, fast */
    /* format() only (the main loop): snprintf with one float, e.g. "%.1f kHz" */
    int      (*print)(char *out, uint32_t size, const char *fmt, float value);
} PtsfxHost;

/* The entries. st: the instance's state. In the main loop: init (once per instance, on
 * zeroed state) and format. In the audio interrupt: set_param (before a block, for each knob
 * that changed, and for every knob after init), process and reset. */
typedef void (*PtsfxInit)(void *st, const PtsfxHost *host);
typedef void (*PtsfxSetParam)(void *st, uint32_t index, float value);
/* frames is PTSFX_BLOCK. R is null for mono (in and out). With R given, in_channels says
 * whether R holds input too (2) or is only for output (1: a WIDENS plugin on a mono part). */
typedef void (*PtsfxProcess)(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels);
typedef void (*PtsfxReset)(void *st);
/* A knob's text for a value (without the label), into out (size bytes, ended by a 0). Without
 * format(), or for a knob it leaves empty, the host shows the value and unit. */
typedef void (*PtsfxFormat)(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size);

#ifdef __cplusplus
static_assert(sizeof(PtsfxParam) == 32, "PtsfxParam is 32 bytes");
static_assert(sizeof(PtsfxHeader) == 476, "PtsfxHeader is 476 bytes");
#else
_Static_assert(sizeof(PtsfxParam) == 32, "PtsfxParam is 32 bytes");
_Static_assert(sizeof(PtsfxHeader) == 476, "PtsfxHeader is 476 bytes");
#endif

/* ---- for plugins ---------------------------------------------------------------------- */

/* The plugin's header, filled in at build time: PTSFX_INFO = { ... } with the fields above
 * from flags to params (the builder sets the rest). */
#ifdef __cplusplus
#define PTSFX_EXPORT extern "C" __attribute__((used, visibility("default")))
#else
#define PTSFX_EXPORT __attribute__((used, visibility("default")))
#endif
#define PTSFX_INFO PTSFX_EXPORT __attribute__((section(".ptsfx_info"))) const PtsfxHeader ptsfx_info

/* A knob, for PTSFX_INFO's params */
#define PTSFX_KNOB(label, lo, hi, def, step, unit, type) \
    { label, (uint8_t)(type), (float)(lo), (float)(hi), (float)(def), (float)(step), unit }

/* The entries' names: the builder finds them by these */
#define PTSFX_INIT        PTSFX_EXPORT void ptsfx_init
#define PTSFX_SET_PARAM   PTSFX_EXPORT void ptsfx_set_param
#define PTSFX_PROCESS     PTSFX_EXPORT void ptsfx_process
#define PTSFX_RESET       PTSFX_EXPORT void ptsfx_reset
#define PTSFX_FORMAT      PTSFX_EXPORT void ptsfx_format

#ifdef __cplusplus
}
#endif

#endif /* PTSFX_H_ */
