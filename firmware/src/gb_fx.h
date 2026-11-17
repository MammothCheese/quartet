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
 *   (and PUMP, in MODULATION: ducks its track on every kick, or on the beat: the sidechain "breathing")
 *   later: BITCRUSHER (drive); REVERSE DELAY, SPRING REVERB (space, shared); PITCH: SHIFTER, HARMONIZER; OTHER: LO-FI,
 *   SLOW (no attack: swells), GHOST (a quiet copy just behind), MUSIC BOX (a little bell on each note), BROKEN CLOCK
 *   (timing hiccups: gb_clock.h plays the track's steps late), SHADOW (a darker copy an octave down)
 * STOP silences them all: fx_flush_req makes fx_process fade the shared pedals out over one block, then empty every
 * line, buffer and state (no echo, loop or late note left over, nothing heard again on PLAY). 
 *
 * Speed: each pedal is its own tight loop over the whole block; a send bus is filled only from the slots that use
 * it; the reverb and the shimmer run at half rate. The time-based pedals take their timing from the clock (exact).
 * Integer only; the big buffers are in the pool (zeroed at boot). A slot's state starts afresh when it is switched
 * on or given another pedal. */

enum { E_REV, E_DLY, E_DST, E_CHO, E_PHS, NFX };    /* the slots (FX, SEL, ENV, LFO, EDIT) and the pedal each held before
                                                   the pedals could be chosen (an older save loads them so) */
typedef char nfx_matches_the_save[NFX == PEDAL_SLOTS ? 1 : -1];
#define NPED_SLOTS 4u                           /* the slots in use: FX SEL ENV LFO (the fifth, EDIT's, is kept in the save but
                                                   unused: EDIT is the sound editor) */
enum { PD_DIST, PD_OD, PD_FUZZ, PD_OCTF, PD_GATEF, PD_FOLD,              /* DRIVE */
       PD_FSEQ, PD_WAH,                                                  /* FILTER */
       PD_CHO, PD_PHS, PD_FLG, PD_TREM, PD_SLICE, PD_VIB, PD_UNI,        /* MODULATION */
       PD_REV, PD_DLY, PD_TAPE, PD_SHIM,                                 /* SPACE (shared) */
       PD_CASS, PD_VINYL, PD_GLITCH, PD_WATER, PD_SYNTH,                 /* OTHER */
       PD_PUMP,                                 /* (added later: MODULATION too; new ones go last, saves keep their numbers) */
       PD_CRUSH, PD_REVD, PD_SPRING,            /* (later still: DRIVE, SPACE, SPACE) */
       PD_SHIFT, PD_HARM,                       /* PITCH */
       PD_LOFI, PD_SLOW, PD_GHOST, PD_MBOX, PD_BROKEN, PD_SHADOW,   /* OTHER */
       NPD };
enum { SP_REV, SP_DLY, SP_TAPE, SP_SHIM, SP_REVD, SP_SPRING, NSPACE_ALL };   /* the shared pedals (and their buses); the
                                                   first NSPACE keep their knobs in set.spk, the later ones in set.spk2 */
typedef char nsp_matches_the_save[SP_SHIM + 1 == NSPACE && NSPACE_ALL - NSPACE == 2 ? 1 : -1];
static int32_t sp_of(uint32_t p)                /* a pedal's shared pedal (SP_...), or -1: it is the slot's own */
{
    return p >= PD_REV && p <= PD_SHIM ? (int32_t)(p - PD_REV) : p == PD_REVD ? SP_REVD : p == PD_SPRING ? SP_SPRING : -1;
}
#define PD_SPACE(p) (sp_of(p) >= 0)
static uint8_t *spk_of(uint32_t sp) { return sp < NSPACE ? set.spk[sp] : set.spk2[(sp - NSPACE) % 2u]; }   /* its two knobs */
static volatile uint32_t fx_flush_req;          /* gb_clock.h: the music stopped: every pedal falls silent (fx_process) */

