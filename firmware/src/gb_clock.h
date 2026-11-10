/* SPDX-License-Identifier: GPL-3.0-only */
/* The groovebox's one master clock (docs/development/GROOVEBOX_PLAN.md, section 4). Included by quartet.c.
 *
 * It counts samples inside the audio interrupt, so every track hears the same clock and nothing can drift.
 * A step is a 16th note; each step has TPS ticks (note lengths end on a tick, swing will use them later).
 * The tick length keeps a fraction (Q8, 1/256 of a sample) so the tempo is exact at any BPM, 40..240.
 *
 * Each track has its own position in its own pattern (patterns can be 16, 32, 48 or 64 steps long). A track
 * changes to the pattern chosen for it (set.sel, written by the main loop) when its pattern comes round to step 1
 * again; while stopped, at once. trk_pos and play_pat say where each track is (written here only).
 *
 * The audio is rendered in segments between ticks: render up to the next tick, fire it, go on. Each track
 * renders into its own mono buffer (tb[track], gb_fx.h); at the end of the block fx_process() puts each through its
 * effects and mixes them into out. Each track module offers, called from here:
 *   xxx_step()           a new step starts: trigger what is on each track's trk_pos in its play_pat
 *   xxx_tick()           every tick: count note lengths down
 *   xxx_render(...)      add n frames of sound to its track buffer(s)
 *   xxx_stop()           the clock stopped: let go of the notes */

/* (TPS, the ticks per step, NSTEP and NPAT are set in quartet.c: the tracks need them too) */
#define BPM_MIN 40u
#define BPM_MAX 240u
#define TICK_Q8(bpm) (42336000u / (bpm))        /* samples a tick, times 256: 44100 * 60 / (bpm * 4 * TPS) * 256 */

/* main loop writes these (one writer each) */
static volatile uint32_t clk_run;               /* 1 = playing */
static volatile uint32_t clk_go;                /* +1 = start from step 1 now (write before clk_run = 1) */
static volatile uint32_t clk_tick_q8 = TICK_Q8(120u);
/* the interrupt writes these */
static volatile uint32_t clk_step;              /* steps since PLAY (for the beat display) */
static volatile uint32_t clk_sub;               /* the tick within the step, 0..TPS-1 (live recording rounds with it) */
/* (trk_pos[] and play_pat[], the step and the pattern each track plays now, are declared in quartet.c: the track
 * modules read them; only this file writes them) */

static void clock_set_bpm(uint32_t bpm)         /* main loop */
{
    if (bpm < BPM_MIN) bpm = BPM_MIN;
    if (bpm > BPM_MAX) bpm = BPM_MAX;
    clk_tick_q8 = TICK_Q8(bpm);
}

static uint32_t pat_len(uint32_t t, uint32_t p)  /* a pattern's length in steps (16, 32, 48 or 64) */
{
    uint32_t n = set.plen[t][p % NPAT];
    return n >= 16u && n <= NSTEP ? n : 16u;
}

/* SUPER SEQ: each track plays its 8 boxes of the song in a row, one time through the box's pattern each, then from
 * the first again. A blank box goes on with the box before it (A...B.A. plays AAAABBAA). X is a rest, and blanks
 * after an X rest too (A..X.... plays A A A then silence); so are blanks at the start (the track comes in later).
 * A rest lasts as long as the pattern before it (or the first one). Off: the patterns chosen with PRESETS, as ever. */
static uint32_t chain_pat(uint32_t t, uint32_t i, uint32_t *rest)   /* the pattern box i plays (or rests as long as) */
{
    const uint8_t *c = set.chain[set.sseq_sel % NCHAIN][t];
    int32_t j;
    uint32_t k;
    *rest = 1;
    for (j = (int32_t)(i % NLINK); j >= 0; j--) {
        if (c[j] == CH_REST) break;             /* an X: a rest, until the next pattern */
        if (c[j] && c[j] <= NPAT) { *rest = 0; return c[j] - 1u; }
    }
    for (; j >= 0; j--) if (c[j] && c[j] <= NPAT) return c[j] - 1u;   /* resting: as long as the pattern before */
    for (k = 0; k < NLINK; k++) if (c[k] && c[k] <= NPAT) return c[k] - 1u;   /* (or the first one) */
    return set.sel[t] % NPAT;                   /* (no pattern at all: the track rests) */
}
static uint32_t chain_len(void)                 /* the song's length, 1..8 boxes */
{
    uint32_t n = set.clen[set.sseq_sel % NCHAIN];
    return n >= 1u && n <= NLINK ? n : NLINK;
}
static void chain_go(uint32_t t, uint32_t i)    /* audio interrupt: track t starts box i */
{
    uint32_t rest;
    chain_at[t] = (uint8_t)i;
    play_pat[t] = (uint8_t)chain_pat(t, i, &rest);
    if (rest) chain_rest |= 1u << t; else chain_rest &= ~(1u << t);
}

