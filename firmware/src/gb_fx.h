/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects, as guitar pedals. Included by quartet.c, before gb_clock.h, which renders each track into tb[track]
 * and then calls fx_process().
 *
 * Each track has five pedal slots (the top buttons FX SEL ENV LFO EDIT); each slot holds any pedal of the library
 * (set.ped[track][slot]) with three knobs (set.pk[track][slot][0..2], 0..127 or a choice), on when bit slot of
 * set.fx[track].on is set. A pedal nobody has switched on costs nothing.
 *
 *   DRIVE       DISTORTION, OVERDRIVE, FUZZ, OCTAVE FUZZ, GATED FUZZ, WAVEFOLDER      in the slot, per track
 *   FILTER      FILTER SEQ (on the tempo), WAH (follows how loud you play)          in the slot, per track
 *   MODULATION  CHORUS, PHASER, FLANGER, TREMOLO, SLICER (on the tempo), VIBRATO, UNI   in the slot, per track
 *   SPACE       REVERB, DELAY, TAPE ECHO, SHIMMER    one of each, shared: a track's first knob is what it sends;
 *               the other two knobs belong to the shared pedal (set.spk[pedal][0..1]), for every track using it
 *   OTHER       CASSETTE, VINYL, GLITCH (on the tempo), UNDERWATER, SYNTH (a square wave under the notes)
 *
 * Speed: each pedal is its own tight loop over the whole block; a send bus is filled only from the slots that use
 * it; the reverb and the shimmer run at half rate. The time-based pedals take their timing from the clock (exact).
 * Integer only; the big buffers are in the pool (zeroed at boot). A slot's state starts afresh when it is switched
 * on or given another pedal. */

enum { E_REV, E_DLY, E_DST, E_CHO, E_PHS, NFX };    /* the slots (FX, SEL, ENV, LFO, EDIT) and the pedal each held before
                                                   the pedals could be chosen (an older save loads them so) */
typedef char nfx_matches_the_save[NFX == PEDAL_SLOTS ? 1 : -1];
enum { PD_DIST, PD_OD, PD_FUZZ, PD_OCTF, PD_GATEF, PD_FOLD,              /* DRIVE */
       PD_FSEQ, PD_WAH,                                                  /* FILTER */
       PD_CHO, PD_PHS, PD_FLG, PD_TREM, PD_SLICE, PD_VIB, PD_UNI,        /* MODULATION */
       PD_REV, PD_DLY, PD_TAPE, PD_SHIM,                                 /* SPACE (shared) */
       PD_CASS, PD_VINYL, PD_GLITCH, PD_WATER, PD_SYNTH,                 /* OTHER */
       NPD };
enum { SP_REV, SP_DLY, SP_TAPE, SP_SHIM };      /* the shared pedals (and their send buses) */
typedef char nsp_matches_the_save[SP_SHIM + 1 == NSPACE ? 1 : -1];
#define PD_SPACE(p) ((p) >= PD_REV && (p) <= PD_SHIM)

static int32_t tb[NTRK][FXN];                   /* each track's sound for this block, mono */
static int32_t fx_bus[NSPACE][FXN] __attribute__((section(".pool")));   /* what the tracks send to each shared pedal (x 128) */
static uint32_t fxc_run, fxc_tick, fxc_pos, fxc_len;   /* the clock at the block's start (gb_clock.h writes) */

static int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
static int32_t clampv(int32_t v, int32_t m) { return v > m ? m : v < -m ? -m : v; }
static int32_t absv(int32_t v) { return v < 0 ? -v : v; }
static int32_t tri16(uint32_t ph) { int32_t s = (int32_t)ph; return ((s ^ (s >> 31)) >> 15) - 32768; }   /* -32768..32767 */

/* ---- the shared pedals ---- */
/* DELAY: 65536 samples (1.49 s at 44.1 kHz); a time that does not fit is halved until it does */
#define DLY_N 65536u
static int16_t dly_buf[DLY_N] __attribute__((section(".pool")));
static uint32_t dly_w;
static int32_t dly_lp;
/* the repeat rates, in 16ths x 3: 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2 */
static const uint8_t DLY_DIV3[7] = {3, 4, 6, 9, 12, 18, 24};
static const char *const DLY_NAME[7] = {"1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2"};
#define NDLY 7u
#define DLY_RETURN 100                          /* the echoes' loudness against the dry sound (/128) */

/* TAPE ECHO: its own 16384 samples (0.37 s); longer times are halved until they fit */
#define TAPE_N 16384u
static int16_t tape_buf[TAPE_N] __attribute__((section(".pool")));
static uint32_t tape_w, tape_ph;
static int32_t tape_lp, tape_lp2;

/* REVERB: 4 combs + 2 allpasses (+1 for the right side), at 22.05 kHz (every other sample). Lengths: Freeverb's,
 * halved for the lower rate. TIME sets the feedback and the room size together; TONE how dark the echoes get */
