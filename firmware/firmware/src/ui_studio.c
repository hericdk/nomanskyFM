/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE logic of the SLOOP screens that No Man Sky FM keeps without their drawing (ui_nms.c draws): the REC button,
 * the hits and notes that light the keys, the hit levels. (SLOOP's DRUMS grid / kit screen is gone: the drum
 * track is stepped in the SEQ layer.) */
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
