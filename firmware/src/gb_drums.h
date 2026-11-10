/* SPDX-License-Identifier: GPL-3.0-only */
/* Track 4, the drums: KICK, SNARE, HAT, OPEN HAT, CLAP, PERC, all synthesised (no samples). Included by quartet.c.
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
 * KICK, SNARE, HAT, OPEN HAT, CLAP, PERC. Some also change how drums are made: ROCK and 909 give the kick a beater
 * click (punch); MINI POPS (a little 1960s rhythm box) gives the snare more tone than rattle and HAT a soft maraca shake;
 * LO-FI, HIP-HOP and INDUSTRIAL crush the sound like an old sampler (KIT_CRUSH: lightly, more, a lot). */
#define NKIT 10u
#define KIT_ROCK 0u
#define KIT_POPS 2u
#define KIT_909 7u
static const dsnd_t KITS[NKIT][NDR] = {
    {{58, 60, 92}, {70, 82, 86}, {64, 60, 60}, {56, 112, 66}, {64, 64, 64}, {34, 82, 78}},     /* ROCK: a punchy kick, a fat snare, a crash, a floor tom */
    {{46, 96, 84}, {54, 80, 76}, {56, 46, 48}, {56, 66, 46}, {56, 70, 66}, {70, 60, 56}},      /* HIP-HOP: a low boomy kick, a fat snare, dark hats, dusty */
    {{72, 50, 72}, {84, 46, 66}, {92, 30, 58}, {88, 70, 48}, {84, 36, 56}, {92, 48, 70}},      /* MINI POPS: a round soft thud, a tonal snare, maracas, claves, a bongo */
    {{76, 40, 64}, {84, 40, 56}, {90, 30, 48}, {90, 50, 48}, {70, 40, 48}, {110, 30, 60}},     /* CR-78: short, light, clicky */
    {{64, 64, 80}, {60, 76, 76}, {64, 56, 60}, {64, 76, 56}, {60, 64, 70}, {40, 80, 70}},      /* LM-1: punchy, a tom for PERC */
    {{72, 44, 78}, {84, 50, 70}, {96, 36, 56}, {96, 64, 54}, {70, 50, 60}, {80, 50, 64}},      /* 606: small and snappy: a short tight kick, a thin bright snare, ticking hats */
    {{44, 110, 72}, {64, 64, 60}, {70, 50, 56}, {70, 80, 56}, {64, 70, 72}, {100, 60, 54}},    /* 808: a long low boom, a cowbell */
    {{60, 72, 90}, {78, 70, 78}, {80, 48, 62}, {80, 80, 60}, {64, 64, 74}, {50, 70, 66}},      /* 909: a clicky punchy kick, a bright snare, crisp hats */
    {{56, 80, 76}, {56, 60, 70}, {50, 50, 56}, {50, 70, 56}, {56, 60, 64}, {60, 60, 60}},      /* LO-FI: lower, then crushed */
    {{54, 80, 92}, {60, 90, 86}, {64, 70, 64}, {60, 100, 62}, {50, 90, 80}, {36, 90, 80}}};    /* INDUSTRIAL: loud and long, crushed hard */
static const char *const KIT_NAME[NKIT] = {"ROCK", "HIP-HOP", "MINI POPS", "CR-78", "LM-1", "606", "808", "909", "LO-FI", "INDUSTRIAL"};
/* the crushers: keep one sample in n (0: off) and round to steps of mask + 1 */
static const uint8_t KIT_CRUSH_N[NKIT] = {0, 2, 0, 0, 0, 0, 0, 0, 3, 4};
static const uint16_t KIT_CRUSH_M[NKIT] = {0, 255, 0, 0, 0, 0, 0, 0, 1023, 4095};

static struct { uint32_t ph, inc, end, cnt; int32_t amp, amp2, lp1, lp2; } dr[NDR];   /* interrupt only */
static volatile uint8_t hit_req[NDR];           /* main loop: +1 = "play drum d now" (the interrupt counts what it did) */

static uint32_t scale_q12(uint32_t v, uint32_t m) { return (uint32_t)(((uint64_t)v * m) >> 12); }