static const uint16_t REV_COMB[4] = {558, 594, 638, 678};
static const uint16_t REV_AP[3] = {278, 220, 170};
static int16_t rev_c0[558], rev_c1[594], rev_c2[638], rev_c3[678], rev_a0[278], rev_a1[220], rev_a2[170];
static int16_t *const REV_CB[4] = {rev_c0, rev_c1, rev_c2, rev_c3};
static int16_t *const REV_AB[3] = {rev_a0, rev_a1, rev_a2};
static uint32_t rev_ci[4], rev_ai[3];
static int32_t rev_lp[4];
static int32_t rev_x[FXN / 2u + 1u], rev_l[FXN / 2u + 1u], rev_r[FXN / 2u + 1u];   /* a block at the half rate */
#define REV_RETURN 110

/* SHIMMER: a reverb whose echoes go through an octave-up pitch shifter and back in: each repeat a little higher,
 * an angelic shine. Its own 4 combs and the shifter's line, at half rate (two read heads that cross-fade) */
static const uint16_t SHM_COMB[4] = {601, 653, 709, 761};
static int16_t shm_c0[601] __attribute__((section(".pool"))), shm_c1[653] __attribute__((section(".pool")));
static int16_t shm_c2[709] __attribute__((section(".pool"))), shm_c3[761] __attribute__((section(".pool")));
static int16_t *const SHM_CB[4] = {shm_c0, shm_c1, shm_c2, shm_c3};
#define SHM_PS 1024u                            /* the shifter's line (half rate) and its window */
static int16_t shm_ps[SHM_PS] __attribute__((section(".pool")));
static uint32_t shm_ci[4], shm_pw, shm_pp;
static int32_t shm_lp[4], shm_last, shm_dc;

static void delay_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t dtime, uint32_t dfb)   /* repeats that get darker */
{
    uint32_t len = 220500u * DLY_DIV3[dtime % NDLY] / set.bpm, i, w = dly_w;   /* exact from the tempo: on the beat */
    int32_t fb = (int32_t)dfb * 115 / 127, lp = dly_lp;   /* INTENSITY: the repeats' feedback, up to ~0.9 */
    while (len >= DLY_N) len >>= 1;
    for (i = 0; i < n; i++) {
        int32_t y = dly_buf[(w - len) & (DLY_N - 1u)];
        lp += (y - lp) >> 2;
        dly_buf[w] = sat16((in[i] >> 7) + ((lp * fb) >> 7));
        w = (w + 1u) & (DLY_N - 1u);
        y = (y * DLY_RETURN) >> 7;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
    dly_w = w; dly_lp = lp;
}

static void tape_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t ttime, uint32_t tfb)   /* TAPE ECHO */
{
    uint32_t len = 220500u * DLY_DIV3[ttime % NDLY] / set.bpm, i, w = tape_w;
    int32_t fb = (int32_t)tfb * 118 / 127, lp = tape_lp, lp2 = tape_lp2, d, d1, sd;
    while (len >= TAPE_N - 64u) len >>= 1;
    d = (int32_t)(len << 8) + (sine(tape_ph) >> 5);   /* the tape's wow: a slow wobble of the time, ~ +-4 samples */
    tape_ph += LFO_INC[22] * n;
    d1 = (int32_t)(len << 8) + (sine(tape_ph) >> 5);
    sd = (d1 - d) / (int32_t)n;
    for (i = 0; i < n; i++) {
        uint32_t a = w - (uint32_t)(d >> 8);
        int32_t v0 = tape_buf[a & (TAPE_N - 1u)], v1 = tape_buf[(a - 1u) & (TAPE_N - 1u)], y, f;
        y = v0 + (((v1 - v0) * (d & 255)) >> 8);
        lp += (y - lp) >> 3; lp2 += (lp - lp2) >> 2;   /* darker each time round */
        f = clampv((lp2 * fb) >> 7, 32767);
        f = (3 * f - ((((f * f) >> 15) * f) >> 15)) >> 1;   /* the tape saturates (round) */
        tape_buf[w] = sat16((in[i] >> 7) + f);
        w = (w + 1u) & (TAPE_N - 1u);
        d += sd;
        y = (y * 105) >> 7;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
    tape_w = w; tape_lp = lp; tape_lp2 = lp2;
}

static void reverb_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t rtone, uint32_t rtime)   /* REVERB, at half rate */
{
    uint32_t m = n >> 1, j, k;                  /* (n is even: the audio block is 256) */
    int32_t fb = 22938 + (int32_t)rtime * 72;   /* TIME: feedback 0.70 .. 0.98 ... */
    int32_t damp = 6000 + (int32_t)rtone * 195; /* TONE: the echoes dark .. bright (195: the product stays under 2^31) */
    for (j = 0; j < m; j++) { rev_x[j] = ((in[2u * j] + in[2u * j + 1u]) >> 8) >> 2; rev_l[j] = 0; }   /* two in, one: quieter */
    for (k = 0; k < 4u; k++) {                  /* combs, one line at a time: echoes that fade, darker each time */
        int16_t *buf = REV_CB[k];
        uint32_t len = (REV_COMB[k] * (64u + (rtime >> 1))) >> 7, idx = rev_ci[k];   /* ... and the room 50..100 % */
        int32_t lp = rev_lp[k];
        if (idx >= len) idx = 0;
        for (j = 0; j < m; j++) {
            int32_t y = buf[idx];
            lp += ((y - lp) * damp) >> 15;
            buf[idx] = sat16(rev_x[j] + ((lp * fb) >> 15));
            if (++idx >= len) idx = 0;
            rev_l[j] += y;
        }
        rev_ci[k] = idx; rev_lp[k] = lp;
    }
    for (k = 0; k < 2u; k++) {                  /* allpasses: smear them */
        int16_t *buf = REV_AB[k];
        uint32_t len = REV_AP[k], idx = rev_ai[k];
        for (j = 0; j < m; j++) {
            int32_t v = k ? rev_l[j] : rev_l[j] >> 1, bo = buf[idx];
            buf[idx] = sat16(v + (bo >> 1));
            rev_l[j] = bo - v;
            if (++idx >= len) idx = 0;
        }
        rev_ai[k] = idx;
    }
    {                                           /* one more for the right side: the two sides differ, it sounds wide */
        int16_t *buf = REV_AB[2];
        uint32_t len = REV_AP[2], idx = rev_ai[2];
        for (j = 0; j < m; j++) {
            int32_t v = rev_l[j], bo = buf[idx];
            buf[idx] = sat16(v + (bo >> 1));
            rev_r[j] = bo - v;
            if (++idx >= len) idx = 0;
        }
        rev_ai[2] = idx;
    }
    for (j = 0; j < m; j++) {                   /* back to the full rate (each value twice) */
        int32_t l = (rev_l[j] * REV_RETURN) >> 7, r = (rev_r[j] * REV_RETURN) >> 7;
        out[4u * j] += l; out[4u * j + 1u] += r;
        out[4u * j + 2u] += l; out[4u * j + 3u] += r;
    }
}

