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
static volatile uint32_t rep_len;               /* BEAT REPEAT while held: the slice in steps (8, 4, 2, 1; REP_HALF a 1/32); 0 off */
#define REP_HALF 0x80u
/* the interrupt writes these */
static volatile uint32_t clk_step;              /* steps since PLAY (for the beat display) */
static volatile uint32_t clk_pre;               /* main loop: steps of count-in before step 1 (read as PLAY starts) */
static volatile uint32_t clk_count;             /* the count-in's beats still to come (4 .. 1), 0: none */
static volatile uint32_t clk_ack;               /* the last clk_go the interrupt has started from */
static volatile uint32_t clk_click;             /* main loop: 1 = a click on every beat (recording) */
/* the click: a short blip, higher on the bar's first beat, into the mix after the pedals */
#define CK_LEN 1300                             /* (30 ms) */
static int32_t ck_at = -1, ck_age = CK_LEN;
static uint32_t ck_ph, ck_inc;
static void click_start(int32_t at, int accent)
{
    ck_at = at; ck_age = 0; ck_ph = 0;
    ck_inc = accent ? 214254000u : 146082000u;  /* (2.2 kHz, 1.5 kHz) */
}
static void click_render(int32_t *out, uint32_t n)
{
    uint32_t i = ck_at >= 0 ? (uint32_t)ck_at : 0u;
    for (; i < n && ck_age < CK_LEN; i++, ck_age++) {
        int32_t v = ((sine(ck_ph) >> 2) * (CK_LEN - ck_age)) / CK_LEN;
        ck_ph += ck_inc;
        out[2u * i] += v; out[2u * i + 1u] += v;
    }
    ck_at = -1;
}
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

/* BEAT REPEAT: the slice playing (on the grid of its length) goes round while rep_len is set. Every step the tracks'
 * places are kept (the last 16); the slice's start is found there, and every slice-length the tracks go back to it.
 * Let go: back to the start, then on, silently, as many steps as went by: the music is where it would have been. A
 * 1/32: each step plays twice (its half-way tick fires the step again). */
typedef struct { uint8_t pos[NTRK], pat[NTRK], cat[NTRK]; uint32_t rest; } seqst_t;
static seqst_t rep_hist[16], rep_snap, rep_true;   /* the last 16 steps; the slice's start; where the repeat began */
static uint32_t rep_on, rep_n, rep_L, rep_tn;   /* on; steps into the slice; its length; steps since it began */
static void seq_keep(seqst_t *s)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) { s->pos[t] = trk_pos[t]; s->pat[t] = play_pat[t]; s->cat[t] = chain_at[t]; }
    s->rest = chain_rest;
}
static void seq_back(const seqst_t *s)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) { trk_pos[t] = s->pos[t]; play_pat[t] = s->pat[t]; chain_at[t] = s->cat[t]; }
    chain_rest = s->rest;
}
static void repeat_step(uint32_t step, int restart)   /* a new step, after the tracks moved on to it */
{
    uint32_t L = rep_len == REP_HALF ? 0u : rep_len, k;   /* (a 1/32 plays each step twice: no slice to go round) */
    if (restart) rep_on = 0;
    else if (rep_on) {
        rep_tn++; rep_n++;
        if (!L) { seq_back(&rep_true); for (k = 0; k < rep_tn; k++) seq_advance(0); rep_on = 0; }   /* let go */
        else if (L == rep_L && rep_n % rep_L == 0u) seq_back(&rep_snap);   /* round again */
    }
    seq_keep(&rep_hist[step & 15u]);
    if (L && !restart && (!rep_on || L != rep_L)) {   /* start (or another length): the slice playing, from its start */
        uint32_t a = step / L * L;
        rep_snap = rep_hist[a & 15u];
        rep_n = step - a; rep_L = L;
        if (!rep_on) { rep_true = rep_snap; rep_tn = rep_n; rep_on = 1; }
    }
}

/* BROKEN CLOCK (a pedal, gb_fx.h): a track with it plays some of its steps late, by up to 3 ticks (3/4 of a step),
 * and now and then twice (a hiccup), like a worn mechanism. AMOUNT how late at most, CHANCE how often, HICCUP how
 * often twice. Its late steps wait here (cleared on STOP) */