static void seq_advance(int restart)            /* audio interrupt: every track one step on (or back to step 1) */
{
    uint32_t t;
    if (!set.sseq_on) chain_rest = 0;
    for (t = 0; t < NTRK; t++) {
        uint32_t pos = restart ? 0u : trk_pos[t] + 1u;
        if (pos >= pat_len(t, play_pat[t])) pos = 0;
        if (pos == 0u) {                        /* round to step 1: */
            if (set.sseq_on) chain_go(t, restart ? 0u : (chain_at[t] + 1u) % chain_len());   /* the song's next box */
            else {
                chain_at[t] = NLINK - 1u;       /* (SUPER SEQ switched on later: it starts at the first box) */
                if (set.sel[t] < NPAT) play_pat[t] = set.sel[t];   /* the chosen pattern starts */
            }
        }
        trk_pos[t] = (uint8_t)pos;
    }
}

/* SWING (set.swing 0..127): every second 16th starts late. The ticks of an on-beat step are made longer and those of
 * the off-beat step after it shorter by the same amount, so each pair of steps keeps its length: the beat, and every
 * track on it, stays exactly in time. Up to a third of a step: 50 % (straight) .. 66 % (a heavy shuffle). */
static uint32_t tick_len(uint32_t base_q8, uint32_t tick)
{
    uint32_t d = (base_q8 >> 8) * ((set.swing & 127u) * 85u / 127u);   /* base x up to 85/256 */
    return (tick / TPS) & 1u ? base_q8 - d : base_q8 + d;
}

static void clock_fire(uint32_t tick)           /* audio interrupt: one tick */
{
    clk_sub = tick % TPS;
    drums_tick();
    synth_tick();
    if (tick % TPS == 0u) {
        seq_advance(tick == 0u);
        clk_step = tick / TPS;
        drums_step();
        synth_step();
    }
    arp_tick();                                 /* (playing: on the patterns' own ticks; after the step, so it arpeggiates
                                                   the step's notes from their first tick) */
}

static void clock_part(int32_t *out, uint32_t n)   /* audio interrupt: up to FXN frames */
{
    static uint32_t go_done, was_run, pos_q8, tick, free_q8;
    uint32_t len_q8 = clk_tick_q8, run = clk_run, t, i = 0, k;
    for (t = 0; t < NTRK; t++) for (k = 0; k < n; k++) tb[t][k] = 0;   /* each track's own buffer (gb_fx.h) */
    if (!run) {                                 /* stopped: a pattern chosen starts at once (SUPER SEQ: the first box) */
        if (set.sseq_on) for (t = 0; t < NTRK; t++) chain_go(t, 0);
        else { chain_rest = 0; for (t = 0; t < NTRK; t++) if (set.sel[t] < NPAT) play_pat[t] = set.sel[t]; }
    }
    if (go_done != clk_go) { go_done = clk_go; pos_q8 = 0; tick = 0; clock_fire(0); }   /* PLAY: step 1 now */
    if (was_run && !run) { drums_stop(); synth_stop(); }
    was_run = run;
    if (pos_q8 > 2u * len_q8) pos_q8 = len_q8;  /* (the tempo jumped up a lot: one tick now, not a burst) */
    fxc_run = run; fxc_tick = tick; fxc_pos = pos_q8 < len_q8 ? pos_q8 : 0u; fxc_len = len_q8;   /* (for synced effects) */
    if (!run) {                                 /* stopped: the arpeggiator still ticks, at the tempo */
        free_q8 += n << 8;
        if (free_q8 > 4u * len_q8) free_q8 = len_q8;
        while (free_q8 >= len_q8) { free_q8 -= len_q8; arp_tick(); }
    }
    while (i < n) {
        uint32_t seg = n - i;
        if (run) {
            uint32_t todo;
            uint32_t cur = tick_len(len_q8, tick);   /* (this tick's length: swing stretches and shrinks them) */
            if (pos_q8 >= cur) { pos_q8 -= cur; clock_fire(++tick); continue; }
            todo = (cur - pos_q8 + 255u) >> 8;  /* whole samples to the next tick */
            if (todo < seg) seg = todo;
        }
        drums_render(&tb[T_DRUMS][i], seg);
        synth_render(tb, i, seg);
        i += seg;
        if (run) pos_q8 += seg << 8;
    }
    fx_process(out, n);                         /* the tracks through their effects, mixed into out */
}

static void clock_audio(int32_t *out, uint32_t n)   /* audio interrupt: the whole block */
{
    while (n) {
        uint32_t m = n > FXN ? FXN : n;
        clock_part(out, m);
        out += 2u * m;
        n -= m;
    }
}