static void shimmer_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t stime, uint32_t samt)   /* SHIMMER, half rate */
{
    uint32_t m = n >> 1, j, k, pw = shm_pw, pp = shm_pp;
    int32_t fb = 24000 + (int32_t)stime * 64;   /* TIME: feedback 0.73 .. 0.98 */
    int32_t sh = (int32_t)samt * 100 / 127, last = shm_last, dc = shm_dc;   /* SHIMMER: how much of the tail goes up an octave again */
    for (j = 0; j < m; j++) {                   /* the shifter: what comes in, and the tail, an octave up */
        uint32_t pa = pp, pb = (pp + SHM_PS / 2u) & (SHM_PS - 1u);
        int32_t ga = pa < SHM_PS / 2u ? (int32_t)pa : (int32_t)(SHM_PS - pa), gb = (int32_t)(SHM_PS / 2u) - ga, s;
        int32_t x = ((in[2u * j] + in[2u * j + 1u]) >> 8) >> 2;
        shm_ps[pw] = sat16(x + ((last * sh) >> 7));
        s = (shm_ps[(pw - (SHM_PS - pa)) & (SHM_PS - 1u)] * ga + shm_ps[(pw - (SHM_PS - pb)) & (SHM_PS - 1u)] * gb) >> 9;
        pw = (pw + 1u) & (SHM_PS - 1u); pp = (pp + 1u) & (SHM_PS - 1u);
        rev_x[j] = (x >> 1) + (s >> 1);
        rev_l[j] = 0; rev_r[j] = 0;
    }
    for (k = 0; k < 4u; k++) {                  /* the tank: four combs, two to each side */
        int16_t *buf = SHM_CB[k];
        uint32_t len = SHM_COMB[k], idx = shm_ci[k];
        int32_t lp = shm_lp[k], *o = k & 1u ? rev_r : rev_l;
        for (j = 0; j < m; j++) {
            int32_t y = buf[idx];
            lp += ((y - lp) * 18000) >> 15;
            buf[idx] = sat16(rev_x[j] + ((lp * fb) >> 15));
            if (++idx >= len) idx = 0;
            o[j] += y;
        }
        shm_ci[k] = idx; shm_lp[k] = lp;
    }
    for (j = 0; j < m; j++) {
        int32_t l = (rev_l[j] * 100) >> 7, r = (rev_r[j] * 100) >> 7;
        last = (rev_l[j] + rev_r[j]) >> 3;
        dc += (last - dc) >> 6; last -= dc;     /* (no DC round the loop: a comb's DC gain is ~1 / (1 - feedback)) */
        out[4u * j] += l; out[4u * j + 1u] += r;
        out[4u * j + 2u] += l; out[4u * j + 3u] += r;
    }
    shm_pw = pw; shm_pp = pp; shm_last = last; shm_dc = dc;
}