static int32_t tb[NTRK][FXN];                   /* each track's sound for this block, mono */
static int32_t fx_bus[NSPACE_ALL][FXN] __attribute__((section(".pool")));   /* what the tracks send to each shared pedal (x 128) */
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

/* REVERSE DELAY: the last slice (on the tempo) played backwards while the next is recorded: each echo swells towards
 * the note. Kept at a quarter of the rate (it is a soft, swelling sound): 8192 samples, slices up to 0.37 s; fed back,
 * the repeats turn round each time (backwards, forwards ...) */
#define RVD_N 8192u
static int16_t rvd_buf[RVD_N] __attribute__((section(".pool")));
static uint32_t rvd_w, rvd_k, rvd_cs, rvd_q;
static int32_t rvd_acc, rvd_y0, rvd_y1;
static void revdly_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t rtime, uint32_t rfb)
{
    uint32_t N = (220500u * DLY_DIV3[rtime % NDLY] / set.bpm) >> 2, i;
    int32_t fb = (int32_t)rfb * 105 / 127;
    while (N >= RVD_N / 2u) N >>= 1;
    if (N < 128u) N = 128u;
    for (i = 0; i < n; i++) {
        int32_t y;
        rvd_acc += in[i] >> 7;
        if (++rvd_q == 4u) {                    /* a quarter-rate sample: read the last slice backwards, write this one */
            uint32_t k = rvd_k;
            int32_t g = (int32_t)(k < 96u ? k : N - k < 96u ? N - k : 96u);   /* (soft at the slice's two ends) */
            rvd_q = 0;
            y = (rvd_buf[(rvd_cs - 1u - k) & (RVD_N - 1u)] * g) / 96;
            rvd_buf[rvd_w] = sat16((rvd_acc >> 2) + ((y * fb) >> 7));
            rvd_acc = 0;
            rvd_w = (rvd_w + 1u) & (RVD_N - 1u);
            if (++rvd_k >= N) { rvd_k = 0; rvd_cs = rvd_w; }
            rvd_y0 = rvd_y1; rvd_y1 = y;
        }
        y = rvd_y0 + (((rvd_y1 - rvd_y0) * (int32_t)rvd_q) >> 2);   /* (back to the full rate: a straight line) */
        y = (y * 110) >> 7;
        out[2u * i] += y; out[2u * i + 1u] += y;
    }
}

/* SPRING REVERB: a short loop (~30 ms, wobbling a little) through six allpasses that smear each round into a chirp:
 * the "boing" and splash of a spring tank. Half rate; the two sides read the loop at different places */
#define SPR_N 1024u
static int16_t spr_line[SPR_N] __attribute__((section(".pool")));
static const uint8_t SPR_APL[6] = {37, 43, 53, 61, 71, 79};
static int16_t spr_apb[344] __attribute__((section(".pool")));   /* (the six allpasses' lines, one after another) */
static uint32_t spr_w, spr_api[6], spr_ph;
static int32_t spr_lp;
static void spring_block(const int32_t *in, int32_t *out, uint32_t n, uint32_t tone, uint32_t stime)
{
    uint32_t m = n >> 1, j, k, w = spr_w;
    int32_t fb = 18000 + (int32_t)stime * 95, damp = 5000 + (int32_t)tone * 200, lp = spr_lp, L, L2;
    spr_ph += LFO_INC[40] * n;
    L = 640 + (sine(spr_ph) >> 13);             /* the loop's length wobbles: the spring sways */
    L2 = L * 47 / 64;
    for (j = 0; j < m; j++) {
        int32_t x = ((in[2u * j] + in[2u * j + 1u]) >> 8) >> 2, y = spr_line[(w - (uint32_t)L) & (SPR_N - 1u)], v, o = 0;
        lp += ((y - lp) * damp) >> 15;
        v = x + ((lp * fb) >> 15);
        for (k = 0; k < 6u; k++) {              /* allpass: out = old - a in; line = in + a out (a = 0.62) */
            int16_t *b = spr_apb + o;
            int32_t bo = b[spr_api[k]], ya = bo - ((20316 * v) >> 15);
            b[spr_api[k]] = sat16(v + ((20316 * ya) >> 15));
            if (++spr_api[k] >= SPR_APL[k]) spr_api[k] = 0;
            v = ya; o += SPR_APL[k];
        }
        spr_line[w] = sat16(v);
        w = (w + 1u) & (SPR_N - 1u);
        rev_l[j] = y; rev_r[j] = spr_line[(w - (uint32_t)L2) & (SPR_N - 1u)];
    }
    for (j = 0; j < m; j++) {
        int32_t l = (rev_l[j] * 120) >> 7, r = (rev_r[j] * 120) >> 7;
        out[4u * j] += l; out[4u * j + 1u] += r;
        out[4u * j + 2u] += l; out[4u * j + 3u] += r;
    }
    spr_w = w; spr_lp = lp;
}

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
static int16_t pbuf[NTRK][NFX][PB_N] __attribute__((section(".pool"), aligned(4)));
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

