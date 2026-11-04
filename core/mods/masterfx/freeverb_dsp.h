/*
 * freeverb_dsp.h: Freeverb in CE's reverb slot. Included once, by ../masterfxDsp.cpp.
 *
 * Freeverb (Jezar at Dreampoint, public domain): the send is summed to mono and fed to
 * 8 parallel comb filters with a one-pole lowpass in their feedback, then 4 series
 * allpasses, per channel; the right channel's delays are 23 samples longer (stereo
 * spread). The delay lengths are Freeverb's own, for 44.1 kHz. The delay lines are
 * stored as 16-bit samples in the stock reverb's two 32 KB buffers: left in the first,
 * right in the second, and the predelay line in what is left at the end of the two.
 * fv_init() lays them out in the (cleared) buffers.
 *
 * The filters run over the whole block at a time, in sample units (16-bit full scale =
 * 32768), so the delay lines need no scaling: the combs four at a time (adding their
 * outputs in the same order as one by one, so the result is the same to the bit), the
 * four allpasses together, and each pass split where one of its lines wraps, so the
 * inner loops have no per-sample position checks.
 *
 * The Master Reverb page is unchanged; Freeverb reads its knobs every block:
 *  - Size (0..1)       -> room size: comb feedback 0.70..0.98
 *  - Damping (1 - damping) -> damping of the comb lowpass, 0..0.4
 *  - Predelay (0..5000 samples, up to about 113 ms) -> a delay before the reverb
 *  - Diffusion (0..0.85) -> allpass feedback 0.30..0.72
 * The return is mixed as the stock reverb mixes it: send + (wet - send) * amount, with
 * the object's amount (0.8) and input gain (0.1).
 */
#ifndef FREEVERB_DSP_H
#define FREEVERB_DSP_H

#define BLOCK               128u
#define BUFFER_SAMPLES      16384u          /* each stock buffer: 32 KB */

/* ---- Freeverb tuning (44.1 kHz) ---------------------------------------------------- */
#define COMBS               8u
#define ALLPASSES           4u
#define STEREO_SPREAD       23u
#define FIXED_GAIN          0.015f          /* Freeverb's input gain ... */
#define SCALE_WET           3.0f            /* ... and wet gain */
#define LINE_GAIN           4.0f            /* the combs run this much hotter (16-bit lines) */
#define OUTPUT_TRIM         0.122f          /* -18.3 dB: the stock reverb's return level */
#define SCALE_ROOM          0.28f
#define OFFSET_ROOM         0.7f
#define SCALE_DAMP          0.4f
#define PREDELAY_MAX        5000u           /* the stock setter's range */
#define PRE_RING            (PREDELAY_MAX + 1u)