/* ---- the pedals in a slot: each slot its own state and a short line (chorus, flanger, vibrato, tape wobble) ---- */
typedef struct {
    int32_t s[12];                              /* filters, followers, allpasses: each pedal its own use */
    uint32_t ph, ph2, rnd, w, cnt, cpos;        /* LFOs, noise, the line's write place, a free-running clock */
    int32_t dl;                                 /* a moving tap's place (Q8), glided block to block */
    uint8_t type, live;                         /* the pedal this state is for (+1); seen this block */
} pst_t;
static pst_t pst[NTRK][NFX] __attribute__((section(".pool")));
#define PB_N 512u                               /* 11.6 ms */
static int16_t pbuf[NTRK][NFX][PB_N] __attribute__((section(".pool")));
#define GL_N 4096u                              /* GLITCH: the track's last 93 ms */
static int16_t glbuf[NTRK][GL_N] __attribute__((section(".pool")));

static int32_t tap(const int16_t *b, uint32_t mask, uint32_t w, int32_t d)   /* a line read d (Q8) samples back */
{
    uint32_t a = w - (uint32_t)(d >> 8);
    int32_t v0 = b[a & mask], v1 = b[(a - 1u) & mask];
    return v0 + (((v1 - v0) * (d & 255)) >> 8);
}
static uint32_t rnd_next(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r; }

/* the clock for the synced pedals: the tick and where in it, sample by sample (stopped: a clock of its own) */
typedef struct { uint32_t tk, p, len; } sync_t;
static void sync_begin(sync_t *c, const pst_t *st)
{
    c->len = fxc_len > 256u ? fxc_len : 256u;
    if (fxc_run) { c->tk = fxc_tick; c->p = fxc_pos; } else { c->tk = st->cnt; c->p = st->cpos; }
}
static int sync_step(sync_t *c) { c->p += 256u; if (c->p >= c->len) { c->p -= c->len; c->tk++; return 1; } return 0; }
static void sync_end(const sync_t *c, pst_t *st) { if (!fxc_run) { st->cnt = c->tk; st->cpos = c->p; } }

/* the drives: gain, a curve, a tone filter, then a volume that keeps the loudness near the clean sound's */
static void drive_block(uint32_t type, pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)
{
    int32_t lvl = k[0], tone = k[1], amt = k[2], lp = st->s[0], dc = st->s[1], env = st->s[2], g = st->s[3], hp = st->s[4];
    int32_t kt = 2000 + tone * 230, gain, vol;  /* (tone 230: the product stays under 2^31, as the distortion's) */
    uint32_t i;
    if (type == PD_GATEF) kt = 2000 + 70 * 230; /* (its middle knob is the gate) */
    switch (type) {
    case PD_DIST: gain = 16 + amt * 4; vol = ((5571 + 16286 * 16 / gain) * lvl) >> 6; break;   /* x1 .. x32, 0.67 .. 0.19 */
    case PD_OD: gain = 16 + amt * 2; vol = ((5243 + 12714 * 16 / gain) * lvl) >> 6; break;     /* x1 .. x17, soft */
    case PD_FOLD: gain = 16 + amt * 4; vol = (4200 * lvl) >> 6; break;
    default: gain = 32 + amt * 6; vol = (5200 * lvl) >> 6; break;    /* the fuzzes: x2 .. x49, clipped hard */
    }
    for (i = 0; i < n; i++) {
        int32_t v = x[i], a;
        switch (type) {
        case PD_DIST:                           /* round clipping (1.5x - 0.5x^3) */
            v = clampv((v * gain) >> 4, 32767);
            v = (3 * v - ((v * ((v * v) >> 15)) >> 15)) >> 1;
            break;
        case PD_OD:                             /* soft and a little lopsided, like a tube: x / (1 + |x|) */
            v = clampv((v * gain) >> 4, 98301);
            if (v < 0) v = (v * 26) >> 5;
            v = v * 16384 / ((32768 + absv(v)) >> 1);   /* (32-bit: no 64-bit division on this chip) */
            break;
        case PD_FUZZ:
            v = clampv((v * gain) >> 4, 12000);
            break;
        case PD_OCTF:                           /* the wave folded up (|x|): an octave higher, then fuzzed */
            a = absv(v); dc += (a - dc) >> 8; v = a - dc;
            v = clampv((v * gain) >> 3, 12000);
            break;
        case PD_GATEF:                          /* lopsided fuzz that splutters and cuts out as the note dies */
            a = absv(v); env += (a - env) >> 7;
            v = (v * gain) >> 4;
            v = v > 12000 ? 12000 : v < -5000 ? -5000 : v;
            dc += (v - dc) >> 9; v -= dc;
            g += (((env > tone * 6 ? 32767 : 0) - g) >> 6);   /* GATE: below it, silence */
            v = (v * g) >> 15;
            break;
        default: {                              /* WAVEFOLDER: past the top, it folds back down (a triangle) */
            int32_t u = ((v * gain) >> 4) + 16384, mm = u & 65535;
            v = mm < 32768 ? mm - 16384 : 49151 - mm;
            v <<= 1;
        }
        }
        lp += ((v - lp) * kt) >> 15;
        hp += (lp - hp) >> 8;                   /* (a DC blocker: the lopsided curves leave an offset) */
        x[i] = ((lp - hp) * (vol >> 1)) >> 14;
    }
    st->s[0] = lp; st->s[1] = dc; st->s[2] = env; st->s[3] = g; st->s[4] = hp;
}

