/* Modular-Trackular Plus: Master FX plugins (.ptmfx). SPDX-License-Identifier: MIT
 *
 * A Master FX plugin is a master effect in a file on the card, in /MasterFX/. The Tracker+ has
 * three master slots: the reverb, the delay and the limiter (here "dynamics"). Each slot plays
 * CE's own effect (Stock) or one plugin made for that slot. The firmware loads the plugin's
 * code into the slot's own fast memory and calls it in the audio interrupt, once per block of
 * 128 samples, in the place CE's effect would run. docs/masterfx_plugins.md has the rules and a
 * walk-through; ptmfx_builder.py builds a plugin from a C or C++ file.
 *
 * The rules, because the code runs in the audio interrupt, beside the synth engines:
 *   - Single precision: float, sinf, 0.5f. A double is done in software and is slow.
 *   - No globals that change: all of a plugin's state is in the memory the host gives it
 *     (state_size bytes, zeroed, in fast RAM) and in the slot's memory (PtmfxMemory). Constant
 *     tables are fine. The builder refuses .data and .bss.
 *   - No heap, no files, no screen, nothing that waits.
 *   - process() must finish quickly: over an eighth of a block's time for 8 blocks running,
 *     the host bypasses the slot. With PTMFX_FLAG_FLOAT, output that is NaN or infinite becomes
 *     0 and the plugin is reset, and the output is held within +-4.
 *   - The code must run wherever it is loaded: the builder compiles it position independent
 *     and checks that it is. At most PTMFX_MAX_CODE bytes of code and constants.
 *
 * Firmware copy: core/mods/masterfx/ptmfx.h is the same file (a host test checks).
 */

#ifndef PTMFX_H_
#define PTMFX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTMFX_MAGIC         0x464D5450u     /* "PTMF", little-endian */
#define PTMFX_VERSION       2u
#define PTMFX_MAX_PARAMS    5u              /* the plugin's own knobs */
#define PTMFX_V1_HEADER     240u            /* format version 1's header: still loaded */
#define PTMFX_V1_MAX_PARAMS 2u              /* and its own knobs */
#define PTMFX_MAX_LABELS    6u              /* CE's knobs on the slot's page */
#define PTMFX_BLOCK         128u            /* frames per process() call */
#define PTMFX_RATE          44100.0f
#define PTMFX_NO_ENTRY      0xFFFFFFFFu     /* an optional entry left out */
#define PTMFX_MAX_CODE      8192u           /* a slot's code area */

/* The slot a plugin is made for */
enum
{
    PTMFX_SLOT_REVERB   = 1,                /* in place of CE's reverb */
    PTMFX_SLOT_DELAY    = 2,                /* in place of CE's delay */
    PTMFX_SLOT_DYNAMICS = 3,                /* in place of CE's limiter, on the master */
};

/* The most state a slot gives (state_size) */
#define PTMFX_STATE_REVERB    1024u
#define PTMFX_STATE_DELAY     512u
#define PTMFX_STATE_DYNAMICS  512u

/* The plugin's flags */
enum
{
    PTMFX_FLAG_FLOAT = 0x0001,              /* float blocks (+-1.0) instead of CE's 16-bit ones */
    PTMFX_FLAG_DUCK  = 0x0002,              /* dynamics, with FLOAT: fills PtmfxBlock.gain, so the
                                               host can duck chosen tracks by it (see PtmfxBlock) */
    PTMFX_FLAG_OWN_DYNAMICS = 0x0004,       /* dynamics, 3 own knobs or more: own knobs 0, 1 and 2
                                               take the Limiter page's Attack, Release and Threshold
                                               columns (CE's knobs aren't shown); 3 and 4 go in the
                                               columns before Back */
    PTMFX_FLAG_BUS   = 0x0008,              /* dynamics, with FLOAT, not with DUCK: works on a group
                                               of tracks the Limiter page's Tracks list picks (see
                                               PtmfxBlock) */
};

/* A knob's display */
enum
{
    PTMFX_PARAM_FLOAT = 'f',                /* the value with one decimal */
    PTMFX_PARAM_INT   = 'd',                /* the value as a whole number */
    PTMFX_PARAM_LIST  = 'l',                /* whole steps from min to max, each shown by format() */
};

/* One of the plugin's own knobs: 32 bytes, the same as a Synth FX knob. The host keeps a knob on
 * its steps: its value is always min + i * step for a whole i from 0 up to max, rounded once to
 * single precision (fmaf(i, step, min)). */
