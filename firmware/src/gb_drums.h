/* SPDX-License-Identifier: GPL-3.0-only */
/* Track 4, the drums: KICK, SNARE, HAT, OPEN HAT, CLAP, PERC, synthesised (the LM-1 kit: real samples). Included by
 * quartet.c.
 * Each drum has its pitch, decay and level (set.dsnd[drum], 0..127, 64 = as designed).
 * The patterns are set.dpat[pattern][drum][page]: bit s of page g = step g*16+s (dbit() in quartet.c).
 * Runs in the audio interrupt (gb_clock.h calls it).
 * Live hits (the keys) come through hit_req; a muted track (trk_mute) skips its pattern. */

#define AMP_FULL (1 << 24)                      /* drum levels are Q24 so slow decays never get stuck */
#define KICK_TOP 15582648u                      /* 160 Hz: f * 2^32 / 44100 */
#define KICK_END 4674794u                       /* 48 Hz: the kick's pitch falls from TOP to END */
#define SNARE_BODY 18504395u                    /* 190 Hz: the snare's drum-skin tone */
#define PERC_TOP 58434928u                      /* 600 Hz: a wood block / tom, falling to 70 % */
#define CLAP_GAP 441u                           /* 10 ms between the three hand claps of a clap */

/* 2^((p - 64) / 32) in Q12: 0.25 .. 3.9 (drum pitch and decay) */
static const uint16_t MULT[128] = {
    1024, 1046, 1069, 1093, 1117, 1141, 1166, 1192, 1218, 1244, 1272, 1300,
    1328, 1357, 1387, 1417, 1448, 1480, 1512, 1545, 1579, 1614, 1649, 1685,
    1722, 1760, 1798, 1838, 1878, 1919, 1961, 2004, 2048, 2093, 2139, 2186,
    2233, 2282, 2332, 2383, 2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371, 3444, 3520, 3597, 3676,
    3756, 3838, 3922, 4008, 4096, 4186, 4277, 4371, 4467, 4565, 4664, 4767,
    4871, 4978, 5087, 5198, 5312, 5428, 5547, 5668, 5793, 5919, 6049, 6182,
    6317, 6455, 6597, 6741, 6889, 7039, 7194, 7351, 7512, 7677, 7845, 8016,
    8192, 8371, 8555, 8742, 8933, 9129, 9329, 9533, 9742, 9955, 10173, 10396,
    10624, 10856, 11094, 11337, 11585, 11839, 12098, 12363, 12634, 12910, 13193, 13482,
    13777, 14079, 14387, 14702, 15024, 15353, 15689, 16033};
/* each drum's decay as designed: the level falls k / 65536 a sample (k 16: ~0.1 s time constant) */
static const uint16_t KDEF[NDR] = {16, 64, 128, 16, 24, 48};

/* the ten kits (SELECT on DRUMS, 2 clicks each): each drum's {pitch, decay, level}, 64 = as designed.
 * KICK, SNARE, HAT, OPEN HAT, CLAP, PERC. How some drums are made differs per kit (KIT_TONE below): the kick's beater
 * click (ROCK, 909), the hats' noise (MINI POPS: soft maracas), what PERC is (a tom, a conga, a shaker ...); MINI POPS
 * gives the snare more tone than rattle; LO-FI, HIP-HOP and INDUSTRIAL crush the sound like an old sampler. The LM-1
 * plays six samples of a real Linn LM-1 (rossf's, lm1_samples.h; CLAP and PERC play its low and high tom).
 * (The drum engine's changes come from SPLITMILK 0.10-0.13, the same author's firmware built on QUARTET.) */