/* a state-variable filter (Chamberlin): low, band; f in Q15 (up to 15800), q (damping) in Q14 (up to 31000). The
 * states are held to +-49151 so every product stays under 2^31 */
static void svf(int32_t *low, int32_t *band, int32_t x, int32_t f, int32_t q)
{
    int32_t hi;
    *low = clampv(*low + ((f * *band) >> 15), 49151);
    hi = clampv(clampv(x, 32767) - *low - ((q * *band) >> 14), 49151);
    *band = clampv(*band + ((f * hi) >> 15), 49151);
}

static void filter_block(uint32_t type, pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)
{
    static const int32_t F7[7] = {700, 1170, 2100, 3730, 6060, 9790, 15800};   /* 150 Hz .. 3.4 kHz */
    static const uint8_t SEQ8[8] = {6, 1, 4, 0, 5, 2, 3, 1};
    int32_t low = st->s[0], band = st->s[1], f = st->s[2], env = st->s[3];
    uint32_t i;
    if (type == PD_FSEQ) {                      /* FILTER SEQ: RATE (1/4, 1/8, 1/16), DEPTH, RES */
        static const uint8_t TK[3] = {16, 8, 4};
        int32_t q = 31000 - (int32_t)k[2] * 200;   /* RES: damping 1.9 .. 0.34 */
        uint32_t per = TK[k[0] % 3u];
        sync_t c;
        sync_begin(&c, st);
        if (!f) f = F7[6];
        for (i = 0; i < n; i++) {
            uint32_t p = SEQ8[(c.tk / per) & 7u];
            int32_t tgt = 15800 - (15800 - F7[p]) * (int32_t)k[1] / 127;
            sync_step(&c);
            f += (tgt - f) >> 7;                /* glide a little: no clicks */
            svf(&low, &band, x[i], f, q);
            x[i] = low;
        }
        sync_end(&c, st);
    } else {                                    /* WAH: SENS, RANGE, RES; the filter opens as you play louder */
        int32_t q = 25000 - (int32_t)k[2] * 150, span = (12900 - 2300) * (int32_t)k[1] / 127;   /* (q: 1.5 .. 0.36) */
        for (i = 0; i < n; i++) {
            int32_t a = absv(x[i]), pos;
            env += (a - env) >> (a > env ? 5 : 10);
            pos = (env * ((int32_t)k[0] + 1)) >> 5;
            pos = pos > 32767 ? 32767 : pos;
            f = 2300 + ((span * pos) >> 15);
            svf(&low, &band, x[i], f, q);
            x[i] = (band * q) >> 13;   /* (the band's peak back to about the sound's level) */
        }
    }
    st->s[0] = low; st->s[1] = band; st->s[2] = f; st->s[3] = env;
}

