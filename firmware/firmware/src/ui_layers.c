/* SPDX-License-Identifier: GPL-3.0-only */
/* LAYERS, teenage-engineering style: hold a function button, touch a key (or turn a knob). While the
 * button is held the screen shows what the 16 white keys and KNOB 1..4 do now; tapped (pressed and
 * let go without touching anything) the button opens its pages as before.
 *   FX    punch-in effects (punch.c)       knobs: FILTER  DUST  DUCK
 *   EDIT  erase that sound / note (seq.c)  knobs: SHIFT  LENGTH x2 / half  TRANSPOSE   OCT- undo  OCT+ redo
 *   ARP   note repeat (seq.c roll)         knobs: RATE
 *   SEQ   steps 1..16 of the page          knobs: SOUND / NOTE  DIV  SWING  LENGTH;  a step key held:
 *         (black keys 1..4: pages)                SOUND / NOTE  LEVEL  RATCHET  NUDGE, PRESETS the value of a
 *                                                 parameter lock, ALGORITHM its parameter (the sound parameter
 *                                                 last touched first); OCT+ cycles the step's fill condition
 *                                                 (normal, fill only, no fill), OCT- clears its nudge, locks and
 *                                                 condition
 *   SEL   any key: the key of the song     knobs: CHORD  SCALE  KEYS  TRANSPOSE
 *   GLO   keys 1..4 mute, 5..8 solo,       knobs: the levels of tracks 1..4
 *         9 fill while held, 10 fill the next bar, the last white key: tap tempo
 *   SAVE  keys 1..4 play section A..D (on the next bar; two or more tapped while SAVE stays held: a chain of
 *         them, looped, each for its pattern's bars), 5..8 store the loop into A..D, 13 loop / song,
 *         14 SONG REC (the order you play becomes the song)
 * The keys' part runs in the audio ISR (seq.c layer_now: no lag, no lost press); the SEQ, SEL and
 * GLO keys come to the UI through seq.c lk_q. HOLD: REC held clears the track, SAVE held saves the
 * project (a ring fills; let go before and nothing happens). */
static const uint8_t LAYER_BTN[LY_COUNT] = {NB, B_FX, B_EDIT, B_ARP, B_SEQ, B_SCL, B_GLO, B_SAVE};
static void section_store(uint32_t s);                  /* project.c */
static void section_load(uint32_t s);
static uint8_t sec_armed;                               /* store over a used section: the key again within 3 s */
static uint32_t sec_armed_ms;
static uint8_t chain_tap[CHAIN_MAX], chain_taps;        /* the section keys tapped in this SAVE hold (the first
                                                         * is asked for at once; two or more: a chain on release) */
#define TAP_MS 450u                                     /* a press shorter than this, untouched: a tap */
#define SHOW_MS 140u                                    /* the layer shows after this (a tap does not flash it) */

/* the ISR's view of the panel: the layers' buttons, OCT- / OCT+, REC / PLAY of a free take */
UI_SMALL static void layers_init(void)
{
    uint32_t l;
    for (l = LY_FX; l < LY_COUNT; l++)
        ly_bit[l] = 1u << panel.btn[LAYER_BTN[l]];
    dyn_bit[0] = 1u << panel.btn[B_OCTDN];
    dyn_bit[1] = 1u << panel.btn[B_OCTUP];
    ft_btn_mask = 1u << panel.btn[B_REC];
    ft_drop_mask = 1u << panel.btn[B_PLAY];
    ui.lock_par = P_ED_FLT;                             /* the lock parameter until a sound knob is turned */
}

UI_SMALL static uint32_t key_of_white(uint32_t w) { return key_of_lane(w & 15u); }   /* white key w -> key index */