static uint8_t brk_wait[NTRK], brk_again[NTRK];
static uint32_t brk_rnd = 0x2545F491u;
/* ---- the note pedals (gb_fx.h lists them): they change which step a track plays, or with what sound, just before it
 * plays it. Each works from the track's own position, so the pattern itself is never changed:
 *   WRONG STATION  now and then tunes the track to a neighbouring sound (drums: kit) for a while, with a burst of
 *                  static, then back: CHANCE, DRIFT (how far along the list), HOLD (how long away)
 *   MECH FAILURE   the instrument wears out: missed attacks, sticking notes (the step before again), notes out of tune,
 *                  until it suddenly works again: WEAR, STICK, RECOVERY
 *   MEMORY LOSS    each time the pattern comes round a few more of its steps are forgotten (the same ones: fragments
 *                  remain), down to FLOOR; RECALL: then it remembers again, in waves: SPEED, FLOOR, RECALL
 *   TAPE WORM      keeps the last steps played and now and then loops a fragment of them: CHANCE, LENGTH, REPEATS
 * WRONG STATION swaps the track's sound here, in the audio interrupt, and puts it back here too: on STOP, while the
 * sound editor is open (ws_hold), and before anything is saved (the music stops first), so a station is never kept */
static uint32_t kit_list(uint8_t *ids, uint32_t cur);
static uint32_t np_rnd = 0x6C8E9CF5u;
static uint32_t np_next(void) { np_rnd = np_rnd * 1664525u + 1013904223u; return np_rnd >> 8; }
static const uint8_t *note_ped(uint32_t t, uint32_t p)   /* the track's pedal p, if switched on: its knobs */
{
    uint32_t e;
    for (e = 0; e < NPED_SLOTS; e++) if (set.fx[t].on >> e & 1u && set.ped[t][e] % NPD == p) return set.pk[t][e];
    return 0;
}
/* WRONG STATION: the sound it went away from, kept to come back to */
static snd_t ws_snd[NSYN];
static uint8_t ws_glide[NSYN], ws_dkit, ws_away[NTRK], ws_alt[NTRK];
static uint16_t ws_left[NTRK];
static dsnd_t ws_dsnd[NDR];
static volatile uint8_t ws_hold;                /* main loop: the sound editor is open (the real sound, please) */
static void ws_home(uint32_t t)                 /* back to the real sound (unless it was changed meanwhile: then that stays) */
{
    if (!ws_away[t]) return;
    ws_away[t] = 0;
    if (t < NSYN) { if (set.snd[t].preset == ws_alt[t]) { set.snd[t] = ws_snd[t]; set.glide[t] = ws_glide[t]; } }
    else if (set.dkit == ws_alt[t]) { uint32_t d; for (d = 0; d < NDR; d++) set.dsnd[d] = ws_dsnd[d]; set.dkit = ws_dkit; }
}
static void ws_home_all(void) { uint32_t t; for (t = 0; t < NTRK; t++) ws_home(t); }
static void ws_step(uint32_t t, const uint8_t *k)   /* a step with WRONG STATION on: tune away, or count down to coming back */
{
    static const uint8_t HOLD[4] = {4, 8, 16, 32};   /* (1/4 bar .. 2 bars) */
    uint8_t ids[48];
    uint32_t n, i, at = 0, span, r = np_next(), id;
    if (ws_away[t]) {
        if (ws_left[t]) ws_left[t]--;
        if (!ws_left[t] || (t < NSYN ? set.snd[t].preset : set.dkit) != ws_alt[t]) { ws_home(t); ws_static[t] = 2000; }
        return;
    }
    if ((r & 0xFFFFu) >= (uint32_t)k[0] * 40u) return;   /* CHANCE: up to ~8 % a step */
    if (t < NSYN) n = sound_list(set.styp[t] % NSYN, ids, set.snd[t].preset);
    else { uint32_t m = kit_list(ids, set.dkit); for (n = i = 0; i < m; i++) if (ids[i] < NKIT) ids[n++] = ids[i]; }
    if (n < 2u) return;
    for (i = 0; i < n; i++) if (ids[i] == (t < NSYN ? set.snd[t].preset : set.dkit)) at = i;
    span = 1u + (uint32_t)k[1] * (n - 1u) / 127u / 2u;   /* DRIFT: the next sound .. halfway round the list */
    i = (r >> 16) % span + 1u;
    id = ids[(r >> 20 & 1u) ? (at + i) % n : (at + n - i % n) % n];
    if (id == (t < NSYN ? set.snd[t].preset : set.dkit)) return;
    if (t < NSYN) { ws_snd[t] = set.snd[t]; ws_glide[t] = set.glide[t]; sound_load(t, id); }
    else { uint32_t d; for (d = 0; d < NDR; d++) { ws_dsnd[d] = set.dsnd[d]; set.dsnd[d] = KITS[id][d]; } ws_dkit = set.dkit; set.dkit = (uint8_t)id; }
    ws_alt[t] = (uint8_t)id; ws_away[t] = 1;
    ws_left[t] = (uint16_t)(HOLD[k[2] & 3u] * (1u + (r >> 22 & 1u)));
    ws_static[t] = 2000;                        /* (~45 ms of static) */
}
/* MECH FAILURE: how worn it is (0 working .. 32767 falling apart), going up step by step until RECOVERY */
static int32_t mf_wear[NTRK];
/* MEMORY LOSS: how much is forgotten (0..255), going up each time round; RECALL: coming back down */
static int32_t ml_amt[NTRK];
static uint8_t ml_down[NTRK], ml_on[NTRK];
/* TAPE WORM: the last 32 steps played; a fragment playing: where it starts, how long, how far in, how many times */
static uint8_t tw_hist[NTRK][32], tw_n[NTRK], tw_from[NTRK], tw_len[NTRK], tw_at[NTRK], tw_rep[NTRK];
#ifdef NOTE_LOG
static void note_log(uint32_t t, uint32_t pos);  /* (a test build: which step each track played) */
#endif
static void track_play(uint32_t t)
{
#ifdef NOTE_LOG
    note_log(t, trk_pos[t]);
#endif
    if (t == T_DRUMS) drums_step(); else synth_step_t(t);
}
static void track_step(uint32_t t)              /* one track's step, through its note pedals */
{
    uint32_t pos = trk_pos[t], play = pos, len = pat_len(t, play_pat[t] % NPAT), before = vx_ages, v;
    const uint8_t *k;
    int32_t detune = 0;
    tw_hist[t][tw_n[t]++ & 31u] = (uint8_t)pos;
    if ((k = note_ped(t, PD_WRONG)) != 0) ws_step(t, k);
    else if (ws_away[t]) ws_home(t);
    if ((k = note_ped(t, PD_MEML)) != 0) {      /* MEMORY LOSS: SPEED, FLOOR, RECALL */
        int32_t top = 255 * (127 - (int32_t)k[1]) / 127;
        if (!ml_on[t]) { ml_on[t] = 1; ml_amt[t] = 0; ml_down[t] = 0; if (pos == 0u) ml_on[t] = 2; }
        if (pos == 0u && ml_on[t] == 2u) ml_on[t] = 3;   /* (the first time through: all of it, once) */
        else if (pos == 0u) {                   /* round again: forget some more (or remember) */
            if (!ml_down[t]) { ml_amt[t] += 6 + (int32_t)k[0] / 3; if (ml_amt[t] >= top) { ml_amt[t] = top; if (k[2]) ml_down[t] = 1; } }
            else { ml_amt[t] -= 4 + (int32_t)k[2] / 4; if (ml_amt[t] <= 0) { ml_amt[t] = 0; ml_down[t] = 0; } }
        }
        if ((pos ? (int32_t)((pos * 2654435761u + t * 0x9E3779B9u + 0x7F4A7C15u) >> 24) : 250) < ml_amt[t]) return;   /* (this step
                                                   forgotten; the downbeat last of all) */
    } else ml_on[t] = 0;
    if ((k = note_ped(t, PD_WORM)) != 0) {      /* TAPE WORM: CHANCE, LENGTH (1/8 .. a bar), REPEATS (1-4) */
        if (!tw_len[t] && tw_n[t] > 8u && (np_next() & 0xFFFFu) < (uint32_t)k[0] * 30u) {
            uint32_t L = 2u << (k[1] & 3u), back = L + np_next() % 16u;
            if (back > 31u) back = 31u;
            tw_len[t] = (uint8_t)L; tw_at[t] = 0; tw_rep[t] = (uint8_t)(1u + (k[2] & 3u));
            tw_from[t] = (uint8_t)(tw_n[t] - 1u - back);
        }
        if (tw_len[t]) {                        /* a fragment playing: its step instead */
            play = tw_hist[t][(uint8_t)(tw_from[t] + tw_at[t]) & 31u] % (len ? len : 1u);
            if (++tw_at[t] >= tw_len[t]) { tw_at[t] = 0; if (!--tw_rep[t]) tw_len[t] = 0; }
        }
    } else tw_len[t] = 0;
    if ((k = note_ped(t, PD_MECH)) != 0) {      /* MECH FAILURE: WEAR, STICK, RECOVERY */
        uint32_t r = np_next();
        int32_t fail;
        mf_wear[t] += 40 + (int32_t)k[0] * 2;
        if (mf_wear[t] > 32767) mf_wear[t] = 32767;
        if ((r & 0xFFFFu) < (uint32_t)k[2] * 8u) mf_wear[t] = 0;   /* (it works again, all at once) */
        fail = mf_wear[t] * (int32_t)k[0] / 127;
        if ((int32_t)((r >> 4) & 0x7FFFu) < fail / 3) return;   /* (a missed attack) */
        if ((int32_t)((r >> 9) & 0x7FFFu) < fail * (int32_t)k[1] / 254) play = (play + len - 1u) % (len ? len : 1u);   /* (it sticks) */
        detune = (int32_t)((r >> 3) % 65u) - 32;
        detune = detune * fail / 32767;         /* (out of tune: up to ~1/3 semitone either way) */
    } else mf_wear[t] = 0;
    trk_pos[t] = (uint8_t)play;
    track_play(t);
    trk_pos[t] = (uint8_t)pos;
    if (detune && t < NSYN)                     /* the notes just started a little off */
        for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++)
            if (vx[v].gate && (int32_t)(vx[v].age - before) > 0) {
                vx[v].inc = (uint32_t)((int32_t)vx[v].inc + (int32_t)(vx[v].inc >> 10) * detune);
                vx[v].tinc = (uint32_t)((int32_t)vx[v].tinc + (int32_t)(vx[v].tinc >> 10) * detune);
            }
}
static const uint8_t *broken_k(uint32_t t)      /* the track's BROKEN CLOCK knobs, if it has one on (or LOOSE: its LATE
                                                   as how late and how often, a hiccup now and then) */
{
    static uint8_t lk[3];
    uint32_t e;
    for (e = 0; e < NPED_SLOTS; e++) {
        uint32_t p = set.ped[t][e] % NPD;
        if (!(set.fx[t].on >> e & 1u)) continue;
        if (p == PD_BROKEN) return set.pk[t][e];
        if (p == PD_LOOSE && set.pk[t][e][0]) {
            lk[0] = set.pk[t][e][0]; lk[1] = (uint8_t)(set.pk[t][e][0] * 3u / 4u); lk[2] = (uint8_t)(set.pk[t][e][0] / 8u);
            return lk;
        }
    }
    return 0;
}
static void steps_fire(void)                    /* a new step: each track plays it, now or (broken) a little later */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        const uint8_t *k = broken_k(t);
        uint32_t r;
        brk_wait[t] = brk_again[t] = 0;
        if (!k) { track_step(t); continue; }
        brk_rnd = brk_rnd * 1664525u + 1013904223u; r = brk_rnd;
        if ((r >> 24) < (uint32_t)k[1] * 2u) brk_wait[t] = (uint8_t)(1u + (r >> 8) % (1u + (uint32_t)k[0] * 2u / 127u));
        else track_step(t);
        if (((r >> 16) & 255u) < (uint32_t)k[2] * 2u) brk_again[t] = (uint8_t)(brk_wait[t] + 2u);   /* (a hiccup: again) */
    }
}
static void broken_tick(void)                   /* every tick between steps: a late step's time come? */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        if (brk_wait[t] && --brk_wait[t] == 0u) track_step(t);
        if (brk_again[t] && --brk_again[t] == 0u) track_step(t);
    }
}