/* the modulations: chorus, flanger, vibrato read a line with a moving tap; phaser and uni sweep allpasses */
static void mod_block(uint32_t type, pst_t *st, int16_t *b, const uint8_t *k, int32_t *x, uint32_t n)
{
    uint32_t i, w = st->w, kk;
    int32_t pl = st->dl, d, sd;
    switch (type) {
    case PD_CHO: {                              /* CHORUS: RATE, DEPTH, LEVEL; two taps swinging opposite */
        int32_t s, lv = k[2], d2, p2 = st->s[0];
        st->ph += LFO_INC[30u + (k[0] * 70u >> 7)] * n;
        s = sine(st->ph);
        d = (300 << 8) + ((s * (int32_t)k[1]) >> 7); d2 = (300 << 8) - ((s * (int32_t)k[1]) >> 7);
        if (!pl) pl = d, p2 = d2;
        sd = (d - pl) / (int32_t)n;
        {
            int32_t sd2 = (d2 - p2) / (int32_t)n;
            for (i = 0; i < n; i++) {
                int32_t wet;
                b[w] = sat16(x[i]);
                pl += sd; p2 += sd2;
                wet = (tap(b, PB_N - 1u, w, pl) + tap(b, PB_N - 1u, w, p2)) >> 1;
                w = (w + 1u) & (PB_N - 1u);
                x[i] = ((x[i] * (128 - lv / 3) + wet * lv) >> 7);
            }
        }
        st->s[0] = p2;
        break;
    }
    case PD_FLG: {                              /* FLANGER: RATE, DEPTH, FEEDBACK; a very short echo that sweeps */
        int32_t fb = (int32_t)k[2] * 108 / 127;
        st->ph += LFO_INC[8u + (k[0] * 70u >> 7)] * n;
        d = (12 << 8) + (((tri16(st->ph) + 32768) * ((int32_t)k[1] * 11 / 8)) >> 8);   /* 12 .. ~185 samples */
        if (!pl) pl = d;
        sd = (d - pl) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t y;
            pl += sd;
            y = tap(b, PB_N - 1u, w, pl);
            b[w] = sat16(x[i] + ((y * fb) >> 7));
            w = (w + 1u) & (PB_N - 1u);
            x[i] = ((x[i] + y) * 80) >> 7;
        }
        break;
    }
    case PD_VIB: {                              /* VIBRATO: RATE, DEPTH, MIX; the pitch wobbles */
        int32_t mx = k[2];
        st->ph += LFO_INC[20u + (k[0] * 90u >> 7)] * n;
        d = (160 << 8) + ((sine(st->ph) * (int32_t)k[1]) >> 7);
        if (!pl) pl = d;
        sd = (d - pl) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t y;
            b[w] = sat16(x[i]);
            pl += sd;
            y = tap(b, PB_N - 1u, w, pl);
            w = (w + 1u) & (PB_N - 1u);
            x[i] += ((y - x[i]) * mx) >> 7;
        }
        break;
    }
    default: {                                  /* PHASER (RATE, DEPTH, FEEDBACK) and UNI (RATE, DEPTH, MIX) */
        static const int32_t UNI_BASE[4] = {6000, 8000, 10500, 13000};
        uint32_t uni = type == PD_UNI, inc = uni ? LFO_INC[12u + (k[0] * 80u >> 7)] : LFO_INC[10u + (k[0] * 85u >> 7)], ph = st->ph;
        int32_t span = uni ? (int32_t)k[1] * 4000 / 127 : (int32_t)k[1] * 5734 / 127;
        int32_t fbk = uni ? 0 : (int32_t)k[2] * 100 / 127, mx = k[2];
        int32_t comp = uni ? 128 : 128 - (int32_t)k[2] * (int32_t)k[2] * 38 / 16129;
        int32_t a[4] = {9011, 9011, 9011, 9011}, last = st->s[8], *x1 = st->s, *y1 = st->s + 4;
        for (i = 0; i < n; i++) {
            int32_t v;
            if ((i & 15u) == 0u) {              /* every 16 samples: the sweep */
                int32_t s;
                ph += inc * 16u;
                if (uni) {                      /* a lamp's glow: lingers at the dark end */
                    s = sine(ph) + 32768; s = ((s >> 1) * (s >> 1)) >> 14;   /* 0 .. 65535 */
                    for (kk = 0; kk < 4u; kk++) a[kk] = UNI_BASE[kk] + (((s - 32768) * span) >> 15);
                } else {
                    s = tri16(ph);
                    a[0] = 9011 + ((s * span) >> 15);
                    a[1] = a[2] = a[3] = a[0];
                }
            }
            v = clampv(x[i] + ((fbk * last) >> 7), 32767);
            for (kk = 0; kk < 4u; kk++) {       /* y = a (y1 - x) + x1 */
                int32_t y = ((a[kk] * (y1[kk] - v)) >> 14) + x1[kk];
                x1[kk] = v; y1[kk] = y; v = y;
            }
            last = v;
            x[i] = uni ? x[i] + (((v - x[i]) * mx) >> 7) : (((x[i] + v) >> 1) * comp) >> 7;
        }
        st->ph = ph; st->s[8] = last;
    }
    }
    st->w = w; st->dl = pl;
}

static void amp_block(uint32_t type, pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)   /* TREMOLO, SLICER */
{
    uint32_t i;
    int32_t g = st->s[0];
    if (type == PD_TREM) {                      /* RATE, DEPTH, SHAPE (round .. square) */
        uint32_t inc = LFO_INC[20u + (k[0] * 90u >> 7)];
        for (i = 0; i < n; i++) {
            if ((i & 15u) == 0u) {
                int32_t s, sq;
                st->ph += inc * 16u;
                s = sine(st->ph); sq = s < 0 ? -32767 : 32767;
                s += ((sq - s) * (int32_t)k[2]) >> 7;
                g = 32767 - (((32767 - s) >> 1) * (int32_t)k[1] >> 7);
            }
            x[i] = (x[i] * g) >> 15;
        }
    } else {                                    /* SLICER: RATE (1/8, 1/16, 1/32), DEPTH, PATTERN (1-8), on the tempo */
        static const uint16_t PAT[8] = {0x5555, 0x7777, 0x3333, 0x6DB6, 0x0F0F, 0xB5AD, 0x1111, 0x7F7F};
        static const uint8_t TK[3] = {8, 4, 2};
        uint32_t per = TK[k[0] % 3u], pat = PAT[k[2] & 7u];
        int32_t lo = 32767 - (32767 * (int32_t)k[1]) / 127;
        sync_t c;
        sync_begin(&c, st);
        for (i = 0; i < n; i++) {
            uint32_t sl = (c.tk / per) & 15u;
            int32_t tgt = pat >> sl & 1u ? 32767 : lo;
            sync_step(&c);
            g += (tgt - g) >> 5;                /* (a soft edge: no clicks) */
            x[i] = (x[i] * g) >> 15;
        }
        sync_end(&c, st);
    }
    st->s[0] = g;
}

