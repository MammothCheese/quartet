/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects, as guitar pedals: three knobs each, named as on a pedal. Included by quartet.c, before gb_clock.h,
 * which renders each track into tb[track] and then calls fx_process().
 *
 *   REVERB      LEVEL (this track's), TONE, TIME              one reverb, shared: each track sends its LEVEL
 *   DELAY       REPEAT RATE (1/16 .. 1/2, on the tempo), ECHO (this track's), INTENSITY (repeats)   shared
 *   DISTORTION  LEVEL, TONE, DISTORTION                        this track's own; the volume follows the drive
 *   CHORUS      RATE, DEPTH, LEVEL (this track's)              shared, stereo
 *   PHASER      RATE, DEPTH, FEEDBACK                          this track's own
 * set.fx[track].on: bit e = pedal e on for this track (E_REV ..). A pedal nobody uses costs nothing.
 *
 * Speed: each effect runs as its own tight loop over the whole block; a send bus is filled only from the tracks
 * that use it; the reverb runs at half rate with one loop per echo line.
 * The delay's time comes from the tempo (exact), so it stays on the beat. Integer only; the big buffers are in the
 * pool (zeroed at boot). */

enum { E_REV, E_DLY, E_DST, E_CHO, E_PHS, NFX };    /* the pedals on FX, SEL, ENV, LFO, EDIT (GLO: the mixer) */
enum { B_REV, B_DLY, B_CHO, NBUS };             /* the shared effects' send buses */

static int32_t tb[NTRK][FXN];                   /* each track's sound for this block, mono */
static int32_t fx_bus[NBUS][FXN];               /* what the tracks send to each shared effect (x 128) */
static uint32_t fxc_run, fxc_tick, fxc_pos, fxc_len;   /* the clock at the block's start (gb_clock.h writes) */

/* delay: 65536 samples (1.49 s at 44.1 kHz); a time that does not fit is halved until it does */
#define DLY_N 65536u
static int16_t dly_buf[DLY_N] __attribute__((section(".pool")));
static uint32_t dly_w;
static int32_t dly_lp;
/* the repeat rates, in 16ths x 3: 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2 */
static const uint8_t DLY_DIV3[7] = {3, 4, 6, 9, 12, 18, 24};
static const char *const DLY_NAME[7] = {"1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2"};
#define NDLY 7u
#define DLY_RETURN 100                          /* the echoes' loudness against the dry sound (/128) */

/* chorus: a 1024-sample line, two taps swinging around ~7 ms */
#define CHO_N 1024u
static int16_t cho_buf[CHO_N] __attribute__((section(".pool")));
static uint32_t cho_w, cho_ph;
static int32_t cho_dl, cho_dr;                  /* the taps' delays now, Q8 samples */
#define CHO_RETURN 100

/* reverb: 4 combs + 2 allpasses (+1 for the right side), at 22.05 kHz (every other sample). Lengths: Freeverb's,
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

static int32_t dst_lp[NTRK];                    /* distortion's tone filter, per track */

static int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }

/* PHASER: 4 first-order allpasses whose coefficient a triangle LFO sweeps (0.55 +- up to 0.35, Q14), fed back into
 * themselves (FEEDBACK, up to ~0.78), mixed half and half with the dry sound: moving notches. Modelled with a full
 * scale square at full depth and feedback: products under 1.8e9 (of 2.1e9), it dies away within a second of a hit;
 * louder with feedback (the resonance), so the volume is trimmed as FEEDBACK rises (to 0.70 at full). */
static int32_t phs_x1[NTRK][4], phs_y1[NTRK][4], phs_last[NTRK];
static uint32_t phs_ph[NTRK];
static void phaser_block(uint32_t t, int32_t *x, uint32_t n, const tfx_t *f)
{
    uint32_t inc = LFO_INC[10u + (f->prate * 85u >> 7)], i, k, ph = phs_ph[t];   /* RATE: ~0.09 .. 5 Hz */
    int32_t span = (int32_t)f->pdepth * 5734 / 127, fbk = (int32_t)f->pfb * 100 / 127;   /* DEPTH, FEEDBACK (/128) */
    int32_t comp = 128 - (int32_t)f->pfb * (int32_t)f->pfb * 38 / 16129, a = 9011, last = phs_last[t];
    int32_t *x1 = phs_x1[t], *y1 = phs_y1[t];
    for (i = 0; i < n; i++) {
        int32_t v;
        if ((i & 15u) == 0u) {                  /* every 16 samples: the sweep */
            int32_t s;
            ph += inc * 16u;
            s = (int32_t)ph;
            a = 9011 + (((((s ^ (s >> 31)) >> 15) - 32768) * span) >> 15);
        }
        v = x[i] + ((fbk * last) >> 7);
        v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        for (k = 0; k < 4u; k++) {              /* y = a (y1 - x) + x1, four times */
            int32_t y = ((a * (y1[k] - v)) >> 14) + x1[k];
            x1[k] = v; y1[k] = y; v = y;
        }
        last = v;
        x[i] = (((x[i] + v) >> 1) * comp) >> 7;
    }
    phs_ph[t] = ph; phs_last[t] = last;
}

/* DISTORTION: drive x1 .. x32, round clipping (1.5x - 0.5x^3), a tone filter, then the volume. The clipped sound
 * sits near full scale, far louder than a clean track (and the curve itself adds x1.5); so the volume falls as the
 * drive rises: 0.17 + 0.50 / drive, i.e. 0.67 .. 0.19. Modelled on a saw lead: at every drive the distorted track
 * comes out 1.0 .. 1.2 x as loud (RMS) as the clean one (before: ~3.3 x). LEVEL (64 = that) trims it. */
static void dist_block(uint32_t t, int32_t *x, uint32_t n, const tfx_t *f)
{
    int32_t gain = 16 + (int32_t)f->drive * 4, k = 2000 + (int32_t)f->tone * 230, lp = dst_lp[t];
    int32_t makeup = 5571 + 16286 * 16 / gain;  /* Q15 */
    int32_t vol = (makeup * (int32_t)f->dlevel) >> 6;   /* Q15, LEVEL folded in (64 = as is) */
    uint32_t i;                                 /* (tone 230: the product stays 5 % under 2^31 at the worst swing, modelled) */
    for (i = 0; i < n; i++) {
        int32_t v = (x[i] * gain) >> 4, v2;
        v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        v2 = (v * v) >> 15;
        v = (3 * v - ((v * v2) >> 15)) >> 1;
        lp += ((v - lp) * k) >> 15;             /* tone: dark .. bright */
        x[i] = (lp * (vol >> 1)) >> 14;
    }
    dst_lp[t] = lp;
}


static void delay_block(const int32_t *in, int32_t *out, uint32_t n, const gfx_t *g)   /* DELAY: repeats that get darker */
{
    uint32_t len = 220500u * DLY_DIV3[g->dtime % NDLY] / set.bpm, i, w = dly_w;   /* exact from the tempo: on the beat */
    int32_t fb = (int32_t)g->dfb * 115 / 127, lp = dly_lp;   /* INTENSITY: the repeats' feedback, up to ~0.9 */
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

static void chorus_block(const int32_t *in, int32_t *out, uint32_t n, const gfx_t *g)   /* CHORUS: a moving tap per side */
{
    int32_t s, dl, dr, sl, sr, pl = cho_dl, pr = cho_dr;
    uint32_t i, w = cho_w;
    cho_ph += LFO_INC[30u + (g->crate * 70u >> 7)] * n;   /* RATE: ~0.2 .. 7 Hz */
    s = sine(cho_ph);
    dl = (300 << 8) + ((s * (int32_t)g->cdepth) >> 7);    /* DEPTH: ~6.8 ms +- up to ~2.9 ms, Q8 */
    dr = (300 << 8) - ((s * (int32_t)g->cdepth) >> 7);
    if (!pl) pl = dl, pr = dr;
    sl = (dl - pl) / (int32_t)n;                /* the taps glide to their new places over the block */
    sr = (dr - pr) / (int32_t)n;
    for (i = 0; i < n; i++) {
        uint32_t a;
        int32_t v0, v1;
        cho_buf[w] = sat16(in[i] >> 7);
        pl += sl; pr += sr;
        a = w - (uint32_t)(pl >> 8);
        v0 = cho_buf[a & (CHO_N - 1u)]; v1 = cho_buf[(a - 1u) & (CHO_N - 1u)];
        out[2u * i] += ((v0 + (((v1 - v0) * (pl & 255)) >> 8)) * CHO_RETURN) >> 7;
        a = w - (uint32_t)(pr >> 8);
        v0 = cho_buf[a & (CHO_N - 1u)]; v1 = cho_buf[(a - 1u) & (CHO_N - 1u)];
        out[2u * i + 1u] += ((v0 + (((v1 - v0) * (pr & 255)) >> 8)) * CHO_RETURN) >> 7;
        w = (w + 1u) & (CHO_N - 1u);
    }
    cho_w = w; cho_dl = pl; cho_dr = pr;
}

static void reverb_block(const int32_t *in, int32_t *out, uint32_t n, const gfx_t *g)   /* REVERB, at half rate */
{
    uint32_t m = n >> 1, j, k;                  /* (n is even: the audio block is 256) */
    int32_t fb = 22938 + (int32_t)g->rtime * 72;   /* TIME: feedback 0.70 .. 0.98 ... */
    int32_t damp = 6000 + (int32_t)g->rtone * 195;   /* TONE: the echoes dark .. bright (195: the product stays under 2^31) */
    for (j = 0; j < m; j++) { rev_x[j] = ((in[2u * j] + in[2u * j + 1u]) >> 8) >> 2; rev_l[j] = 0; }   /* two in, one: quieter */
    for (k = 0; k < 4u; k++) {                  /* combs, one line at a time: echoes that fade, darker each time */
        int16_t *buf = REV_CB[k];
        uint32_t len = (REV_COMB[k] * (64u + (g->rtime >> 1))) >> 7, idx = rev_ci[k];   /* ... and the room 50..100 % */
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

static void fx_process(int32_t *out, uint32_t n)
{
    static const uint8_t BUS_FX[NBUS] = {E_REV, E_DLY, E_CHO};
    const gfx_t *g = &set.gfx;
    uint32_t i, t, b, used = 0;
    n &= ~1u;                                   /* (the reverb takes pairs; the block is 256) */
    for (t = 0; t < NTRK; t++) {                /* 1. each track through its own pedals, in place */
        const tfx_t *f = &set.fx[t];
        int32_t gv = (int32_t)set.vol[t] * 4096 / 100;   /* the mixer: 100 = as is (Q12), 127 = x1.27, after the pedals */
        if (f->on >> E_DST & 1u) dist_block(t, tb[t], n, f);
        if (f->on >> E_PHS & 1u) phaser_block(t, tb[t], n, f);
        if (gv != 4096) {
            int32_t *x = tb[t];
            for (i = 0; i < n; i++) x[i] = (x[i] * gv) >> 12;
        }
    }
    for (b = 0; b < NBUS; b++) {                /* 2. the send buses, from the tracks that use them only */
        int32_t *bus = fx_bus[b];
        uint32_t first = 1;
        for (t = 0; t < NTRK; t++) {
            const tfx_t *f = &set.fx[t];
            int32_t s = b == B_REV ? f->rev_send : b == B_DLY ? f->dly_send : f->cho_send;
            const int32_t *x = tb[t];
            if (!(f->on >> BUS_FX[b] & 1u) || !s) continue;
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
    if (used >> B_DLY & 1u) delay_block(fx_bus[B_DLY], out, n, g);   /* 4. the shared pedals, each in one go */
    if (used >> B_CHO & 1u) chorus_block(fx_bus[B_CHO], out, n, g);
    if (used >> B_REV & 1u) reverb_block(fx_bus[B_REV], out, n, g);
}
