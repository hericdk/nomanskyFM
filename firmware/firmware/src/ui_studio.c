/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE logic of the SLOOP screens that No Man Sky FM keeps without their drawing (ui_nms.c draws): the REC button,
 * the hits and notes that light the keys, the hit levels. (SLOOP's DRUMS grid / kit screen is gone: the drum
 * track is stepped in the SEQ layer.) */
/* (2.7.1) the black keys pick the drum sound on the GRID page and in the SEQ layer (the pages: OCT- / OCT+), never heard;
 * the same key again: the other sound of its pair. F#3 .. F#5: KICK / KICK 2, SNARE / SNARE 2, RIM, CLAP, HAT / PEDAL,
 * OPEN HAT, LOW TOM / HI TOM, CRASH, RIDE, SHAKER / CONGA, COWBELL */
static const uint8_t DRUM_BLACK[11][2] = {{0, 1}, {2, 8}, {7, 7}, {3, 3}, {4, 6}, {5, 5}, {9, 10}, {11, 11}, {12, 12},
                                          {13, 14}, {15, 15}};
UI_SMALL static int32_t drum_black_of(uint32_t k)       /* key k: its black key 0..10, -1 a white key */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (PUNCH_BLACK[i] == k)
            return (int32_t)i;
    return -1;
}
UI_SMALL static uint32_t drum_black_key(uint32_t lane)    /* the key of the black key a sound is on */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (DRUM_BLACK[i][0] == lane || DRUM_BLACK[i][1] == lane)
            return PUNCH_BLACK[i];
    return PUNCH_BLACK[0];
}
UI_SMALL static void drum_black_pick(uint32_t b)          /* black key b: its sound, or the other of its pair */
{
    const uint8_t *g = DRUM_BLACK[b % 11u];
    pen_lane = pen_lane == g[0] && g[1] != g[0] ? g[1] : g[0];
    ui_say("SOUND ", LANE_NAME[pen_lane]);
    ui.force = 1;
}
UI_SMALL static void drum_hear(uint32_t lane) { if (drum_preview) audition_lane(lane); }   /* (2.7.1) menu DRUM PREVIEW */
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
UI_SMALL static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;                   /* (no message: the REC disc goes) */
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the hit levels, softest first (the SEQ layer: a held step + KNOB 2) */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
UI_SMALL static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}

/* PARTY (from winampFM, FX > PARTY): who is on stage. KNOB 1 FORM: SOLO one track, DUEL two, TRIAD three, PARTY all;
 * KNOB 2 LEAD: the first of them; KNOB 3 AUTOTURN: every 1 / 2 / 4 / 8 bars the stage turns (OFF: it stays); KNOB 4
 * STYLE: KEEP (the LEAD stays, the other places turn through the rest) or YOYO (the stage walks on and comes back). It
 * works through the solo mask (the others fade out). */
static uint8_t wp_form = 3, wp_lead, wp_turn, wp_style;
static uint32_t wp_p0;                        /* the turns count from PLAY */
static uint8_t wp_mask = 0xFF;                /* the stage last set (0xFF: not ours) */
static const char *const WP_FORM[4] = {"SOLO", "DUEL", "TRIAD", "PARTY"};
static const char *const WP_TURN[5] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const char *const WP_STYLE[2] = {"KEEP", "YOYO"};
UI_SMALL static uint32_t wp_bar(uint32_t *frac)  /* bars since PLAY, a little early (a note on the beat finds its track
                                                  * open); frac: into it, /1024 */
{
    uint32_t la = 2205u * (uint32_t)song.g[G_BPM], pos = clk_pos + la, beat = clk_beat + pos / BEAT_U;
    pos %= BEAT_U;
    if (frac)
        *frac = ((beat & 3u) * 256u) + pos / (BEAT_U / 256u);
    return beat / 4u;
}
UI_SMALL static uint32_t wp_stage(void)        /* the tracks on stage now (a mask of 4) */
{
    static const uint8_t BARS[5] = {0, 1, 2, 4, 8}, YO[6] = {0, 1, 2, 3, 2, 1};
    uint32_t n = wp_form + 1u, b = BARS[wp_turn % 5u], st = 0, j, m = 0, fr = 0;
    uint32_t p = song.playing ? wp_bar(&fr) * 1024u + fr : 0u, el = p > wp_p0 ? p - wp_p0 : 0u;
    if (b)
        st = el / (b * 1024u);
    if (wp_style) {                                 /* YOYO */
        uint32_t o = b ? YO[st % 6u] : 0u;
        for (j = 0; j < n; j++)
            m |= 1u << ((wp_lead + o + j) & 3u);
        return m;
    }
    m = 1u << (wp_lead & 3u);                       /* KEEP: the lead, and n - 1 of the other three, turning */
    for (j = 0; j + 1u < n; j++)
        m |= 1u << ((wp_lead + 1u + (st + j) % 3u) & 3u);
    return m;
}
UI_SMALL static void party_tick(void)          /* ui_draw, every frame: the stage turns on the bar */
{
    uint32_t m;
    if (!song.playing)
        wp_p0 = 0;
    m = wp_stage();
    if (m == 15u)
        m = 0u;                                     /* everyone: no solo */
    if (m == wp_mask || (wp_mask == 0xFFu && !m))   /* (PARTY and nothing of ours to undo: the solo stays the) */
        return;
    song.solo = (uint8_t)m;
    wp_mask = m ? (uint8_t)m : 0xFFu;
}
UI_SMALL static void party_edit(uint32_t slot, int32_t steps)   /* FX > PARTY: a step a detent */
{
    static const uint8_t TOP[4] = {3, 3, 4, 1};
    uint8_t *pv[4] = {&wp_form, &wp_lead, &wp_turn, &wp_style};
    *pv[slot & 3u] = (uint8_t)clamp((int32_t)*pv[slot & 3u] + (steps > 0 ? 1 : -1), 0, TOP[slot & 3u]);
    if (slot == 1u && song.playing) {               /* a new LEAD steps into the running turns, in time */
        uint32_t fr;
        wp_p0 = wp_bar(&fr) * 1024u;
    }
}