/* ------------------------------------------------------------ tools --- */
/* EDIT layer knobs; each a step of one undo (the layer's hold) */
UI_SMALL static void layer_undo_mark(track_t *t)
{
    if (!ui.step_sess)
        ui.step_sess = (undo_sess += 4u) | 3u;
    undo_mark(t, ui.step_sess);
}
UI_SMALL static void pattern_rotate(track_t *t, int32_t d)       /* every step one later (d > 0) / earlier */
{
    uint32_t len = trk_len(t), i;
    step_t keep;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0) {
        keep = t->step[len - 1u];
        for (i = len - 1u; i > 0; i--)
            t->step[i] = t->step[i - 1u];
        t->step[0] = keep;
    } else {
        keep = t->step[0];
        for (i = 0; i + 1u < len; i++)
            t->step[i] = t->step[i + 1u];
        t->step[len - 1u] = keep;
    }
    fm1_irq_on();
}
UI_SMALL static void pattern_length(track_t *t, int32_t d)       /* x2 (the pattern again after itself), or half */
{
    uint32_t len = trk_len(t), i;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0 && len * 2u <= NSTEP) {
        for (i = 0; i < len; i++)
            t->step[len + i] = t->step[i];
        t->p[P_SLEN] = (int16_t)(len * 2u);
    } else if (d < 0 && len >= 2u) {
        t->p[P_SLEN] = (int16_t)(len / 2u);
    }
    fm1_irq_on();                                       /* (no message: KNOB 2 shows the length) */
}
UI_SMALL static void pattern_transpose(track_t *t, int32_t d)    /* every note a semitone up / down (synth parts) */
{
    uint32_t i, j;
    if (is_drum(t))
        return;
    layer_undo_mark(t);
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < t->step[i].n && j < 4u; j++)
            t->step[i].note[j] = (uint8_t)clamp(t->step[i].note[j] + (d > 0 ? 1 : -1), 0, 127);
    fm1_irq_on();
}

/* ------------------------------------------------------------- SEQ --- */
/* a step key (white key w) of the SEQ layer, Elektron style: an empty step is set at once (drums: the
 * sound's lane; synth: the pen, the chord / note played last); a set one goes when its key is let go,
 * unless a knob changed it meanwhile (level, ratchet, note) */