static void drum_trigger(uint32_t d)
{
    uint32_t m = MULT[set.dsnd[d].pitch & 127u];
    dr[d].amp = AMP_FULL;
    dr[d].ph = 0;
    dr[d].cnt = 0;
    if (d == DR_KICK) { dr[d].inc = scale_q12(KICK_TOP, m); dr[d].end = scale_q12(KICK_END, m); dr[d].amp2 = set.dkit == KIT_ROCK || set.dkit == KIT_909 ? AMP_FULL : 0; }
    if (d == DR_SNARE) { dr[d].inc = scale_q12(SNARE_BODY, m); dr[d].amp2 = AMP_FULL; dr[d].lp1 = 0; }
    if (d == DR_PERC) { dr[d].inc = scale_q12(PERC_TOP, m); dr[d].end = dr[d].inc - (dr[d].inc >> 2) - (dr[d].inc >> 4); }
    if (d == DR_HAT) dr[DR_OPEN].amp = 0;       /* a closed hat cuts an open one short, like a real hi-hat */
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
    static int32_t prev_noise;
    int32_t k[NDR], g[NDR];
    uint32_t i, d;
    for (d = 0; d < NDR; d++)
        if (hit_done[d] != hit_req[d]) { hit_done[d] = hit_req[d]; drum_trigger(d); }
    if (drums_quiet())
        return;                                 /* silent: costs nothing */
    for (d = 0; d < NDR; d++) {                 /* each drum's decay and level, for this piece */
        int32_t kk = (int32_t)((KDEF[d] * (uint32_t)MULT[127u - (set.dsnd[d].decay & 127u)]) >> 12);
        k[d] = kk < 1 ? 1 : kk > 1023 ? 1023 : kk;
        g[d] = set.dsnd[d].level & 127u;        /* 64 = as designed, 127 = about twice */
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
            if (dr[DR_KICK].amp2) {             /* ROCK: the beater's click, a few ms of bright noise */
                s += (((hp * (dr[DR_KICK].amp2 >> 9)) >> 15) * g[DR_KICK]) >> 7;
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
            if (pops) {                         /* MINI POPS: maracas, a softer, rounder shake */
                dr[DR_HAT].lp1 += (noise - dr[DR_HAT].lp1) >> 1;
                hn = dr[DR_HAT].lp1 >> 1;
            }
            s += (((hn * (dr[DR_HAT].amp >> 9)) >> 15) * g[DR_HAT]) >> 6;
            dr[DR_HAT].amp = decay_k(dr[DR_HAT].amp, k[DR_HAT]);
        }
        if (dr[DR_OPEN].amp) {                  /* OPEN HAT: the same noise, ringing longer */
            int32_t oh = hp >> 2;
            s += (((oh * (dr[DR_OPEN].amp >> 9)) >> 15) * g[DR_OPEN]) >> 6;
            dr[DR_OPEN].amp = decay_k(dr[DR_OPEN].amp, k[DR_OPEN]);
        }
        if (dr[DR_CLAP].amp) {                  /* CLAP: three quick bursts of mid noise, then a tail */
            uint32_t c = ++dr[DR_CLAP].cnt;
            int32_t band;
            if (c == CLAP_GAP || c == 2u * CLAP_GAP) dr[DR_CLAP].amp = AMP_FULL;
            dr[DR_CLAP].lp1 += (noise - dr[DR_CLAP].lp1) >> 1;   /* (two smoothings: what is left is ~450 Hz - 7 kHz) */
            dr[DR_CLAP].lp2 += (dr[DR_CLAP].lp1 - dr[DR_CLAP].lp2) >> 4;
            band = dr[DR_CLAP].lp1 - dr[DR_CLAP].lp2;
            s += ((((band * (dr[DR_CLAP].amp >> 9)) >> 15) * 3 >> 2) * g[DR_CLAP]) >> 6;
            dr[DR_CLAP].amp = decay_k(dr[DR_CLAP].amp, c < 2u * CLAP_GAP ? 400 : k[DR_CLAP]);
        }
        if (dr[DR_PERC].amp) {                  /* PERC: a short knock that falls a little in pitch */
            dr[DR_PERC].inc -= (uint32_t)((int32_t)(dr[DR_PERC].inc - dr[DR_PERC].end) >> 8);
            dr[DR_PERC].ph += dr[DR_PERC].inc;
            s += ((((sine(dr[DR_PERC].ph) * (dr[DR_PERC].amp >> 9)) >> 15) >> 1) * g[DR_PERC]) >> 6;
            dr[DR_PERC].amp = decay_k(dr[DR_PERC].amp, k[DR_PERC]);
        }
        if (crush) {                            /* LO-FI, HIP-HOP, INDUSTRIAL: fewer samples, coarse steps: an old sampler */
            if (++crush_n >= crush) { crush_n = 0; crush_v = s & ~(int32_t)cmask; }
            s = crush_v;
        }
        out[i] += s;
    }
}