static void other_block(uint32_t t, uint32_t type, pst_t *st, int16_t *b, const uint8_t *k, int32_t *x, uint32_t n)
{
    uint32_t i, w = st->w;
    int32_t pl = st->dl, d, sd, lp = st->s[0], lp2 = st->s[1], c3 = st->s[2], c4 = st->s[3];
    switch (type) {
    case PD_CASS: {                             /* CASSETTE: WOW, HISS, TONE; a worn tape: wobbly, dark, hissing */
        int32_t kt = 3000 + (int32_t)k[2] * 150;
        st->ph += LFO_INC[14] * n; st->ph2 += LFO_INC[60] * n;
        d = (150 << 8) + ((sine(st->ph) * (int32_t)k[0]) >> 8) + ((sine(st->ph2) * (int32_t)k[0]) >> 10);
        if (!pl) pl = d;
        sd = (d - pl) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t y, nz;
            b[w] = sat16(x[i]);
            pl += sd;
            y = tap(b, PB_N - 1u, w, pl);
            w = (w + 1u) & (PB_N - 1u);
            lp += ((y - lp) * kt) >> 15; lp2 += ((lp - lp2) * kt) >> 15;
            nz = (int32_t)(rnd_next(&st->rnd) >> 17) - 16384;
            y = clampv(lp2 + ((nz * (int32_t)k[1]) >> 13), 32767);
            x[i] = (3 * y - ((y * ((y * y) >> 15)) >> 15)) >> 1;   /* (the tape squashes the peaks) */
        }
        break;
    }
    case PD_VINYL: {                            /* VINYL: CRACKLE, AGE, LEVEL; clicks, rumble, an old narrow sound */
        int32_t khp = 300 + (int32_t)k[1] * 20, klp = 26000 - (int32_t)k[1] * 160, lv = k[2];
        uint32_t chance = (uint32_t)k[0] * 2u;
        for (i = 0; i < n; i++) {
            uint32_t r = rnd_next(&st->rnd);
            int32_t v = x[i];
            c4 += (v - c4) * khp >> 15; v -= c4;          /* the bass thinned */
            lp += ((v - lp) * klp) >> 15;                 /* the top dulled */
            if ((r >> 16) < chance) {           /* a click, either way, of any size */
                uint32_t r2 = rnd_next(&st->rnd);
                c3 = (int32_t)((r2 >> 8) & 0xFFF) + 800;
                if (r2 >> 31) c3 = -c3;
            }
            c3 = (c3 * 21) / 32;                /* (divided, not shifted: it dies away to 0 either way) */
            lp2 += (((int32_t)(r >> 20) - 2048) - lp2 + 256) >> 9;   /* rumble (+256: rounded, no drift) */
            x[i] = ((lp + c3 + ((lp2 * (int32_t)k[1]) >> 5)) * lv) >> 6;
        }
        break;
    }
    case PD_WATER: {                            /* UNDERWATER: DEPTH, WOBBLE, LEVEL; muffled and drifting */
        int32_t kt = 6000 - (int32_t)k[0] * 40, lv = ((int32_t)k[2] * (127 + k[0])) >> 7;
        st->ph += LFO_INC[16] * n;
        d = (200 << 8) + ((sine(st->ph) * (int32_t)k[1]) >> 7);
        if (!pl) pl = d;
        sd = (d - pl) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t y;
            b[w] = sat16(x[i]);
            pl += sd;
            y = tap(b, PB_N - 1u, w, pl);
            w = (w + 1u) & (PB_N - 1u);
            lp += ((y - lp) * kt) >> 15; lp2 += ((lp - lp2) * kt) >> 15;
            x[i] = (lp2 * lv) >> 6;
        }
        break;
    }
    case PD_SYNTH: {                            /* SYNTH: OCTAVE (0, -1, -2), TONE, MIX; a square wave on the lowest note */
        uint32_t inc = 0, v, low = 128, oc = k[0] % 3u;
        int32_t kt = 1500 + (int32_t)k[1] * 200, mx = k[2];
        if (t < NSYN) {
            for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++)
                if (vx[v].gate && vx[v].note < low) { low = vx[v].note; inc = vx[v].inc; }
        }
        if (!inc) inc = st->cnt ? st->cnt : INC_C9[0] >> 7;   /* (no note held: the last one; drums: a low C) */
        st->cnt = inc;
        inc >>= oc;
        for (i = 0; i < n; i++) {
            int32_t a = absv(x[i]), y;
            c3 += (a - c3) >> 6;                /* how loud the track plays: the synth follows it */
            st->ph += inc;
            y = (int32_t)st->ph < 0 ? -c3 : c3;
            lp += ((y * 2 - lp) * kt) >> 15;
            x[i] += ((lp - x[i]) * mx) >> 7;
        }
        break;
    }
    default: {                                  /* GLITCH: CHANCE, LENGTH (1/32, 1/16, 1/8), MIX; on a step, now and then,
                                                   a slice of what just played repeats, plays backwards or stutters */
        static const uint8_t TK[3] = {2, 4, 8};
        int16_t *g = glbuf[t];
        uint32_t L = (fxc_len * TK[k[1] % 3u]) >> 8, act = (uint32_t)st->s[4], mode = (uint32_t)st->s[5];
        uint32_t st0 = (uint32_t)st->s[6], rp = (uint32_t)st->s[7], gw = (uint32_t)st->s[8];
        int32_t mx = k[2];
        sync_t c;
        if (L < 64u) L = 64u;
        if (L > GL_N - 1u) L = GL_N - 1u;
        sync_begin(&c, st);
        for (i = 0; i < n; i++) {
            int32_t y;
            if (sync_step(&c) && c.tk % TPS == 0u && fxc_run) {   /* a new step: glitch it? */
                uint32_t r = rnd_next(&st->rnd);
                act = (r >> 24) < (uint32_t)k[0] * 2u;
                if (act) { mode = (r >> 8) % 3u; st0 = gw - L; rp = 0; }
            }
            if (act) {
                uint32_t q = rp % L;
                y = g[(st0 + (mode == 1u ? L - 1u - q : q)) & (GL_N - 1u)];
                if (mode == 2u && q > L / 2u) y = 0;
                rp++;
                x[i] += ((y - x[i]) * mx) >> 7;
            } else { g[gw] = sat16(x[i]); gw = (gw + 1u) & (GL_N - 1u); }   /* (it listens while not glitching) */
        }
        sync_end(&c, st);
        st->s[4] = (int32_t)act; st->s[5] = (int32_t)mode; st->s[6] = (int32_t)st0; st->s[7] = (int32_t)rp; st->s[8] = (int32_t)gw;
    }
    }
    st->w = w; st->dl = pl; st->s[0] = lp; st->s[1] = lp2; st->s[2] = c3; st->s[3] = c4;
}