#include "lm1_samples.h"                       /* (firmware/gen: tools/samples2c.py) */
#define NKIT 10u
#define KIT_ROCK 0u
#define KIT_POPS 2u
#define KIT_LM1 4u                              /* the LM-1: real samples, not synthesised */
#define KIT_909 7u
static const dsnd_t KITS[NKIT][NDR] = {
    {{58, 60, 92}, {70, 82, 86}, {64, 60, 60}, {56, 112, 66}, {64, 64, 64}, {34, 82, 78}},     /* ROCK: a punchy kick, a fat snare, a crash, a floor tom */
    {{46, 96, 84}, {54, 80, 76}, {56, 46, 48}, {56, 66, 46}, {56, 70, 66}, {70, 60, 56}},      /* HIP-HOP: a low boomy kick, a fat snare, dark hats, dusty */
    {{72, 50, 72}, {84, 46, 66}, {92, 30, 58}, {88, 70, 48}, {84, 36, 56}, {64, 42, 54}},      /* MINI POPS: a round soft thud, a tonal snare, maracas, claves, a shaker */
    {{76, 40, 64}, {84, 40, 56}, {90, 30, 48}, {90, 50, 48}, {70, 40, 48}, {58, 64, 72}},      /* CR-78: short, light, clicky, a conga */
    {{64, 64, 70}, {64, 64, 66}, {64, 64, 44}, {64, 64, 40}, {64, 64, 58}, {64, 64, 58}},      /* LM-1: samples (64 = as recorded) */
    {{72, 44, 78}, {84, 50, 70}, {96, 36, 56}, {96, 64, 54}, {70, 50, 60}, {64, 56, 58}},      /* 606: small and snappy: a short tight kick, a thin bright snare, ticking hats, a shaker */
    {{44, 110, 72}, {64, 64, 60}, {70, 50, 56}, {70, 80, 56}, {64, 70, 72}, {70, 82, 72}},     /* 808: a long low boom, a high tom */
    {{60, 72, 90}, {78, 70, 78}, {80, 48, 62}, {80, 80, 60}, {64, 64, 74}, {50, 70, 66}},      /* 909: a clicky punchy kick, a bright snare, crisp hats */
    {{56, 80, 76}, {56, 60, 70}, {50, 50, 56}, {50, 70, 56}, {56, 60, 64}, {44, 86, 74}},      /* LO-FI: lower, then crushed, a low tom */
    {{54, 80, 92}, {60, 90, 86}, {64, 70, 64}, {60, 100, 62}, {50, 90, 80}, {36, 90, 80}}};    /* INDUSTRIAL: loud and long, crushed hard */
static const char *const KIT_NAME[NKIT] = {"ROCK", "HIP-HOP", "MINI POPS", "CR-78", "LM-1", "606", "808", "909", "LO-FI", "INDUSTRIAL"};
/* how some drums are made, per kit: the kick's beater click (0 none, 1..4: how loud), the hats' noise (0 bright, the
 * change from sample to sample; 1 full, the noise itself; 2 soft, smoothed: MINI POPS' maracas), whether PITCH tunes
 * the clap (0: as designed; 1: pitch moves its noise band down / up), and what PERC is:
 *   P_SINE   a sine falling to 70 % from 600 Hz (the knock)       P_TOM    a long fall from 200 Hz, skin at the hit
 *   P_CONGA  a quick drop from 380 Hz, a slap (higher: a bongo)   P_COWBELL two squares (540, 800 Hz): the 808's
 *   P_RIM    a short 1.7 kHz crack and a click: a rimshot         P_SHAKER bright noise swelling in: maracas
 *   P_GUIRO  noise scraped in short ramps: a guiro                P_TAMB   three quick bursts, then a ring
 * (PITCH moves each up / down, 64 = as above; DECAY sets its length) */
typedef struct { uint8_t click, hat, open, clap_tune, perc; } ktone_t;
enum { P_SINE, P_TOM, P_CONGA, P_COWBELL, P_RIM, P_SHAKER, P_GUIRO, P_TAMB, NPERC };
static const ktone_t KIT_TONE[NKIT] = {
    {1, 0, 0, 0, P_SINE}, {0, 0, 0, 0, P_SINE}, {0, 2, 0, 0, P_SHAKER}, {0, 0, 0, 0, P_CONGA},   /* ROCK, HIP-HOP, MINI POPS, CR-78 */
    {0, 0, 0, 0, P_SINE}, {0, 0, 0, 0, P_SHAKER}, {0, 0, 0, 0, P_TOM}, {1, 0, 0, 0, P_SINE},       /* LM-1, 606, 808, 909 */
    {0, 0, 0, 0, P_TOM}, {0, 0, 0, 0, P_SINE}};                                                   /* LO-FI, INDUSTRIAL */
/* the crushers: keep one sample in n (0: off) and round to steps of mask + 1 */
static const uint8_t KIT_CRUSH_N[NKIT] = {0, 2, 0, 0, 0, 0, 0, 0, 3, 4};
static const uint16_t KIT_CRUSH_M[NKIT] = {0, 255, 0, 0, 0, 0, 0, 0, 1023, 4095};