/* PUMP: DEPTH, RELEASE (50 .. 600 ms), SOURCE (the kick, as it plays; or every 1/4 or 1/8): down at once, back up on a
 * curve (the gain's dip squared: it comes back slowly, then quickly), as a compressor fed by the kick does */
static void pump_block(pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)
{
    static const uint8_t TK[3] = {0, 16, 8};
    uint32_t i, src = k[2] % 3u, R = 2205u + (uint32_t)k[1] * 200u, pos = (uint32_t)st->s[3], per = TK[src];
    int32_t g = 32767, depth = k[0];
    sync_t c;
    if (!st->s[4]) { pos = R; st->s[4] = 1; }  /* (fresh: not ducked) */
    sync_begin(&c, st);
    for (i = 0; i < n; i++) {
        int edge = sync_step(&c), trig = src ? edge && c.tk % per == 0u : (int32_t)i == kick_at;
        if (trig) pos = 0;
        if (trig || (i & 15u) == 0u) {
            if (pos < R) {
                int32_t f = (int32_t)(((R - pos) << 15) / R), d = (f * f) >> 15;
                g = 32767 - ((d * depth) >> 7);
            } else g = 32767;
        }
        if (pos < R) pos++;
        x[i] = (x[i] * g) >> 15;
    }
    sync_end(&c, st);
    st->s[3] = (int32_t)pos;
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
            if (!fxc_run) act = 0;              /* (stopped: no glitches) */
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

/* a pitch shifter in the slot's line: two read heads, half a window apart, sliding at the new speed, each faded in
 * and out (a triangle) so that one is always in the middle of its window. ratio Q16 (65536 = as is) */
#define PS_W (440 << 16)
static const uint32_t SEMI_Q16[49] = {16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548, 26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858, 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072, 138866, 147123, 155872, 165140, 174960, 185364, 196386, 208064, 220436, 233544, 247431, 262144};   /* -24 .. +24 semitones */
static int32_t shift_one(pst_t *st, const int16_t *b, uint32_t w, uint32_t ratio)
{
    int32_t d1 = (int32_t)st->ph, d2 = d1 + PS_W / 2, g1, y;
    if (d2 >= PS_W) d2 -= PS_W;
    g1 = d1 >> 16; g1 = g1 < 220 ? g1 : 440 - g1;
    y = (tap(b, PB_N - 1u, w, (d1 >> 8) + 512) * g1 + tap(b, PB_N - 1u, w, (d2 >> 8) + 512) * (220 - g1)) / 220;
    d1 += 65536 - (int32_t)ratio;
    if (d1 < 0) d1 += PS_W;
    if (d1 >= PS_W) d1 -= PS_W;
    st->ph = (uint32_t)d1;
    return y;
}
static void pitch_block(uint32_t type, pst_t *st, int16_t *b, const uint8_t *k, int32_t *x, uint32_t n)
{
    static const int8_t IV[10] = {-12, -7, -5, -4, -3, 3, 4, 5, 7, 12};   /* HARMONIZER's intervals */
    uint32_t i, w = st->w, ratio;
    int32_t lp = st->s[0], kt = 32767, mx = 127, lv = 127;
    if (type == PD_SHIFT) {                     /* SHIFTER: SEMI (-12..+12), OCTAVE (-2..+2), MIX */
        int32_t sm = (int32_t)(k[0] % 25u) - 12 + 12 * ((int32_t)(k[1] % 5u) - 2);
        ratio = SEMI_Q16[(sm < -24 ? -24 : sm > 24 ? 24 : sm) + 24]; mx = k[2];
    } else if (type == PD_HARM) {               /* HARMONIZER: INTERVAL, LEVEL, TONE: a second voice beside the first */
        ratio = SEMI_Q16[IV[k[0] % 10u] + 24]; lv = k[1]; kt = 4000 + (int32_t)k[2] * 220;
    } else {                                    /* SHADOW: LEVEL, TONE, OCTAVE (-1, -2): quieter and darker, below */
        ratio = k[2] % 2u ? 16384u : 32768u; lv = k[0]; kt = 1500 + (int32_t)k[1] * 120;
    }
    for (i = 0; i < n; i++) {
        int32_t y;
        b[w] = sat16(x[i]);
        y = shift_one(st, b, w, ratio);
        w = (w + 1u) & (PB_N - 1u);
        if (type == PD_SHIFT) x[i] += ((y - x[i]) * mx) >> 7;
        else { lp += ((y - lp) * kt) >> 15; x[i] += (lp * lv) >> 7; }
    }
    st->w = w; st->s[0] = lp;
}

static void lofi_block(uint32_t type, pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)   /* BITCRUSHER, LO-FI */
{
    uint32_t i, cnt = st->cnt;
    int32_t hold = st->s[0], lp = st->s[1], sh, per, mx = 127, nz = 0, drop = 0;
    if (type == PD_CRUSH) {                     /* BITS (16 .. 2), RATE (x1 .. x32 lower), MIX */
        sh = (int32_t)k[0] * 14 / 127; per = 1 + (int32_t)k[1] * 31 / 127; mx = k[2];
    } else {                                    /* LO-FI: RATE (x1 .. x8, a little unsteady), NOISE, DIRT (bits, dropouts) */
        sh = 4 + (int32_t)k[2] * 6 / 127; per = 1 + (int32_t)k[0] * 7 / 127; nz = k[1]; drop = k[2];
    }
    for (i = 0; i < n; i++) {
        if (++cnt >= (uint32_t)per) {           /* a new sample held: the rest of the period repeats it */
            int32_t v = clampv(x[i], 32767);
            cnt = 0;
            if (type == PD_LOFI) {
                uint32_t r = rnd_next(&st->rnd);
                if (per > 1 && (r & 7u) == 0u) cnt = 1;   /* (the clock jitters) */
                v += ((int32_t)(r >> 17) - 16384) * nz >> 11;   /* hiss */
                if ((r >> 24) < (uint32_t)drop >> 3) v >>= 2;  /* a dropout */
            }
            hold = sh ? ((v + (1 << (sh - 1))) >> sh) << sh : v;   /* fewer bits (rounded: no offset) */
        }
        if (type == PD_LOFI) { lp += ((hold - lp) * 14000) >> 15; x[i] = lp; }   /* (and dulled, ~ 3 kHz) */
        else x[i] += ((hold - x[i]) * mx) >> 7;
    }
    st->cnt = cnt; st->s[0] = hold; st->s[1] = lp;
}

static void slow_block(pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)   /* SLOW: RISE, DEPTH, SENS */
{
    uint32_t i;
    int32_t fast = st->s[0], slw = st->s[1], g = st->s[2], armed = st->s[3];   /* (g: Q23) */
    int32_t rise = (32767 << 8) / (2205 + (int32_t)k[0] * 500);   /* RISE: 50 ms .. 1.5 s to full */
    int32_t floor_ = (32767 - (32767 * (int32_t)k[1]) / 127) << 8, th = 200 + (127 - (int32_t)k[2]) * 20;
    if (!st->s[4]) { g = 32767 << 8; armed = 1; st->s[4] = 1; }
    for (i = 0; i < n; i++) {
        int32_t a = absv(x[i]);
        fast += (a - fast) >> 4; slw += (a - slw) >> 10;
        if (armed && fast > 2 * slw + th) { g = floor_; armed = 0; }   /* a new note: from (nearly) nothing ... */
        else if (!armed && fast < slw + (slw >> 2)) armed = 1;
        if ((i & 15u) == 0u && g < (32767 << 8)) { g += rise * 16; if (g > (32767 << 8)) g = 32767 << 8; }   /* ... it swells in */
        x[i] = (x[i] * (g >> 8)) >> 15;
    }
    st->s[0] = fast; st->s[1] = slw; st->s[2] = g; st->s[3] = armed;
}

static void ghost_block(uint32_t t, pst_t *st, const uint8_t *k, int32_t *x, uint32_t n)   /* GHOST: DELAY, LEVEL, DRIFT */
{
    int16_t *g = glbuf[t];
    uint32_t i, w = st->w;
    int32_t lp = st->s[0], d, sd, pl = st->dl, lv = k[1];
    st->ph += LFO_INC[18] * n;                  /* it drifts a little: never quite in time, nor in tune */
    d = ((900 + (int32_t)k[0] * 20) << 8) + ((sine(st->ph) * (int32_t)k[2]) >> 6);   /* (20 .. 80 ms, +- up to 6 ms: inside the line) */
    if (!pl) pl = d;
    sd = (d - pl) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t y;
        g[w] = sat16(x[i]);
        pl += sd;
        y = tap(g, GL_N - 1u, w, pl);
        w = (w + 1u) & (GL_N - 1u);
        lp += ((y - lp) * 9000) >> 15;          /* softer and darker, a step behind */
        x[i] += (lp * lv) >> 7;
    }
    st->w = w; st->s[0] = lp; st->dl = pl;
}