static void clock_fire(uint32_t tick)           /* audio interrupt: one tick */
{
    clk_sub = tick % TPS;
    drums_tick();
    synth_tick();
    if (tick % TPS == 0u) {
        seq_advance(tick == 0u);
        repeat_step(tick / TPS, tick == 0u);
        clk_step = tick / TPS;
        if (clk_click && (tick / TPS) % 4u == 0u) click_start((int32_t)blk_pos, (tick / TPS) % 16u == 0u);   /* (the beat) */
        steps_fire();
    } else {
        broken_tick();
        if (tick % TPS == TPS / 2u && rep_len == REP_HALF) { drums_step(); synth_step(); }   /* (a 1/32 repeat) */
    }
    arp_tick(tick);                             /* (playing: on the patterns' own ticks; after the step, so it arpeggiates
                                                   the step's notes from their first tick) */
}

/* the final mix: never clipped. A limiter: a peak over LIM_T turns the level down at once, just enough; it comes back
 * up over ~50 ms. Then a soft knee: above LIM_K the sound bends gently towards full scale instead of being cut flat
 * (what hard clipping does: a crackle on every loud beat) */
#define LIM_T 26000                             /* the line (~ -2 dB) */
#define LIM_K 24000                             /* where the knee starts */
static int32_t lim_g = 32768;                   /* the level now, Q15 (32768: as is) */
static inline int32_t lim_knee(int32_t v)
{
    int32_t a = v < 0 ? -v : v, o;
    if (a <= LIM_K) return v;
    o = a - LIM_K;                              /* over the knee: squeezed into what is left, 32767 - LIM_K */
    o = (o * (32767 - LIM_K)) / (o + (32767 - LIM_K));
    o += LIM_K;
    return v < 0 ? -o : o;
}
static void master_limit(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t g = lim_g;
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u], pk = l < 0 ? -l : l, pr = r < 0 ? -r : r;
        if (pr > pk) pk = pr;
        g += (32768 - g) >> 11;                 /* (back up, slowly) */
        if (((pk >> 4) * (g >> 4)) >> 7 > LIM_T) g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)pk);   /* (down, at once) */
        l = (int32_t)(((l >> 2) * (g >> 2)) >> 11); r = (int32_t)(((r >> 2) * (g >> 2)) >> 11);
        out[2u * i] = lim_knee(l); out[2u * i + 1u] = lim_knee(r);
    }
    lim_g = g;
}
static void clock_part(int32_t *out, uint32_t n)   /* audio interrupt: up to FXN frames */
{
    static uint32_t go_done, was_run, pos_q8, tick, free_q8, free_tick, pre_left;
    uint32_t len_q8 = clk_tick_q8, run = clk_run, t, i = 0, k;
    for (t = 0; t < NTRK; t++) for (k = 0; k < n; k++) tb[t][k] = 0;   /* each track's own buffer (gb_fx.h) */
    if (!run) {                                 /* stopped: a pattern chosen starts at once (SUPER SEQ: the first box) */
        if (set.sseq_on) for (t = 0; t < NTRK; t++) chain_go(t, 0);
        else { chain_rest = 0; for (t = 0; t < NTRK; t++) if (set.sel[t] < NPAT) play_pat[t] = set.sel[t]; }
    }
    blk_pos = 0;
    if (go_done != clk_go) {                    /* PLAY: step 1 now (or a count-in bar first: silent, a click a beat) */
        go_done = clk_go; pos_q8 = 0; tick = 0;
        pre_left = clk_pre * TPS; clk_pre = 0;
        if (pre_left) { clk_count = pre_left / (TPS * 4u); click_start(0, 1); }
        else clock_fire(0);
        clk_ack = go_done;
    }
    if (!run) { pre_left = 0; clk_count = 0; }
    if (was_run && !run) {                      /* STOP: the notes let go; the pedals fall silent (gb_fx.h); no late step */
        drums_stop(); synth_stop(); fx_flush_req = 1; fx_quiet = 1;
        ws_home_all();                          /* (WRONG STATION: the real sounds back) */
        for (t = 0; t < NTRK; t++) brk_wait[t] = brk_again[t] = 0;
    }
    was_run = run;
    if (ws_hold) ws_home_all();                /* (the sound editor open: the real sound) */
    if (pos_q8 > 2u * len_q8) pos_q8 = len_q8;  /* (the tempo jumped up a lot: one tick now, not a burst) */
    if (run) fx_quiet = 0;
    fxc_run = run; fxc_tick = tick; fxc_pos = pos_q8 < len_q8 ? pos_q8 : 0u; fxc_len = len_q8;   /* (for synced effects) */
    if (!run) {                                 /* stopped: the arpeggiator still ticks, at the tempo */
        free_q8 += n << 8;
        if (free_q8 > 4u * len_q8) free_q8 = len_q8;
        while (free_q8 >= len_q8) { free_q8 -= len_q8; arp_tick(free_tick++); }
    }
    while (i < n) {
        uint32_t seg = n - i;
        if (run) {
            uint32_t todo;
            uint32_t cur = tick_len(len_q8, tick);   /* (this tick's length: swing stretches and shrinks them) */
            if (pos_q8 >= cur) {
                pos_q8 -= cur; blk_pos = i;
                if (!pre_left) clock_fire(++tick);
                else if (--pre_left == 0u) { tick = 0; clk_count = 0; clock_fire(0); }   /* (the count-in done: step 1) */
                else if (++tick % (TPS * 4u) == 0u) { clk_count = pre_left / (TPS * 4u); click_start((int32_t)i, 0); }
                continue;
            }
            todo = (cur - pos_q8 + 255u) >> 8;  /* whole samples to the next tick */
            if (todo < seg) seg = todo;
        }
        blk_pos = i;
        drums_render(&tb[T_DRUMS][i], seg);
        synth_render(tb, i, seg);
        i += seg;
        if (run) pos_q8 += seg << 8;
    }
    fx_process(out, n);                         /* the tracks through their effects, mixed into out */
    click_render(out, n);                       /* (the count-in, the recording's click) */
    master_limit(out, n);
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