static struct { uint32_t ph, inc, end, cnt; int32_t amp, amp2, lp1, lp2; } dr[NDR];   /* interrupt only */
static volatile uint8_t hit_req[NDR];           /* main loop: +1 = "play drum d now" (the interrupt counts what it did) */

static uint32_t scale_q12(uint32_t v, uint32_t m) { return (uint32_t)(((uint64_t)v * m) >> 12); }

static void drum_trigger(uint32_t d)
{
    uint32_t kit = set.dkit % NKIT, m = MULT[set.dsnd[d].pitch & 127u];
    dr[d].amp = AMP_FULL;
    dr[d].ph = 0;
    dr[d].cnt = 0;
    if (d == DR_KICK) { dr[d].inc = scale_q12(KICK_TOP, m); dr[d].end = scale_q12(KICK_END, m); dr[d].amp2 = KIT_TONE[kit].click ? AMP_FULL : 0; }
    if (d == DR_SNARE) { dr[d].inc = scale_q12(SNARE_BODY, m); dr[d].amp2 = AMP_FULL; dr[d].lp1 = 0; }
    if (d == DR_PERC) {
        dr[d].inc = scale_q12(PERC_TOP, m); dr[d].end = dr[d].inc - (dr[d].inc >> 2) - (dr[d].inc >> 4);
        dr[d].amp2 = 0; dr[d].lp1 = dr[d].lp2 = 0;
        switch (KIT_TONE[kit].perc) {
        case P_TOM: dr[d].inc = scale_q12(19478299u, m); dr[d].end = (dr[d].inc >> 1) + (dr[d].inc >> 4); dr[d].amp2 = AMP_FULL; break;   /* 200 Hz to 56 % */
        case P_CONGA: dr[d].inc = scale_q12(37008776u, m); dr[d].end = dr[d].inc - (dr[d].inc >> 3); dr[d].amp2 = AMP_FULL; break;     /* 380 Hz to 88 % */
        case P_COWBELL: dr[d].inc = scale_q12(52591424u, m); dr[d].cnt = scale_q12(77913221u, m); dr[d].end = 0; dr[d].amp2 = AMP_FULL; break;   /* 540, 800 Hz */
        case P_RIM: dr[d].inc = scale_q12(165565594u, m); dr[d].amp2 = AMP_FULL; break;   /* 1.7 kHz */
        case P_GUIRO: dr[d].end = (uint32_t)(1100u * 4096u / m); break;   /* (samples a scrape: ~25 ms at 64) */
        default: break;
        }
    }
    if (d == DR_HAT) dr[DR_OPEN].amp = 0;       /* a closed hat cuts an open one short, like a real hi-hat */
    if (kit == KIT_LM1) { dr[d].ph = 0; dr[d].inc = m << 4; dr[d].amp2 = 0; }   /* a sample: from its start, at PITCH's speed
                                                   (ph: the place, Q16; 64 = as recorded) */
}

static void drums_step(void)
{
    uint32_t d, s = trk_pos[T_DRUMS], p = play_pat[T_DRUMS] % NPAT;
    if ((trk_mute | chain_rest) >> T_DRUMS & 1u) return;   /* muted, or a SUPER SEQ rest */
    for (d = 0; d < NDR; d++)
        if (dbit(p, d, s)) drum_trigger(d);
}
static void drums_tick(void) {}                 /* (drums have no note lengths) */
static void drums_stop(void) {}                 /* (they ring out) */

static int drums_quiet(void)
{
    uint32_t d;
    for (d = 0; d < NDR; d++)
        if (dr[d].amp || dr[d].amp2) return 0;
    return 1;
}

static int32_t decay_k(int32_t a, int32_t k)    /* a (Q24) falls by k / 65536 each sample (k <= 1023), and reaches 0 */
{
    a -= (((a >> 10) * k) >> 6) + 1;
    return a < 0 ? 0 : a;
}