static void ped_insert(uint32_t t, uint32_t e, uint32_t type, const uint8_t *k, int32_t *x, uint32_t n)
{
    pst_t *st = &pst[t][e];
    if (st->type != type + 1u || !st->live) {   /* switched on, or another pedal: start afresh */
        uint32_t i;
        int16_t *b = pbuf[t][e];
        uint8_t *z = (uint8_t *)st;
        for (i = 0; i < sizeof *st; i++) z[i] = 0;
        for (i = 0; i < PB_N; i++) b[i] = 0;
        st->type = (uint8_t)(type + 1u);
        st->rnd = 0x9E3779B9u * (t * NFX + e + 1u);
    }
    st->live = 1;
    if (type <= PD_FOLD) drive_block(type, st, k, x, n);
    else if (type <= PD_WAH) filter_block(type, st, k, x, n);
    else if (type == PD_TREM || type == PD_SLICE) amp_block(type, st, k, x, n);
    else if (type <= PD_UNI) mod_block(type, st, pbuf[t][e], k, x, n);
    else other_block(t, type, st, pbuf[t][e], k, x, n);
}

static void fx_process(int32_t *out, uint32_t n)
{
    uint32_t i, t, e, b, used = 0;
    n &= ~1u;                                   /* (the reverb takes pairs; the block is 256) */
    for (t = 0; t < NTRK; t++) {                /* 1. each track through its own pedals, slot by slot, in place */
        uint32_t on = set.fx[t].on;
        int32_t gv = (int32_t)set.vol[t] * 4096 / 100;   /* the mixer: 100 = as is (Q12), 127 = x1.27, after the pedals */
        for (e = 0; e < NFX; e++) {
            uint32_t p = set.ped[t][e] % NPD;
            if (on >> e & 1u && !PD_SPACE(p)) ped_insert(t, e, p, set.pk[t][e], tb[t], n);
            else pst[t][e].live = 0;
        }
        if (gv != 4096) {
            int32_t *x = tb[t];
            for (i = 0; i < n; i++) x[i] = (x[i] * gv) >> 12;
        }
    }
    for (b = 0; b < NSPACE; b++) {              /* 2. the send buses, from the slots that use them only */
        int32_t *bus = fx_bus[b];
        uint32_t first = 1;
        for (t = 0; t < NTRK; t++)
            for (e = 0; e < NFX; e++) {
                int32_t s = set.pk[t][e][0];
                const int32_t *x = tb[t];
                if (!(set.fx[t].on >> e & 1u) || set.ped[t][e] % NPD != PD_REV + b || !s) continue;
                if (first) { for (i = 0; i < n; i++) bus[i] = x[i] * s; first = 0; }
                else for (i = 0; i < n; i++) bus[i] += x[i] * s;
            }
        if (!first) used |= 1u << b;
    }
    for (i = 0; i < n; i++) {                   /* 3. the dry mix */
        int32_t d = tb[0][i] + tb[1][i] + tb[2][i] + tb[3][i];
        out[2u * i] += d;
        out[2u * i + 1u] += d;
    }
    if (used >> SP_DLY & 1u) delay_block(fx_bus[SP_DLY], out, n, set.spk[SP_DLY][0], set.spk[SP_DLY][1]);   /* 4. the shared ones */
    if (used >> SP_TAPE & 1u) tape_block(fx_bus[SP_TAPE], out, n, set.spk[SP_TAPE][0], set.spk[SP_TAPE][1]);
    if (used >> SP_REV & 1u) reverb_block(fx_bus[SP_REV], out, n, set.spk[SP_REV][0], set.spk[SP_REV][1]);
    if (used >> SP_SHIM & 1u) shimmer_block(fx_bus[SP_SHIM], out, n, set.spk[SP_SHIM][0], set.spk[SP_SHIM][1]);
}