/* MUSIC BOX: DECAY, TONE, MIX. Each note the track starts plays a little plucked bell an octave up (a sine and a
 * metallic partial at 2.75x that dies sooner), each a touch out of tune, of level, and late: the comb of a music box.
 * The voices' states sit in the slot's line (it needs none): 4 x (age, envelope, two phases, increment, wait) */
static void mbox_block(uint32_t t, pst_t *st, int16_t *b, const uint8_t *k, int32_t *x, uint32_t n)
{
    int32_t *m = (int32_t *)(void *)b, mx = k[2], part = k[1], dec = 32767 - (135 - (int32_t)k[0]) * 3;
    uint32_t i, v, nv = t < NSYN ? TRK[t].count : 0u;
    for (v = 0; v < nv && v < 4u; v++) {        /* a note just started: pluck */
        uint32_t vi = TRK[t].first + v, *mv = (uint32_t *)(m + v * 6);
        if (vx[vi].gate && vx[vi].age != mv[0]) {
            uint32_t r = rnd_next(&st->rnd), inc = vx[vi].inc << 1;
            mv[0] = vx[vi].age;
            m[v * 6 + 1] = 26000 + (int32_t)(r & 4095u) * 2;   /* (its level, a little uneven) */
            mv[2] = mv[3] = 0;
            mv[4] = inc + (uint32_t)((int32_t)(inc >> 10) * ((int32_t)((r >> 12) & 15u) - 8));   /* (a touch out of tune) */
            mv[5] = (r >> 20) & 511u;           /* (and a hair late) */
        }
    }
    for (i = 0; i < n; i++) {
        int32_t y = 0;
        for (v = 0; v < nv && v < 4u; v++) {
            uint32_t *mv = (uint32_t *)(m + v * 6);
            int32_t env = m[v * 6 + 1];
            if (env < 64) continue;
            if (mv[5]) { mv[5]--; continue; }
            mv[2] += mv[4]; mv[3] += (mv[4] >> 2) * 11u;   /* (the partial: 2.75 x, without overflow) */
            y += ((sine(mv[2]) * env) >> 18) + ((((sine(mv[3]) * ((env * env) >> 15)) >> 15) * part) >> 10);
            if ((i & 15u) == 0u) m[v * 6 + 1] = (env * dec) >> 15;
        }
        x[i] += ((y - x[i]) * mx) >> 7;
    }
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
        if (type == PD_GLITCH || type == PD_GHOST) for (i = 0; i < GL_N; i++) glbuf[t][i] = 0;   /* (the track's line) */
        st->type = (uint8_t)(type + 1u);
        st->rnd = 0x9E3779B9u * (t * NFX + e + 1u);
    }
    st->live = 1;
    if (type <= PD_FOLD) drive_block(type, st, k, x, n);
    else if (type <= PD_WAH) filter_block(type, st, k, x, n);
    else if (type == PD_TREM || type == PD_SLICE) amp_block(type, st, k, x, n);
    else if (type == PD_PUMP) pump_block(st, k, x, n);
    else if (type == PD_CRUSH || type == PD_LOFI) lofi_block(type, st, k, x, n);
    else if (type == PD_SHIFT || type == PD_HARM || type == PD_SHADOW) pitch_block(type, st, pbuf[t][e], k, x, n);
    else if (type == PD_SLOW) slow_block(st, k, x, n);
    else if (type == PD_GHOST) ghost_block(t, st, k, x, n);
    else if (type == PD_MBOX) mbox_block(t, st, pbuf[t][e], k, x, n);
    else if (type == PD_BROKEN) {}              /* (BROKEN CLOCK changes when the track plays: gb_clock.h) */
    else if (type <= PD_UNI) mod_block(type, st, pbuf[t][e], k, x, n);
    else other_block(t, type, st, pbuf[t][e], k, x, n);
}