static const uint16_t comb_len[COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const uint16_t allpass_len[ALLPASSES] = {556, 441, 341, 225};

/* ---- state ---------------------------------------------------------------------- */
#define FV_MAGIC            0x56524654u     /* "TFRV": lines laid out for buf_l/buf_r */

struct line { uint16_t start, len, pos, pad; };

struct fv_state {
    uint32_t magic;
    uint32_t buf_l, buf_r;                  /* the buffers the lines were laid out in */
    struct line comb[2][COMBS];
    struct line allpass[2][ALLPASSES];
    float comb_store[2][COMBS];             /* comb lowpass states */
    uint16_t pre_pos;                       /* predelay ring write position, 0..PRE_RING-1 */
    uint16_t pre_split;                     /* ring positions below this are in the left buffer */
    uint32_t pre_l, pre_r;                  /* the ring's two parts: the buffers' unused ends */
};
static struct fv_state fv_state_ram;
#define FV (&fv_state_ram)

static inline int16_t sat16(float v)
{
    return (int16_t)__ssat((int32_t)v, 16);
}

/* A predelay ring position: the first part is in the left buffer's spare end, the rest
 * in the right buffer's. */
static inline int16_t *pre_at(uint32_t i)
{
    return i < FV->pre_split ? (int16_t *)FV->pre_l + i : (int16_t *)FV->pre_r + (i - FV->pre_split);
}

/* Lay out the delay lines in the two (cleared) buffers. */
static void fv_init(uint32_t buf_l, uint32_t buf_r)
{
    for (uint32_t ch = 0; ch < 2; ch++) {
        uint32_t at = 0, spread = ch ? STEREO_SPREAD : 0;
        for (uint32_t i = 0; i < COMBS; i++) {
            FV->comb[ch][i] = (struct line){(uint16_t)at, (uint16_t)(comb_len[i] + spread), 0, 0};
            at += comb_len[i] + spread;
            FV->comb_store[ch][i] = 0.0f;
        }
        for (uint32_t i = 0; i < ALLPASSES; i++) {
            FV->allpass[ch][i] = (struct line){(uint16_t)at, (uint16_t)(allpass_len[i] + spread), 0, 0};
            at += allpass_len[i] + spread;
        }
        /* the rest of this buffer: part of the predelay ring */
        if (ch == 0) {
            FV->pre_l = buf_l + 2 * at;
            FV->pre_split = (uint16_t)(BUFFER_SAMPLES - at);
        } else {
            FV->pre_r = buf_r + 2 * at;
        }
    }
    FV->pre_pos = 0;
    FV->buf_l = buf_l;
    FV->buf_r = buf_r;
    FV->magic = FV_MAGIC;
}

#define GROUP               4u              /* combs per pass */

/* Samples until the first of n lines wraps, at most `left`. */
static inline uint32_t run_length(const uint32_t *pos, const uint32_t *len, uint32_t n, uint32_t left)
{
    for (uint32_t j = 0; j < n; j++)
        if (len[j] - pos[j] < left)
            left = len[j] - pos[j];
    return left;
}

/* Four comb filters over the block. first: acc = their sum, else acc += their sum, with
 * the outputs added in comb order as one comb per pass would. */
static inline __attribute__((always_inline)) void comb_group(struct line *c, int16_t *buf, float *store_p,
                                                             const float *in, float *acc, int first,
                                                             float feedback, float damp1, float damp2)
{
    uint32_t pos[GROUP], len[GROUP];
    for (uint32_t k = 0; k < GROUP; k++) {
        pos[k] = c[k].pos;
        len[k] = c[k].len;
    }
    float s0 = store_p[0], s1 = store_p[1], s2 = store_p[2], s3 = store_p[3];
    for (uint32_t n = 0; n < BLOCK;) {
        uint32_t run = run_length(pos, len, GROUP, BLOCK - n);
        int16_t *q0 = buf + c[0].start + pos[0], *q1 = buf + c[1].start + pos[1];
        int16_t *q2 = buf + c[2].start + pos[2], *q3 = buf + c[3].start + pos[3];
        const float *x = in + n;
        float *a = acc + n;
        for (uint32_t k = 0; k < run; k++) {
            float o0 = (float)q0[k], o1 = (float)q1[k], o2 = (float)q2[k], o3 = (float)q3[k];
            s0 = o0 * damp2 + s0 * damp1;
            q0[k] = sat16(x[k] + s0 * feedback);
            s1 = o1 * damp2 + s1 * damp1;
            q1[k] = sat16(x[k] + s1 * feedback);
            s2 = o2 * damp2 + s2 * damp1;
            q2[k] = sat16(x[k] + s2 * feedback);
            s3 = o3 * damp2 + s3 * damp1;
            q3[k] = sat16(x[k] + s3 * feedback);
            float sum = first ? 0.0f + o0 : a[k] + o0;
            a[k] = ((sum + o1) + o2) + o3;
        }
        for (uint32_t k = 0; k < GROUP; k++)
            if ((pos[k] += run) == len[k])
                pos[k] = 0;
        n += run;
    }
    for (uint32_t k = 0; k < GROUP; k++)
        c[k].pos = (uint16_t)pos[k];
    store_p[0] = s0;
    store_p[1] = s1;
    store_p[2] = s2;
    store_p[3] = s3;
}

/* The four allpasses in series over the block, in place. */
static void allpass_chain(struct line *a, int16_t *buf, float *io, float feedback)
{
    uint32_t pos[ALLPASSES], len[ALLPASSES];
    for (uint32_t k = 0; k < ALLPASSES; k++) {
        pos[k] = a[k].pos;
        len[k] = a[k].len;
    }
    for (uint32_t n = 0; n < BLOCK;) {
        uint32_t run = run_length(pos, len, ALLPASSES, BLOCK - n);
        int16_t *q0 = buf + a[0].start + pos[0], *q1 = buf + a[1].start + pos[1];
        int16_t *q2 = buf + a[2].start + pos[2], *q3 = buf + a[3].start + pos[3];
        float *v = io + n;
        for (uint32_t k = 0; k < run; k++) {
            float x = v[k], b;
            b = (float)q0[k];
            q0[k] = sat16(x + b * feedback);
            x = b - x;
            b = (float)q1[k];
            q1[k] = sat16(x + b * feedback);
            x = b - x;
            b = (float)q2[k];
            q2[k] = sat16(x + b * feedback);
            x = b - x;
            b = (float)q3[k];
            q3[k] = sat16(x + b * feedback);
            v[k] = b - x;
        }
        for (uint32_t k = 0; k < ALLPASSES; k++)
            if ((pos[k] += run) == len[k])
                pos[k] = 0;
        n += run;
    }
    for (uint32_t k = 0; k < ALLPASSES; k++)
        a[k].pos = (uint16_t)pos[k];
}

/* lo..hi; NaN (from a corrupted project) counts as lo, so filter states never get NaN */
static float fclamp(float v, float lo, float hi)
{
    if (!(v >= lo))
        return lo;
    return v > hi ? hi : v;
}

/* Process one block of the stereo send in place; returns the average output level. */
MFX_FLASH static float fv_process(const ReverbKnobs *obj, int16_t *left, int16_t *right, int muted)
{
    int16_t *buf[2] = {(int16_t *)FV->buf_l, (int16_t *)FV->buf_r};
    int16_t *io[2] = {left, right};
    float size = fclamp(obj->size, 0.0f, 1.0f);
    float damping = fclamp(1.0f - obj->lp, 0.0f, 1.0f);
    float diffusion = fclamp(obj->diffusion, 0.0f, 1.0f);
    float amount = fclamp(obj->amount, 0.0f, 1.0f);
    float gain = fclamp(obj->inputGain, 0.0f, 1.0f);
    int32_t predelay = obj->predelay;
    if (predelay < 0)
        predelay = 0;
    if (predelay > (int32_t)PREDELAY_MAX)
        predelay = PREDELAY_MAX;

    float feedback = size * SCALE_ROOM + OFFSET_ROOM;
    float damp1 = damping * SCALE_DAMP, damp2 = 1.0f - damp1;
    float ap_feedback = 0.3f + diffusion * 0.5f;
    /* the stock reverb's input gain (0.1) moves Freeverb's input gain with it */
    float in_gain = FIXED_GAIN * LINE_GAIN * (gain / 0.1f);
    float wet_gain = SCALE_WET * OUTPUT_TRIM / LINE_GAIN;

    /* the mono send through the predelay (a ring of PRE_RING samples), in integers
     * ((l + r) / 2 cuts the half toward zero, as the old float write's sat16 did); the
     * walks split at the ring's buffer seam and its end instead of branching per sample */
    float mono[BLOCK];
    {
        uint32_t wr = FV->pre_pos;
        uint32_t rd = wr + PRE_RING - (uint32_t)predelay;
        if (rd >= PRE_RING)
            rd -= PRE_RING;
        float g2 = 2.0f * in_gain;
        for (uint32_t n = 0; n < BLOCK;) {
            uint32_t run = BLOCK - n;
            uint32_t wb = (wr < FV->pre_split ? FV->pre_split : PRE_RING) - wr;
            uint32_t rb = (rd < FV->pre_split ? FV->pre_split : PRE_RING) - rd;
            if (wb < run)
                run = wb;
            if (rb < run)
                run = rb;
            int16_t *pw = pre_at(wr), *pr = pre_at(rd);
            for (uint32_t i = 0; i < run; i++) {
                *pw++ = (int16_t)(((int32_t)left[n + i] + (int32_t)right[n + i]) / 2);
                mono[n + i] = (float)*pr++ * g2;
            }
            wr += run;
            if (wr == PRE_RING)
                wr = 0;
            rd += run;
            if (rd == PRE_RING)
                rd = 0;
            n += run;
        }
        FV->pre_pos = (uint16_t)wr;
    }

    float level = 0.0f;
    for (uint32_t ch = 0; ch < 2; ch++) {
        float acc[BLOCK];
        comb_group(&FV->comb[ch][0], buf[ch], &FV->comb_store[ch][0], mono, acc, 1, feedback, damp1, damp2);
        comb_group(&FV->comb[ch][GROUP], buf[ch], &FV->comb_store[ch][GROUP], mono, acc, 0, feedback, damp1, damp2);
        allpass_chain(FV->allpass[ch], buf[ch], acc, ap_feedback);
        int16_t *s = io[ch];
        for (uint32_t n = 0; n < BLOCK; n++) {
            float wet = (float)acc[n] * wet_gain, dry = (float)s[n];
            if (!muted)
                s[n] = sat16(dry + (wet - dry) * amount);
            level += wet < 0 ? -wet : wet;
        }
    }
    return level / BLOCK;
}

#endif