static void drums_render(int32_t *out, uint32_t n)      /* out: the drum track's mono buffer */
{
    static uint8_t hit_done[NDR];
    static uint32_t rnd = 22222u, crush_n;
    static int32_t crush_v;
    uint32_t kit = set.dkit % NKIT, crush = KIT_CRUSH_N[kit], cmask = KIT_CRUSH_M[kit], pops = kit == KIT_POPS;
    const ktone_t *kt = &KIT_TONE[kit];
    static int32_t prev_noise;
    int32_t ca1 = 0, ca2 = 0;                   /* (a tuned clap: its band's two smoothing steps, Q12) */
    int32_t k[NDR], g[NDR];
    uint32_t i, d;
    for (d = 0; d < NDR; d++)
        if (hit_done[d] != hit_req[d]) { hit_done[d] = hit_req[d]; drum_trigger(d); }
    if (drums_quiet())
        return;                                 /* silent: costs nothing */
    if (kit == KIT_LM1) {                       /* the LM-1: its samples (mu-law, from flash), each read at its speed,
                                                   a straight line between samples; DECAY under 64 fades it faster */
        for (d = 0; d < NDR; d++) {
            uint32_t dc = set.dsnd[d].decay & 127u;
            k[d] = dc >= 64u ? 0 : (int32_t)(64u - dc) * 2;
            g[d] = set.dsnd[d].level & 127u;
        }
        for (i = 0; i < n; i++) {
            int32_t s = 0;
            for (d = 0; d < NDR; d++) {
                uint32_t p;
                int32_t a, v;
                if (!dr[d].amp) continue;
                p = dr[d].ph >> 16;
                if (p + 1u >= LM1_LEN[d]) { dr[d].amp = 0; continue; }   /* (its end) */
                a = MULAW[LM1_DATA[d][p]];
                v = a + (((MULAW[LM1_DATA[d][p + 1u]] - a) * (int32_t)((dr[d].ph >> 2) & 0x3FFFu)) >> 14);
                s += (((v * (dr[d].amp >> 9)) >> 15) * g[d]) >> 7;
                dr[d].ph += dr[d].inc;
                if (k[d]) dr[d].amp = decay_k(dr[d].amp, k[d]);
            }
            out[i] += s;
        }
        return;
    }
    for (d = 0; d < NDR; d++) {                 /* each drum's decay and level, for this piece */
        int32_t kk = (int32_t)((KDEF[d] * (uint32_t)MULT[127u - (set.dsnd[d].decay & 127u)]) >> 12);
        k[d] = kk < 1 ? 1 : kk > 1023 ? 1023 : kk;
        g[d] = set.dsnd[d].level & 127u;        /* 64 = as designed, 127 = about twice */
    }
    if (kt->clap_tune) {                        /* 2048 x and 256 x the pitch's multiple (Q12): 1/2 and 1/16 at 64 */
        uint32_t m = MULT[set.dsnd[DR_CLAP].pitch & 127u];
        ca1 = (int32_t)((2048u * m) >> 12); ca2 = (int32_t)((256u * m) >> 12);
        if (ca1 > 4095) ca1 = 4095;
        if (ca1 < 64) ca1 = 64;
        if (ca2 < 8) ca2 = 8;
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0, noise, hp;
        rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;   /* xorshift: white noise */
        noise = (int32_t)rnd >> 16;
        hp = (noise - prev_noise) >> 1;         /* the change from the last sample: a bright, hissy noise */
        prev_noise = noise;
        if (dr[DR_KICK].amp) {                  /* KICK: a sine whose pitch drops fast */
            dr[DR_KICK].inc -= (uint32_t)((int32_t)(dr[DR_KICK].inc - dr[DR_KICK].end) >> 9);
            dr[DR_KICK].ph += dr[DR_KICK].inc;
            s += ((((sine(dr[DR_KICK].ph) * (dr[DR_KICK].amp >> 9)) >> 15) * 3 >> 2) * g[DR_KICK]) >> 6;
            dr[DR_KICK].amp = decay_k(dr[DR_KICK].amp, k[DR_KICK]);
            if (dr[DR_KICK].amp2) {             /* ROCK, 909: the beater's click, a few ms of bright noise */
                s += ((((hp * (dr[DR_KICK].amp2 >> 9)) >> 15) * g[DR_KICK]) >> 7) * (int32_t)kt->click;
                dr[DR_KICK].amp2 = decay_k(dr[DR_KICK].amp2, 300);
            }
        }
        if (dr[DR_SNARE].amp || dr[DR_SNARE].amp2) {   /* SNARE: a short tone + a burst of noise */
            int32_t sn;
            dr[DR_SNARE].ph += dr[DR_SNARE].inc;
            if (pops) {                         /* MINI POPS: mostly the drum's tone, a little rattle */
                sn = ((sine(dr[DR_SNARE].ph) * (dr[DR_SNARE].amp >> 9)) >> 15) * 5 >> 3;
                sn += ((noise * (dr[DR_SNARE].amp2 >> 9)) >> 15) >> 2;
            } else {
                sn = ((sine(dr[DR_SNARE].ph) * (dr[DR_SNARE].amp >> 9)) >> 15) * 3 >> 3;
                sn += ((noise * (dr[DR_SNARE].amp2 >> 9)) >> 15) >> 1;
            }
            s += (sn * g[DR_SNARE]) >> 6;
            dr[DR_SNARE].amp = decay_k(dr[DR_SNARE].amp, k[DR_SNARE]);
            dr[DR_SNARE].amp2 = decay_k(dr[DR_SNARE].amp2, k[DR_SNARE] >> 1);
        }
        if (dr[DR_HAT].amp) {                   /* HAT: very short bright noise */
            int32_t hn = hp >> 2;
            if (kt->hat == 2u) { dr[DR_HAT].lp1 += (noise - dr[DR_HAT].lp1) >> 1; hn = dr[DR_HAT].lp1 >> 1; }   /* soft: maracas */
            else if (kt->hat == 1u) hn = noise >> 2;   /* full */
            s += (((hn * (dr[DR_HAT].amp >> 9)) >> 15) * g[DR_HAT]) >> 6;
            dr[DR_HAT].amp = decay_k(dr[DR_HAT].amp, k[DR_HAT]);
        }
        if (dr[DR_OPEN].amp) {                  /* OPEN HAT: the same noise, ringing longer */
            int32_t oh = hp >> 2;
            if (kt->open == 2u) { dr[DR_OPEN].lp1 += (noise - dr[DR_OPEN].lp1) >> 1; oh = dr[DR_OPEN].lp1 >> 1; }   /* soft */
            else if (kt->open == 1u) oh = noise >> 2;   /* full */
            s += (((oh * (dr[DR_OPEN].amp >> 9)) >> 15) * g[DR_OPEN]) >> 6;
            dr[DR_OPEN].amp = decay_k(dr[DR_OPEN].amp, k[DR_OPEN]);
        }
        if (dr[DR_CLAP].amp) {                  /* CLAP: three quick bursts of mid noise, then a tail */
            uint32_t c = ++dr[DR_CLAP].cnt;
            int32_t band;
            if (c == CLAP_GAP || c == 2u * CLAP_GAP) dr[DR_CLAP].amp = AMP_FULL;
            if (kt->clap_tune) {                /* tuned: the band slides with PITCH (64: as designed) */
                dr[DR_CLAP].lp1 += ((noise - dr[DR_CLAP].lp1) * ca1) >> 12;
                dr[DR_CLAP].lp2 += ((dr[DR_CLAP].lp1 - dr[DR_CLAP].lp2) * ca2) >> 12;
            } else {                            /* (two smoothings: what is left is ~450 Hz - 7 kHz) */
                dr[DR_CLAP].lp1 += (noise - dr[DR_CLAP].lp1) >> 1;
                dr[DR_CLAP].lp2 += (dr[DR_CLAP].lp1 - dr[DR_CLAP].lp2) >> 4;
            }
            band = dr[DR_CLAP].lp1 - dr[DR_CLAP].lp2;
            s += ((((band * (dr[DR_CLAP].amp >> 9)) >> 15) * 3 >> 2) * g[DR_CLAP]) >> 6;
            dr[DR_CLAP].amp = decay_k(dr[DR_CLAP].amp, c < 2u * CLAP_GAP ? 400 : k[DR_CLAP]);
        }
        if (dr[DR_PERC].amp) {                  /* PERC, by the kit's type */
            int32_t pv, a9 = dr[DR_PERC].amp >> 9, b9 = dr[DR_PERC].amp2 >> 9;
            switch (kt->perc) {
            case P_TOM:                         /* a long fall, skin at the hit */
                dr[DR_PERC].inc -= (uint32_t)((int32_t)(dr[DR_PERC].inc - dr[DR_PERC].end) >> 11);
                dr[DR_PERC].ph += dr[DR_PERC].inc;
                pv = (((sine(dr[DR_PERC].ph) * a9) >> 15) >> 1) + (((noise * b9) >> 15) >> 3);
                dr[DR_PERC].amp2 = decay_k(dr[DR_PERC].amp2, 250);
                break;
            case P_CONGA:                       /* a quick drop, a slap */
                dr[DR_PERC].inc -= (uint32_t)((int32_t)(dr[DR_PERC].inc - dr[DR_PERC].end) >> 7);
                dr[DR_PERC].ph += dr[DR_PERC].inc;
                dr[DR_PERC].lp1 += (noise - dr[DR_PERC].lp1) >> 2;
                pv = (((sine(dr[DR_PERC].ph) * a9) >> 15) >> 1) + (((dr[DR_PERC].lp1 * b9) >> 15) >> 1);
                dr[DR_PERC].amp2 = decay_k(dr[DR_PERC].amp2, 500);
                break;
            case P_COWBELL: {                   /* two squares, smoothed (~3 kHz down) and the lows taken out */
                int32_t v, b;
                dr[DR_PERC].ph += dr[DR_PERC].inc;
                dr[DR_PERC].end += dr[DR_PERC].cnt;
                v = ((int32_t)dr[DR_PERC].ph < 0 ? -8192 : 8192) + ((int32_t)dr[DR_PERC].end < 0 ? -8192 : 8192);
                dr[DR_PERC].lp1 += (v - dr[DR_PERC].lp1) >> 1;
                dr[DR_PERC].lp2 += (dr[DR_PERC].lp1 - dr[DR_PERC].lp2) >> 5;
                b = dr[DR_PERC].lp1 - dr[DR_PERC].lp2;
                pv = (b * (a9 + (b9 >> 1))) >> 15;   /* (a bright strike, then the body) */
                dr[DR_PERC].amp2 = decay_k(dr[DR_PERC].amp2, 200);
                break;
            }
            case P_RIM:                         /* a short crack and a click */
                dr[DR_PERC].ph += dr[DR_PERC].inc;
                pv = (((sine(dr[DR_PERC].ph) * a9) >> 15) >> 1) + ((hp * b9) >> 15);
                dr[DR_PERC].amp2 = decay_k(dr[DR_PERC].amp2, 600);
                break;
            case P_SHAKER: {                    /* bright noise, swelling in over 10 ms */
                int32_t att = dr[DR_PERC].cnt < 441u ? (int32_t)dr[DR_PERC].cnt * 74 : 32767;
                dr[DR_PERC].cnt++;
                pv = (((hp * a9) >> 15) * att) >> 15;
                break;
            }
            case P_GUIRO: {                     /* noise in a band, scraped in ramps */
                uint32_t per = dr[DR_PERC].end ? dr[DR_PERC].end : 1100u, c = dr[DR_PERC].cnt++ % per;
                int32_t ramp = c < per / 2u ? (int32_t)(c * 65000u / per) : 0, band;
                dr[DR_PERC].lp1 += (noise - dr[DR_PERC].lp1) >> 1;
                dr[DR_PERC].lp2 += (dr[DR_PERC].lp1 - dr[DR_PERC].lp2) >> 4;
                band = dr[DR_PERC].lp1 - dr[DR_PERC].lp2;
                pv = (((band * a9) >> 15) * ramp) >> 15;
                break;
            }
            case P_TAMB: {                      /* three quick bursts of bright noise, then the ring */
                uint32_t c = ++dr[DR_PERC].cnt;
                if (c == 330u || c == 660u) dr[DR_PERC].amp = AMP_FULL - (AMP_FULL >> 2);
                pv = (hp * a9) >> 15;
                break;
            }
            default:                            /* P_SINE: the knock */
                dr[DR_PERC].inc -= (uint32_t)((int32_t)(dr[DR_PERC].inc - dr[DR_PERC].end) >> 8);
                dr[DR_PERC].ph += dr[DR_PERC].inc;
                pv = ((sine(dr[DR_PERC].ph) * a9) >> 15) >> 1;
            }
            s += (pv * g[DR_PERC]) >> 6;
            dr[DR_PERC].amp = decay_k(dr[DR_PERC].amp, k[DR_PERC]);
        }
        if (crush) {                            /* LO-FI, HIP-HOP, INDUSTRIAL: fewer samples, coarse steps: an old sampler */
            if (++crush_n >= crush) { crush_n = 0; crush_v = s & ~(int32_t)cmask; }
            s = crush_v;
        }
        out[i] += s;
    }
}