static void zero16(int16_t *p, uint32_t n) { while (n--) *p++ = 0; }
static void fx_flush(void)                      /* the music stopped: every line, loop and state emptied */
{
    uint32_t k;
    zero16(dly_buf, DLY_N); zero16(tape_buf, TAPE_N); zero16(rvd_buf, RVD_N); zero16(spr_line, SPR_N);
    zero16(spr_apb, sizeof spr_apb / 2u); zero16(shm_ps, SHM_PS);
    for (k = 0; k < 4u; k++) { zero16(REV_CB[k], REV_COMB[k]); zero16(SHM_CB[k], SHM_COMB[k]); rev_lp[k] = shm_lp[k] = 0; }
    for (k = 0; k < 3u; k++) zero16(REV_AB[k], REV_AP[k]);
    zero16(&pbuf[0][0][0], NTRK * NFX * PB_N); zero16(&glbuf[0][0], NTRK * GL_N);
    { uint8_t *z = (uint8_t *)pst; for (k = 0; k < sizeof pst; k++) z[k] = 0; }   /* (each slot starts afresh) */
    dly_lp = tape_lp = tape_lp2 = shm_last = shm_dc = spr_lp = rvd_acc = rvd_y0 = rvd_y1 = 0;
    rvd_k = rvd_q = 0; rvd_cs = rvd_w;
}