typedef struct __attribute__((packed, aligned(4)))
{
    char    label[11];                      /* up to 10 characters */
    uint8_t display_type;                   /* PTMFX_PARAM_* */
    float   min_val;
    float   max_val;
    float   default_val;                    /* on a step */
    float   step;                           /* one encoder tick, over 0 */
    char    unit[4];                        /* up to 3 characters, after the value */
} PtmfxParam;

/* Where the own knobs show: the Reverb and Limiter pages have the two columns before Back for
 * knobs 0 and 1 (with PTMFX_FLAG_OWN_DYNAMICS, knobs 3 and 4, after Attack, Release and Threshold);
 * the Delay page has none. A knob the page has no column for plays at its default. */

/* The file's header: 336 bytes, then the code (format version 1's was 240, with 2 knobs). The plugin fills in the first part (PTMFX_INFO);
 * the builder fills in magic, version, the code's place and the entries' offsets (from the
 * code's start; Thumb bit clear). */
typedef struct __attribute__((packed, aligned(4)))
{
    uint32_t   magic;                       /* PTMFX_MAGIC */
    uint16_t   format_version;              /* PTMFX_VERSION */
    uint16_t   flags;                       /* PTMFX_FLAG_* */
    uint8_t    slot;                        /* PTMFX_SLOT_* */
    uint8_t    num_params;                  /* 0 to PTMFX_MAX_PARAMS */
    uint8_t    reserved[2];
    char       name[32];                    /* shown in the slot's list and title; after its 0 an
                                             * optional short name for the Master page's column,
                                             * also ended by a 0 (older firmware ignores it) */
    char       author[16];
    /* The labels of CE's knobs on the slot's page, in the page's order, as this plugin uses
     * them; an empty one keeps CE's. Index i is the page's column i. Reverb: Size, Damping,
     * Predelay, Diffusion. Delay: PingPong, Sync, Rate, Time, Feedback, Filter. Dynamics:
     * Attack, Release, Threshold. */
    char       labels[PTMFX_MAX_LABELS][12];
    uint32_t   state_size;                  /* bytes of state (zeroed by the host) */
    uint32_t   code_offset;                 /* from the file's start: sizeof(PtmfxHeader) */
    uint32_t   code_size;
    uint32_t   init_offset;
    uint32_t   set_param_offset;
    uint32_t   process_offset;
    uint32_t   reset_offset;
    uint32_t   format_offset;               /* or PTMFX_NO_ENTRY */
    uint32_t   reserved2[3];
    PtmfxParam params[PTMFX_MAX_PARAMS];
} PtmfxHeader;

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
    /* format() only (the main loop): snprintf with one float, e.g. "%.1f ms" */
    int      (*print)(char *out, uint32_t size, const char *fmt, float value);
    float    (*tempo)(void);                /* the project's tempo in BPM */
} PtmfxHost;

/* The slot's memory, cleared by the host before init and before every reset. Reverb: area 0 is
 * 32 KB of fast RAM, area 1 32 KB of SDRAM. Delay: three areas of 512 KB of SDRAM. Dynamics: area
 * 0 is 64 KB of SDRAM. Unused areas are null with size 0. */
typedef struct
{
    void    *area[3];
    uint32_t size[3];
} PtmfxMemory;

/* CE's knobs as its pages left them, for the slot's plugin, every block */
typedef struct
{
    float   amount;                         /* return = send + (wet - send) * amount */
    float   input_gain;
    float   size;                           /* 0..1 (Size) */
    float   diffusion;                      /* 0..0.85 (Diffusion) */
    float   lp;                             /* 1 - Damping, 0..1 */
    int32_t predelay;                       /* samples, 0..5000 (Predelay) */
} PtmfxReverbKnobs;

typedef struct
{
    uint32_t time;                          /* samples (Time, or Sync and Rate) */
    int32_t  filter_pole;                   /* -pole * 1023 (Filter) */
    uint8_t  feedback;                      /* 0..252 (Feedback) */
    uint8_t  pingpong;                      /* 0 or 1 */
    uint8_t  reserved[2];
} PtmfxDelayKnobs;

typedef struct
{
    float    attack;                        /* ms (Attack) */
    float    release;                       /* ms (Release) */
    float    threshold;                     /* 0..1 of full scale (Threshold) */
    uint32_t key_on;                        /* 1: the Sidechain list picked a key */
} PtmfxDynamicsKnobs;