static uint16_t step_pend_off;                         /* the step keys that clear their step when let go */
UI_SMALL static int step_is_on(const track_t *t, uint32_t idx)
{
    return is_drum(t) ? dstep_has(&t->dstep[idx], pen_lane) : step_on(&t->step[idx]);
}
UI_SMALL static void step_down(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ui.step_page * 16u + w;
    if (idx >= trk_len(t))
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (step_is_on(t, idx)) {
        step_pend_off |= (uint16_t)(1u << w);
        return;
    }
    layer_undo_mark(t);
    fm1_irq_off();
    if (is_drum(t)) {
        dstep_set(&t->dstep[idx], pen_lane, LV_NORM, 0);
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, n = pen_n ? pen_n : 1u;
        memset(s, 0, sizeof *s);
        for (i = 0; i < n && i < 4u; i++)
            s->note[i] = pen_n ? pen_note[i] : last_note;
        s->n = (uint8_t)(t->p[P_VOICE] == V_POLY ? n : 1u);
        s->time = ST_NOTE;
        s->vel = 100;
    }
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
}
UI_SMALL static void step_up(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ui.step_page * 16u + w;
    if (!((step_pend_off >> w) & 1u))
        return;
    step_pend_off &= (uint16_t)~(1u << w);
    if (idx >= trk_len(t) || !step_is_on(t, idx))
        return;
    layer_undo_mark(t);
    fm1_irq_off();
    if (is_drum(t))
        dstep_clr(&t->dstep[idx], pen_lane);
    else
        step_clear(&t->step[idx]);
    if (!is_drum(t) || !dstep_mask(&t->dstep[idx])) {   /* an empty step keeps no lock, no nudge, no condition */
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* the drum sound the SEQ layer sets (KNOB 1): heard when it changes */
UI_SMALL static void pen_lane_move(int32_t s)
{
    uint8_t l = (uint8_t)clamp(pen_lane + s, 0, DRUM_LANES - 1);
    if (l != pen_lane)
        audition_lane(l);
    pen_lane = l;
}
/* KNOB 1..4 with step keys held: their sound / note (drums: the sound's lane; synth: every note), level,
 * ratchet, nudge (1/64 of a step, - early + late). knob 4 = PRESETS: a lock of ui.lock_par on them, made at
 * the track's value, then moved */
UI_SMALL static void steps_held_edit(uint32_t knob, int32_t s)
{
    track_t *t = TSEL;
    uint32_t w, i;
    /* the accelerated turn once for every step held (a second call would see no time pass: x3 on the others) */
    int32_t sv = knob == 1u && !is_drum(t) ? accel(EN_K2, s, 126) : 0;
    int32_t sk = knob == 4u ? accel(EN_PRESET, s, lock_desc(t, ui.lock_par % P_COUNT)->max -
                                                  lock_desc(t, ui.lock_par % P_COUNT)->min) : 0;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;            /* (edited: kept when let go) */
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (knob == 3u) {                                 /* NUDGE: the whole step (drums: every lane) */
            t->micro[idx] = (int8_t)clamp(t->micro[idx] + s, MICRO_MIN, MICRO_MAX);
            continue;
        }
        if (knob == 4u) {                                 /* LOCK (PRESETS): ui.lock_par on this step */
            uint32_t id = ui.lock_par % P_COUNT;
            int k = lock_find(t, idx, id, 0);
            int32_t v = k >= 0 ? t->lock[k].val : t->p[id];
            if (!lock_set(t, idx, id, v + sk))
                ui_message(p_lockable(id) ? "NO LOCK LEFT" : "NOT LOCKABLE");
            continue;
        }
        if (is_drum(t)) {
            dstep_t *d = &t->dstep[idx];
            uint32_t lv = dstep_lvl(d, pen_lane), rt = dstep_rat(d, pen_lane);
            if (!dstep_has(d, pen_lane))
                continue;
            if (knob == 1u)
                lv = LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
            else
                rt = (uint32_t)clamp((int32_t)rt + s, 0, 3);
            dstep_set(d, pen_lane, lv, rt);
        } else {
            step_t *st = &t->step[idx];
            if (!step_on(st))
                continue;
            if (knob == 1u) {                             /* VEL (2.6): the step's velocity, 1..127; every note */
                int32_t v = clamp((int32_t)step_vel_shown(st) + sv, 1, 127);   /* at it (NORM) */
                st->vel = (uint8_t)v;
                st->lvl = 0;
                st->flags &= (uint8_t)~SF_ACCENT;         /* (ACC would play it at 127) */
                continue;
            }
            for (i = 0; i < st->n; i++) {
                uint32_t sh = 2u * i, lv = (st->lvl >> sh) & 3u, rt = (st->rat >> sh) & 3u;
                if (knob == 0u) {
                    st->note[i] = (uint8_t)clamp(st->note[i] + s, 0, 127);
                    continue;
                }
                if (knob == 1u)
                    lv = LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
                else
                    rt = (uint32_t)clamp((int32_t)rt + s, 0, 3);
                st->lvl = (uint8_t)((st->lvl & ~(3u << sh)) | lv << sh);
                st->rat = (uint8_t)((st->rat & ~(3u << sh)) | rt << sh);
            }
        }
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* ALGORITHM with step keys held: the lock parameter, through the lockable ones (the drum track: not the
 * engine parameters it has no use for), wrapping round */
UI_SMALL static void lock_par_step(int32_t s)
{
    const track_t *t = TSEL;
    uint32_t id = ui.lock_par % P_COUNT, guard = P_COUNT;
    do {
        id = (id + (uint32_t)P_COUNT + (uint32_t)(s > 0 ? 1 : -1)) % P_COUNT;
    } while (guard-- && (!p_lockable(id) || lock_desc(t, id)->max <= lock_desc(t, id)->min ||
                         (is_drum(t) && id > P_E0 && id <= P_E7)));
    ui.lock_par = (uint8_t)id;
}
/* OCT- with step keys held: their nudge, locks and fill condition go */
UI_SMALL static void steps_held_clear(void)
{
    track_t *t = TSEL;
    uint32_t w, n = 0;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        n += step_locked(t, idx) || step_fill(t, idx);
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(n ? "NUDGE, LOCKS, FILL CLEARED" : "NO NUDGE, LOCK OR FILL");
}
/* OCT+ with step keys held: their fill condition, round: normal -> fill only -> no fill (the first one's next) */
UI_SMALL static void steps_held_fill(void)
{
    track_t *t = TSEL;
    static const char *const MSG[3] = {"FILL: NORMAL", "FILL ONLY", "NO FILL"};
    uint32_t w, v = 0, first = 1;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (first)
            v = (step_fill(t, idx) + 1u) % 3u;
        first = 0;
        step_fill_set(t, idx, v);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(MSG[v % 3u]);
}

/* ------------------------------------------------------------- GLO --- */
UI_SMALL static void tap_tempo(void)
{
    uint32_t now = fm1_ms, i, sum = 0;
    if (ui.tap_n && now - ui.tap_ms[(ui.tap_n - 1u) & 3u] > 2000u)
        ui.tap_n = 0;                                   /* a pause: a new count */
    ui.tap_ms[ui.tap_n & 3u] = now;
    ui.tap_n++;
    if (ui.tap_n < 2u)
        return;
    {
        uint32_t n = ui.tap_n - 1u > 3u ? 3u : ui.tap_n - 1u;
        for (i = 0; i < n; i++)
            sum += ui.tap_ms[(ui.tap_n - 1u - i) & 3u] - ui.tap_ms[(ui.tap_n - 2u - i) & 3u];
        if (sum)
            song.g[G_BPM] = (int16_t)clamp((int32_t)((60000u * n + sum / 2u) / sum), 40, 240);
        ui.bpm_t = 40;
    }
}

/* a key of the layers the UI runs (seq.c lk_q): SEQ steps, SEL key, GLO mix */
UI_SMALL static void layer_key(uint32_t layer, uint32_t k, uint32_t down)
{
    int32_t w = punch_key(k);
    if (layer >= LY_COUNT)
        return;                                         /* (KB_GRID: the DRUMS grid page's, ui_studio.c) */
    if (!down) {
        if (w >= 0) {
            ui.step_held &= (uint16_t)~(1u << w);
            if (layer == LY_STEP)
                step_up((uint32_t)w);
            if (layer == LY_MIX && w == 8)
                fill_held = 0;                              /* GLO + key 9 let go: the fill ends */
        }
        return;
    }
    switch (layer) {
    case LY_STEP:
        if (w < 0) {                                    /* the first four black keys: pages 1..4 */
            static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
            if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < trk_len(TSEL))
                ui.step_page = (uint8_t)PG[k];
            return;
        }
        ui.step_held |= (uint16_t)(1u << w);
        step_down((uint32_t)w);
        return;
    case LY_SCALE: {                                    /* the key of the song: every synth part */
        uint32_t i, root = (53u + k) % 12u;
        for (i = 0; i < NPART; i++)
            trk[i].p[P_ROOT] = (int16_t)root;                /* (no message: the top shows its number) */
        return;
    }
    case LY_SONG: {                                     /* sections A..D: play, store; loop / song; SONG REC */
        char b[2] = {0, 0};
        if (w < 0)
            return;
        b[0] = (char)('A' + (w & 3));
        if (w < 4) {
            if (arrangement_clock.running) {
                ui_message("SONG PLAYS");
            } else if (!((arrangement_ready() >> w) & 1u)) {
                ui_say("EMPTY ", b);
            } else if (song.playing) {
                if (!chain_taps) {                          /* the first tap: as ever, and any chain stops */
                    fm1_irq_off();
                    chain_n = 0;
                    live_req = (int8_t)w;
                    fm1_irq_on();
                    ui_say("NEXT: ", b);
                } else {
                    ui.msg_t = 0;                           /* (the sub line shows the chain) */
                }
                if (chain_taps < CHAIN_MAX && (fm1_in.buttons & ly_bit[LY_SONG]))
                    chain_tap[chain_taps++] = (uint8_t)w;   /* (SAVE held: more taps make a chain) */
            } else {
                section_load((uint32_t)w);
                ui_say("LOADED ", b);
            }
        } else if (w < 8) {
            uint32_t s = (uint32_t)w - 4u;
            if (((arrangement_ready() >> s) & 1u) && !(sec_armed == s + 1u && fm1_ms - sec_armed_ms < 3000u)) {
                sec_armed = (uint8_t)(s + 1u);
                sec_armed_ms = fm1_ms;
                ui_say("AGAIN: ", b);
            } else {
                sec_armed = 0;
                section_store(s);
                ui_say("SAVED ", b);
            }
        } else if (w == 12) {
            if (srec) {
                ui_message("REC IS ON");
            } else {
                arrangement_enabled ^= 1u;
                ui_message(arrangement_enabled ? "SONG MODE" : "LOOP MODE");
            }
        } else if (w == 13) {
            if (srec) {
                fm1_irq_off();
                srec_stop();                            /* (playing: the order so far is the song) */
                fm1_irq_on();
            } else if (arrangement_clock.running) {
                ui_message("STOP FIRST");
            } else {
                arrangement_enabled = 0;
                srec = 1;
                ui_message(live_sec < 0 ? "PICK A PART" : "REC: NEXT BAR");
            }
        }
        return;
    }
    case LY_MIX:
        if (k == 1u) {                                      /* (2.6) the black key F#3: MIDI LEARN on / off */
            ml_toggle();
        } else if (w >= 0 && w < 4) {
            trk[w].p[P_MUTE] = (int16_t)!trk[w].p[P_MUTE];
        } else if (w >= 4 && w < 8) {
            song.solo ^= (uint8_t)(1u << (w - 4));
        } else if (w == 8) {
            fill_held = 1;                                  /* FILL while held */
        } else if (w == 9) {
            fill_arm = (uint8_t)!fill_arm;                  /* FILL NEXT BAR (again: cancelled) */
            ui_message(fill_arm ? "FILL: NEXT BAR" : "FILL BAR OFF");
        } else if (w == 15) {
            tap_tempo();
        }
        return;
    default:
        return;
    }
}

/* SAVE let go (ui_input.c): two or more section taps while it was held play as a chain, from the one
 * already asked for (the first), each for the bars of its pattern, looped; one tap was a plain jump */
UI_SMALL static void chain_release(void)
{
    uint32_t i;
    if (chain_taps >= 2u && song.playing && !arrangement_clock.running) {
        fm1_irq_off();
        for (i = 0; i < chain_taps && i < CHAIN_MAX; i++)
            chain_sec[i] = chain_tap[i];
        chain_i = 0;
        chain_bars = (uint8_t)section_bars(chain_tap[0]);   /* (the first plays already, or is asked for) */
        chain_n = (uint8_t)(chain_taps < CHAIN_MAX ? chain_taps : CHAIN_MAX);
        fm1_irq_on();
    }
    chain_taps = 0;
}
/* "chain A B B C": the chain being built (SAVE held) or playing */
UI_SMALL static void chain_sub(char *sub, uint32_t n)
{
    uint32_t i, m = chain_taps >= 2u ? chain_taps : chain_n, k = 5;
    str_cpy(sub, "chain", n);
    for (i = 0; i < m && i < CHAIN_MAX && k + 2u < n; i++) {
        sub[k++] = ' ';
        sub[k++] = (char)('A' + ((chain_taps >= 2u ? chain_tap[i] : chain_sec[i]) & 3u));
    }
    sub[k] = 0;
}

/* KNOB 1..4 while a layer is held: what the layer gives them (the page does not see them) */
UI_SMALL static void layer_knobs(uint32_t layer)
{
    uint32_t k;
    int32_t s;
    track_t *t = TSEL;
    if (layer == LY_STEP && ui.step_held) {              /* a step held: PRESETS the lock's value, ALGORITHM its parameter
                                                          * (neither browses a sound nor selects a track meanwhile) */
        if ((s = panel_enc(EN_PRESET)) != 0) {
            ui.layer_used = 1;
            steps_held_edit(4, s);
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            ui.layer_used = 1;
            lock_par_step(s);
        }
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.layer_used = 1;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        switch (layer) {
        case LY_FX:
            if (k == 0u)
                song.g[G_FILT] = (int16_t)clamp(song.g[G_FILT] + accel(EN_K1, s, 127), -64, 63);
            else if (k == 1u)
                song.g[G_DUST] = (int16_t)clamp(song.g[G_DUST] + accel(EN_K2, s, 127), 0, 127);
            else if (k == 2u)
                song.g[G_DUCK] = (int16_t)clamp(song.g[G_DUCK] + accel(EN_K3, s, 127), 0, 127);
            else                                        /* the selected track's FILTER (2.4) */
                t->p[P_TFLT] = (int16_t)clamp(t->p[P_TFLT] + accel(EN_K4, s, 127), -64, 63);
            break;
        case LY_ERASE:
            if (k == 0u)
                pattern_rotate(t, s);
            else if (k == 1u)
                pattern_length(t, s);
            else if (k == 2u)
                pattern_transpose(t, s);
            break;
        case LY_ROLL:
            if (k == 0u)
                song.g[G_ROLL] = (int16_t)clamp(song.g[G_ROLL] + s, 0, 4);
            break;
        case LY_STEP:
            if (ui.step_held) {                         /* SOUND / NOTE  LEVEL  RATCHET  NUDGE */
                if (k == 0u && is_drum(t))
                    pen_lane_move(s);
                else
                    steps_held_edit(k, s);
            } else if (k == 0u) {
                if (is_drum(t)) {
                    pen_lane_move(s);
                } else {
                    pen_note[0] = (uint8_t)clamp(pen_note[0] + s, 0, 127);
                    pen_n = 1;
                }
            } else if (k == 1u) {
                t->p[P_SDIV] = (int16_t)clamp(t->p[P_SDIV] + s, 0, (int32_t)NDIV_STEP - 1);   /* (up to 2BAR) */
            } else if (k == 2u) {
                t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, 100), 0, 100);
            } else {
                t->p[P_SLEN] = (int16_t)clamp(t->p[P_SLEN] + accel(EN_K4, s, 63), 1, NSTEP);
                if ((uint32_t)ui.step_page * 16u >= trk_len(t))
                    ui.step_page = (uint8_t)((trk_len(t) - 1u) / 16u);
            }
            break;
        case LY_SCALE:
            if (k == 0u) {
                if (!is_drum(t))
                    t->p[P_CHORD] = (int16_t)clamp(t->p[P_CHORD] + s, 0, 5);
            } else if (k == 1u) {
                uint32_t i;
                int16_t v = (int16_t)clamp(trk[0].p[P_SCALE] + s, 0, NSCALES - 1);
                for (i = 0; i < NPART; i++)
                    trk[i].p[P_SCALE] = v;
            } else if (k == 2u) {
                if (!is_drum(t))
                    t->p[P_QUANT] = (int16_t)clamp(t->p[P_QUANT] + s, 0, 2);
            } else if (!is_drum(t)) {
                t->p[P_TRANS] = (int16_t)clamp(t->p[P_TRANS] + s, -24, 24);
            }
            break;
        case LY_MIX: {
            int16_t *lv = k == TRK_DRUM ? &song.g[G_DRLVL] : &trk[k].p[P_LEVEL];
            *lv = (int16_t)clamp(*lv + accel(EN_K1 + k, s, 127), 0, 127);
            break;
        }
        default:
            break;
        }
    }
}