static void fx_process(int32_t *out, uint32_t n)
{
    uint32_t i, t, e, b, used = 0, flush = fx_flush_req;
    int32_t *wet = &tb[0][0];                   /* (the shared pedals' sound, stereo: tb is free once mixed) */
    n &= ~1u;                                   /* (the reverb takes pairs; the block is 256) */
    for (t = 0; t < NTRK; t++) {                /* 1. each track through its own pedals, slot by slot, in place */
        uint32_t on = set.fx[t].on;
        int32_t gv = (int32_t)set.vol[t] * 4096 / 100;   /* the mixer: 100 = as is (Q12), 127 = x1.27, after the pedals */
        for (e = 0; e < NFX; e++) {
            uint32_t p = set.ped[t][e] % NPD;
            if (e < NPED_SLOTS && on >> e & 1u && !PD_SPACE(p) && !(fx_quiet && (p == PD_GHOST || p == PD_GLITCH))) ped_insert(t, e, p, set.pk[t][e], tb[t], n);
            else pst[t][e].live = 0;
        }
        if (gv != 4096) {
            int32_t *x = tb[t];
            for (i = 0; i < n; i++) x[i] = (x[i] * gv) >> 12;
        }
    }
    for (b = 0; b < NSPACE_ALL; b++) {          /* 2. the send buses, from the slots that use them only */
        int32_t *bus = fx_bus[b];
        uint32_t first = 1;
        for (t = 0; t < NTRK; t++)
            for (e = 0; e < NPED_SLOTS; e++) {
                int32_t sv = set.pk[t][e][0];
                const int32_t *x = tb[t];
                if (!(set.fx[t].on >> e & 1u) || sp_of(set.ped[t][e] % NPD) != (int32_t)b || !sv || fx_quiet) continue;
                if (first) { for (i = 0; i < n; i++) bus[i] = x[i] * sv; first = 0; }
                else for (i = 0; i < n; i++) bus[i] += x[i] * sv;
            }
        if (!first) used |= 1u << b;
    }
    for (i = 0; i < n; i++) {                   /* 3. the dry mix */
        int32_t d = tb[0][i] + tb[1][i] + tb[2][i] + tb[3][i];
        out[2u * i] += d;
        out[2u * i + 1u] += d;
    }
    for (i = 0; i < 2u * n; i++) wet[i] = 0;    /* 4. the shared ones, each in one go */
    if (used >> SP_DLY & 1u) delay_block(fx_bus[SP_DLY], wet, n, set.spk[SP_DLY][0], set.spk[SP_DLY][1]);
    if (used >> SP_TAPE & 1u) tape_block(fx_bus[SP_TAPE], wet, n, set.spk[SP_TAPE][0], set.spk[SP_TAPE][1]);
    if (used >> SP_REVD & 1u) revdly_block(fx_bus[SP_REVD], wet, n, set.spk2[0][0], set.spk2[0][1]);
    if (used >> SP_REV & 1u) reverb_block(fx_bus[SP_REV], wet, n, set.spk[SP_REV][0], set.spk[SP_REV][1]);
    if (used >> SP_SHIM & 1u) shimmer_block(fx_bus[SP_SHIM], wet, n, set.spk[SP_SHIM][0], set.spk[SP_SHIM][1]);
    if (used >> SP_SPRING & 1u) spring_block(fx_bus[SP_SPRING], wet, n, set.spk2[1][0], set.spk2[1][1]);
    if (flush) {                                /* STOP: their tails fade out over this block, then all is emptied */
        for (i = 0; i < n; i++) {
            int32_t g = (int32_t)((n - i) * 32767u / n);
            out[2u * i] += (int32_t)(((int64_t)wet[2u * i] * g) >> 15);
            out[2u * i + 1u] += (int32_t)(((int64_t)wet[2u * i + 1u] * g) >> 15);
        }
        fx_flush();
        fx_flush_req = 0;
    } else if (used) for (i = 0; i < 2u * n; i++) out[i] += wet[i];
    kick_at = -1;                               /* (this block's kick has been heard) */
}