/* One block. Without PTMFX_FLAG_FLOAT the int16_t pointers are set (Q15, CE's own); with it,
 * the float ones. frames is PTMFX_BLOCK.
 *   Reverb:   left and right in place: the send in, the return out.
 *   Delay:    in is the mono send; left and right are the return, to fill.
 *   Dynamics: left and right in place: the master. key_left and key_right are the sidechain
 *             key (the Sidechain list's pick), or null when it is off.
 *   gain:     null, except for a PTMFX_FLAG_DUCK plugin while the project ducks tracks instead of
 *             the master. Then left and right hold a copy of the key (as is the key), the host
 *             throws away what the plugin leaves in them, and the plugin writes into gain[i], for
 *             each frame, the factor it would apply to the master there (its makeup included);
 *             the host multiplies the chosen tracks by it.
 *   PTMFX_FLAG_BUS: with tracks chosen in the Tracks list, left and right hold their sum (as CE's
 *             tracks mixer gets them, before its saturation and the reverb and delay returns)
 *             instead of the master; the plugin works on it in place as it would on the master,
 *             and the host adds what the plugin left to the other tracks' sum before CE's
 *             saturation. The other tracks and the sends aren't touched. Held within +-11.0.
 *             With the Tracks list on Master, it gets the master as usual. */
typedef struct
{
    int16_t       *left;
    int16_t       *right;
    const int16_t *in;
    const int16_t *key_left;
    const int16_t *key_right;
    float         *fleft;
    float         *fright;
    const float   *fin;
    const float   *fkey_left;
    const float   *fkey_right;
    const void    *knobs;                   /* PtmfxReverbKnobs, PtmfxDelayKnobs or PtmfxDynamicsKnobs */
    uint32_t       frames;
    float         *gain;                    /* frames floats to fill, or null (see above) */
} PtmfxBlock;

/* The entries. st: the plugin's state.
 *   init      main loop, once, on zeroed state: keep mem and host. The memory isn't ready yet:
 *             don't write to it here.
 *   set_param audio interrupt: an own knob, before a block (each that changed, and every one
 *             after init).
 *   reset     audio interrupt: the memory has just been cleared (before the first block, at a
 *             project load, an export, the sample editor); start over in it.
 *   process   audio interrupt: one block. Returns the gain it applied for the slot's meter
 *             (dynamics: 1.0 is none); other slots return 1.0.
 *   format    main loop, optional: an own knob's text for a value. */
typedef void  (*PtmfxInit)(void *st, const PtmfxMemory *mem, const PtmfxHost *host);
typedef void  (*PtmfxSetParam)(void *st, uint32_t index, float value);
typedef void  (*PtmfxReset)(void *st);
typedef float (*PtmfxProcess)(void *st, const PtmfxBlock *block);
typedef void  (*PtmfxFormat)(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size);

#ifdef __cplusplus
static_assert(sizeof(PtmfxParam) == 32, "PtmfxParam is 32 bytes");
static_assert(sizeof(PtmfxHeader) == 336, "PtmfxHeader is 336 bytes");
#else
_Static_assert(sizeof(PtmfxParam) == 32, "PtmfxParam is 32 bytes");
_Static_assert(sizeof(PtmfxHeader) == 336, "PtmfxHeader is 336 bytes");
#endif

/* ---- for plugins ---------------------------------------------------------------------- */

/* The plugin's header, filled in at build time: PTMFX_INFO = { ... } with the fields from flags
 * to labels and params (the builder sets the rest). */
#ifdef __cplusplus
#define PTMFX_EXPORT extern "C" __attribute__((used, visibility("default")))
#else
#define PTMFX_EXPORT __attribute__((used, visibility("default")))
#endif
#define PTMFX_INFO PTMFX_EXPORT __attribute__((section(".ptmfx_info"))) const PtmfxHeader ptmfx_info

/* A knob, for PTMFX_INFO's params */
#define PTMFX_KNOB(label, lo, hi, def, step, unit, type) \
    { label, (uint8_t)(type), (float)(lo), (float)(hi), (float)(def), (float)(step), unit }

/* The entries' names: the builder finds them by these */
#define PTMFX_INIT        PTMFX_EXPORT void ptmfx_init
#define PTMFX_SET_PARAM   PTMFX_EXPORT void ptmfx_set_param
#define PTMFX_RESET       PTMFX_EXPORT void ptmfx_reset
#define PTMFX_PROCESS     PTMFX_EXPORT float ptmfx_process
#define PTMFX_FORMAT      PTMFX_EXPORT void ptmfx_format

#ifdef __cplusplus
}
#endif

#endif /* PTMFX_H_ */
