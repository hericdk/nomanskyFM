/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* No Man Sky FM: which screen shows, once a frame. The screens themselves are in ui_nms.c. */
static void n_home(void);                               /* ui_nms.c */
static void n_page(void);
static void n_layer(void);
static void n_hold(void);
static void n_rec(void);
static void n_wavs(void);
static void rw_tick(void);
static int rw_show(void);
static void rw_toggle(void);
static int rw_view_on(void);
static int g_home_single(void);
static void n_menu(void);
static void vz_tick(void);                             /* (the visualisers' memory of the sound) */

UI_SMALL static uint32_t trk_level(uint32_t c)                /* LEVEL 0..127 (the drum track: GLO > DRUMS LEVEL) */
{
    return (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL]) & 127u;
}

/* the UI's timers, once a frame whatever the screen (a message, the BPM highlight, an armed GO, the
 * knob just turned) */
static void ui_timers(void)
{
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
}

static void g_step(void);                               /* ui_game.c: the game a frame on, its scene under a band */
static void g_backdrop(int32_t y, int32_t h);
static void ui_draw(void)
{
    ui.frame++;
    cv_keyreq = 0;
    pads_tick();
    vz_tick();
    rw_tick();                                          /* (each track's sound over its loop: the REC windows) */
    if (g_home_single())                                /* HOME tapped once on the game: the tracks' WAVs, open / closed */
        rw_toggle();
    if (ft_bars) {                                      /* a free take closed: the loop it made */
        char m[24];
        uint32_t n = ft_bars;
        ft_bars = 0;
        if (n == 0xFFu) {
            ui_message("TAKE DROPPED");
        } else {
            str_cpy(m, n == 1u ? "LOOP 1 BAR " : n == 2u ? "LOOP 2 BARS " : "LOOP 4 BARS ", sizeof m);
            fmt_int(m + str_len(m), song.g[G_BPM]);
            str_cpy(m + str_len(m), " BPM", sizeof m - str_len(m));
            ui_message(m);
        }
    }
    if (ui.menu || ui.hold_kind || ui.layer != LY_PLAY || rec_wait || ft_on || rw_show() || rw_view_on() || !(ui.home || cur_page()->scope == SC_TRK)) {
        /* No Man Sky FM: every screen but the game's own shows the game behind it (gfx.c cv_keyreq, a 40 % mesh), and
         * whatever its bands leave gets the game too (cv_fill_gaps): no black backgrounds */
        g_step();
        cv_backdrop = g_backdrop, cv_keyreq = 1, ui.force = 1;
    }
    if (!ui.menu && ui.hold_kind)                       /* REC / SAVE held to confirm */
        n_hold();
    else if (!ui.menu && ui.layer != LY_PLAY)           /* a layer held */
        n_layer();
    else if ((rec_wait || ft_on) && !ui.menu)           /* armed / free take */
        n_rec();
    else if ((rw_show() || rw_view_on()) && !ui.menu && (ui.home || cur_page()->scope == SC_TRK))
        n_wavs();                                       /* recording over the loop (and 6 s after): the WAV windows */
    else if (ui.menu)
        n_menu();
    else if (ui.home || cur_page()->scope == SC_TRK)    /* TRACKS */
        n_home();
    else {
        cursor_fix();
        n_page();
    }
    if (cv_keyreq)
        cv_fill_gaps();                                 /* (over the game: nothing left black) */
    ui_timers();
    ui.force = 0;
}
