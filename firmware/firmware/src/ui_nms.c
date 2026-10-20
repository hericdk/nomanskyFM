/* SPDX-License-Identifier: GPL-3.0-only */
/* No Man Sky FM: every screen. Black, white and four colours (#006EA0 #009043 #53268D #D65432: track / knob 1..4).
 *   HOME   the game (ui_game.c), the music's head on top
 *   PAGE   a page of the selected track: the pages' names in a carousel (SELECT), four values, one per knob
 *   LAYER  a function button held: the 16 white keys as squares, KNOB 1..4 as bars
 *   HOLD   REC / SAVE held to confirm: a bar that fills
 *   REC    armed, the count-in, a free take
 *   MENU   HOME held: the only screen with words (its settings have no numbers)
 * A bar GROWS for a value along a range (from the middle when it goes both ways) and TURNS for a
 * choice among a few (a waveform, a mode). What is on or touched is never inverted: it gets a TEXTURE, a one-bit
 * pattern in a colour that lets the black through (dots, checks, lines, diagonals), and its text an outline.
 * Every band is drawn into the canvas (gfx.c) only when its signature changed. */
#define NW C_WHITE
#define NK C_BLACK
enum { BK_NONE, BK_GROW, BK_MID, BK_TURN };            /* how a knob is drawn */
enum { SCR_NONE, SCR_HOME, SCR_PAGE, SCR_LAYER, SCR_HOLD, SCR_REC, SCR_MENU };
static uint8_t n_scr;                                   /* the screen drawn last (another one: cleared) */

static uint32_t nh(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t nhs(uint32_t h, const char *s)
{
    while (*s)
        h = nh(h, (uint8_t)*s++);
    return nh(h, 0x55u);
}

static void n_screen(uint32_t s)                        /* a screen begins: a clean black one */
{
    if (n_scr == s)
        return;
    n_scr = (uint8_t)s;
    lcd_fill(0, 0, 240, 240, NK);
    ui.force = 1;
}

/* text in Terminus (FONT_S, 8 x 16), every pixel n x n; returns the end x */
static int32_t n_text(int32_t x, int32_t y, const char *s, uint32_t n, uint16_t c)
{
    const felucca_font_t *f = &FONT_S;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t i = gy * w + gx;
                if ((gd[i >> 3] >> (7u - (i & 7u))) & 1u)
                    cv_rect(x + ((int32_t)gx - f->pad) * (int32_t)n, y + (int32_t)(gy * n), (int32_t)n, (int32_t)n, c);
            }
        x += f->adv[gi] * (int32_t)n;
    }
    return x;
}
static int32_t n_w(const char *s, uint32_t n) { return text_w(&FONT_S, s) * (int32_t)n; }
/* the largest scale (of 1..max) at which s fits in w */
static uint32_t n_fit(const char *s, int32_t w, uint32_t max)
{
    while (max > 1u && n_w(s, max) > w)
        max--;
    return max;
}

/* the four colours: track 1 / KNOB 1 blue, 2 green, 3 purple, 4 orange */
#define NB RGB(0x00, 0x6E, 0xA0)
#define NG RGB(0x00, 0x90, 0x43)
#define NP RGB(0x53, 0x26, 0x8D)
#define NO RGB(0xD6, 0x54, 0x32)
static const uint16_t NCOL[4] = {NB, NG, NP, NO};
/* and four more for the world of the game (depth, water, warp): bright green, red, yellow, deep blue. GCOL: the
 * colours a planet can take (0..3 as NCOL, 4 EG, 5 ER, 6 EY, 7 EN) */
#define EG RGB(0x00, 0xE8, 0x00)
#define ER RGB(0xFF, 0x00, 0x46)
#define EY RGB(0xFF, 0xEB, 0x00)
#define EN RGB(0x19, 0x2B, 0x56)
static const uint16_t GCOL[8] = {NB, NG, NP, NO, EG, ER, EY, EN};

/* textures: 8 x 8 one-bit patterns, a colour over black (the black between: "transparency") */
enum { PT_DOTS, PT_GRID, PT_CHECK, PT_CHECK2, PT_HLINE, PT_VLINE, PT_DIAG, PT_CHEV, PT_N };
static const uint8_t TEX[PT_N][8] = {
    {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00},    /* dots */
    {0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00},    /* a grid of dots */
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},    /* checks */
    {0xCC, 0xCC, 0x33, 0x33, 0xCC, 0xCC, 0x33, 0x33},    /* big checks */
    {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00},    /* lines */
    {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA},    /* columns */
    {0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11},    /* diagonals */
    {0x18, 0x24, 0x42, 0x81, 0x18, 0x24, 0x42, 0x81},    /* chevrons */
};
static void n_tex(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t t, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++) {
        uint32_t row = TEX[t % PT_N][(uint32_t)j & 7u];
        for (i = x; i < x + w; i++)
            if ((row >> (7u - ((uint32_t)i & 7u))) & 1u)
                cv_pset(i, j, c);
    }
}

/* text with a black outline (legible over a texture) */
static int32_t n_texto(int32_t x, int32_t y, const char *s, uint32_t n, uint16_t c)
{
    n_text(x - 1, y, s, n, NK);
    n_text(x + 1, y, s, n, NK);
    n_text(x, y - 1, s, n, NK);
    n_text(x, y + 1, s, n, NK);
    n_text(x + 1, y + 1, s, n, NK);
    return n_text(x, y, s, n, c);
}

/* text at any size q (256 = 8 x 16, 512 = twice), so a title can grow smoothly */
static int32_t n_textq(int32_t x, int32_t y, const char *s, int32_t q, uint16_t c)
{
    const felucca_font_t *f = &FONT_S;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++) {
            int32_t y0 = y + (((int32_t)gy * q) >> 8), y1 = y + (((int32_t)gy + 1) * q >> 8);
            for (gx = 0; gx < w; gx++) {
                uint32_t i = gy * w + gx;
                if ((gd[i >> 3] >> (7u - (i & 7u))) & 1u) {
                    int32_t x0 = x + ((((int32_t)gx - f->pad) * q) >> 8), x1 = x + ((((int32_t)gx + 1 - f->pad) * q) >> 8);
                    cv_rect(x0, y0, x1 - x0, y1 - y0, c);
                }
            }
        }
        x += (f->adv[gi] * q) >> 8;
    }
    return x;
}
static int32_t n_wq(const char *s, int32_t q) { return (text_w(&FONT_S, s) * q) >> 8; }

/* a message: a band of orange checks, the words outlined */
static void n_msg(int32_t y, int32_t h)
{
    n_tex(0, y, 240, h, PT_CHECK2, NO);
    n_texto(120 - n_w(ui.msg, 1) / 2, y + (h - 16) / 2, ui.msg, 1, NW);
}

static void n_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)   /* an outline t thick */
{
    cv_rect(x, y, w, t, c);
    cv_rect(x, y + h - t, w, t, c);
    cv_rect(x, y, t, h, c);
    cv_rect(x + w - t, y, t, h, c);
}

static void n_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}

static void n_ring(int32_t cx, int32_t cy, int32_t r, int32_t t, uint16_t c)
{
    int32_t x, y, ri = r - t;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y;
            if (d <= r * r + r && d > ri * ri + ri)
                cv_pset(cx + x, cy + y, c);
        }
}

/* a thick line (3 px) */
static void n_stick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t a, b;
    for (a = -1; a <= 1; a++)
        for (b = -1; b <= 1; b++)
            cv_line(x0 + a, y0 + b, x1 + a, y1 + b, c);
}

/* the bars' two new tones of a knob colour (after the user's references): 0 deep (the cells to fill), 1 light */
static uint16_t n_tone(uint16_t c, int light)
{
    static const uint16_t K[4] = {NB, NG, NP, NO};
    static const uint16_t T[2][4] = {{RGB(0x18, 0x28, 0x8C), RGB(0x0A, 0x46, 0x28), RGB(0x28, 0x12, 0x50), RGB(0x6E, 0x1E, 0x16)},
                                     {RGB(0x60, 0xD8, 0xEC), RGB(0x78, 0xE6, 0x96), RGB(0xAA, 0x8C, 0xE6), RGB(0xF8, 0xD4, 0xA8)}};
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (c == K[i])
            return T[light ? 1 : 0][i];
    return light ? NW : EN;
}
/* a knob in w x h: a bar that grows (a range; BK_MID from the middle) or turns (a choice); r 0..1000 */
static void n_knob(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t kind, uint16_t fg, uint16_t fc)
{
    if (kind == BK_NONE)
        return;
    r = clamp(r, 0, 1000);
    if (kind == BK_TURN) {                              /* a bar turning on the bottom middle, left (0) .. right (1000) */
        int32_t cx = x + w / 2, cy = y + h, len = w / 2 - 4 < h + 6 ? w / 2 - 4 : h + 6;
        uint32_t a = (uint32_t)(512 + r * 512 / 1000) & 1023u;   /* (SINE: 1024 a turn, 0 = east, 256 = south) */
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_rect(cx - len - 2, cy - 1, 3, 3, fg);        /* the two ends */
        cv_rect(cx + len, cy - 1, 3, 3, fg);
        n_stick(cx, cy, cx + ((co * len) >> 15), cy + ((si * len) >> 15), fc);
        n_disc(cx, cy, 3, fg);
        return;
    }
    {   /* a range: a thin row of cells (1 px apart), no frame; the cells to fill in the space's navy, the filled ones a
         * mesh of the knob's colour (no solid colour); BK_MID fills from a tick in the middle */
        int32_t bh = h / 3 < 3 ? 3 : h / 3, by = y + (h - bh) / 2, cw = w >= 80 ? 4 : 3, n = (w + 1) / (cw + 1), i;
        int32_t f = n * r / 1000, mid = n / 2;
        for (i = 0; i < n; i++) {
            int32_t cx = x + i * (cw + 1), on = kind == BK_GROW ? i < f : (f >= mid ? i >= mid && i < f : i >= f && i < mid);
            if (!on) {
                cv_rect(cx, by, cw, bh, n_tone(fc, 0));
            } else {                                    /* lit: the core solid, a white line through it, and two rows
                                                         * above and below fading out (dither: 1/2, 1/4) */
                int32_t a, b;
                cv_rect(cx, by, cw, bh, fc);
                cv_rect(cx, by, cw, 1, n_tone(fc, 1));      /* (its light tone on top) */
                if (bh >= 3)
                    cv_rect(cx, by + bh / 2, cw, 1, NW);
                for (b = 1; b <= 2; b++)
                    for (a = 0; a < cw; a++)
                        if (((cx + a + b) & (b == 1 ? 1 : 3)) == 0)
                            cv_pset(cx + a, by - b, fc), cv_pset(cx + a, by + bh - 1 + b, fc);
            }
        }
        if (kind == BK_MID)
            cv_rect(x + mid * (cw + 1) - 1, by - 3, 1, bh + 6, fg);
    }
}

static uint32_t n_kind(const param_desc_t *d)
{
    if (!d)
        return BK_NONE;
    return d->fmt == F_ENUM ? BK_TURN : d->min < 0 ? BK_MID : BK_GROW;
}

/* a parameter's value, digits only: a choice is its number (1..), the rest keeps its digits and signs */
static void n_val(const param_desc_t *d, int32_t v, char *b)
{
    const char *u;
    char t[16];
    uint32_t i, k = 0;
    if (d->fmt == F_ENUM) {
        fmt_int(b, v - d->min + 1);
        return;
    }
    if (d->fmt == F_FILT || d->fmt == F_NOTE) {         /* LP / HP: minus / plus; a note: its MIDI number */
        if (d->fmt == F_FILT && v > 0) {
            b[0] = '+';
            fmt_int(b + 1, v);
        } else {
            fmt_int(b, v);
        }
        return;
    }
    param_format(d, v, t, &u);
    for (i = 0; t[i] && k < 6u; i++)
        if ((t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == '-' || t[i] == '+' || t[i] == '/')
            b[k++] = t[i];
    b[k] = 0;
    if (!k)
        fmt_int(b, v);
}

/* ------------------------------------------------------------ the bands --- */
/* the top band of the pages and layers, 0..31: the selected track, n/N, LOCK, transport; a message
 * replaces it (the one place with words besides the menu) */
static void n_top(const char *mid, uint32_t locked, uint32_t *cache)
{
    uint32_t sig = nhs(nh(nh(song.sel, song.playing), locked * 3u + (song.rec != 0) * 5u + mlu.on * 7u), mid);
    if (ui.msg_t)
        sig = nhs(sig ^ 0x9E3779B9u, ui.msg);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 32, NK);
    if (ui.msg_t) {
        n_msg(2, 28);
        cv_blit(0, 0);
        return;
    }
    {   /* the track: its colour, its number */
        char b[2] = {(char)('1' + song.sel), 0};
        cv_rect(4, 4, 24, 24, NCOL[song.sel & 3u]);
        n_text(12, 8, b, 1, NW);
    }
    if (mlu.on)                                         /* MIDI LEARN on (GLO + F#3): a ring beside it */
        n_ring(40, 16, 6, 2, NW);
    if (mid[0])
        n_text(120 - n_w(mid, 2) / 2, 0, mid, 2, NW);
    if (locked)                                         /* a layer locked open (HOME): a square of checks */
        n_tex(186, 8, 16, 16, PT_CHECK, NW), n_frame(186, 8, 16, 16, 1, NW);
    if (song.rec)
        n_disc(214, 16, 7, NW);
    else if (song.playing) {
        int32_t i;
        for (i = 0; i < 14; i++)
            cv_rect(208 + i, 9 + i / 2, 1, 15 - i, NW);
    } else {
        n_frame(208, 9, 14, 14, 2, NW);
    }
    cv_blit(0, 0);
}

/* the knob band at y0 (h 52): four columns, a number over a bar in the knob's colour; the knob just turned: a texture
 * behind (each knob its own) */
static const uint8_t KTEX[4] = {PT_DIAG, PT_CHECK2, PT_DOTS, PT_CHEV};
static void n_vz(uint32_t st, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, int32_t k);
/* vz: each knob's visual (VZ_*) and its variant, shown behind it while it turns (0xFF: none) */
static void n_knobs(int32_t y0, char v[4][8], const int32_t r[4], const uint8_t kind[4], const uint8_t vz[4][2], uint32_t *cache)
{
    uint32_t k, sig = 7u;
    for (k = 0; k < 4u; k++)
        sig = nhs(nh(nh(sig, (uint32_t)r[k] / 4u), kind[k] * 3u + (ui.hot_t && ui.hot_col == k)), v[k]);
    if (ui.hot_t && vz[ui.hot_col & 3u][0] != 0xFFu)
        sig = nh(sig, ui.frame);                        /* (a visual: every frame) */
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 52, NK);
    for (k = 0; k < 4u; k++) {
        int32_t x = (int32_t)k * 60, hot = ui.hot_t && ui.hot_col == k;
        uint32_t n;
        if (kind[k] == BK_NONE && !v[k][0])
            continue;
        if (hot && vz[k][0] != 0xFFu)
            n_vz(vz[k][0], x + 2, 0, 56, 52, r[k], NCOL[k], vz[k][1]);
        else if (hot)
            n_tex(x + 2, 0, 56, 52, KTEX[k], NCOL[k]);
        n = n_fit(v[k], 52, 2);
        n_texto(x + 30 - n_w(v[k], n) / 2, n == 2u ? 2 : 10, v[k], n, NW);
        n_knob(x + 6, 36, 48, 12, r[k], kind[k], NW, NCOL[k]);
    }
    cv_blit(0, (uint32_t)y0);
}

/* ---------------------------------------------------------- the visualisers --- */
/* While a knob turns, what is behind its value moves with the sound: the mix as it leaves (audio.c scope_buf, after
 * every effect, MASTER as if all the way up), drawn the way the thing touched changes it: a filter smooths the wave
 * and draws its curve, a drive clips it, a delay repeats its peaks, a reverb is a cloud, an envelope its shape... One
 * look per kind of parameter, in the knob's colour, white where the knob acts; one per punch-in effect (FX held). */
enum { VZ_WAVE, VZ_DRIVE, VZ_FILTER, VZ_ENV, VZ_LFO, VZ_ECHO, VZ_SPACE, VZ_CHORUS, VZ_PAN, VZ_DUST, VZ_DUCK, VZ_PITCH,
       VZ_STEPS, VZ_CRUSH, VZ_GATE, VZ_BARS,
       VZ_LOOP, VZ_REV, VZ_STOP, VZ_FREEZE, VZ_KILL, VZ_FADE };   /* (the punch-in effects only) */
static uint32_t g_hash_ui(uint32_t h) { h = (h ^ 61u) ^ (h >> 16); h *= 9u; h ^= h >> 4; h *= 0x27d4eb2du; return h ^ (h >> 15); }
static int32_t vz_s(uint32_t back) { return scope_buf[(scope_w - 1u - back) & (SCOPE_N - 1u)]; }   /* Q15, back samples */
static int32_t vz_gain(void)                            /* Q8: the loudest of the last 256 to 0.45 of the height */
{
    int32_t i, pk = 1;
    for (i = 0; i < 256; i++) {
        int32_t a = vz_s((uint32_t)i);
        if (a < 0)
            a = -a;
        if (a > pk)
            pk = a;
    }
    return pk < 900 ? 0 : (int32_t)(115u * 32768u / (uint32_t)pk);   /* (silence: flat) */
}
/* a trace through the columns: ys[i] (from y), clipped to the box */
static void vz_trace(int32_t x, int32_t y, int32_t w, int32_t h, const int16_t *ys, uint16_t c)
{
    int32_t i, prev = ys[0], m = h / 2;
    for (i = 0; i < w; i++) {
        int32_t a = clamp(ys[i], 0, h - 1), lo = a < prev ? a : prev, hi = a < prev ? prev : a, j;
        lo = clamp(lo, 0, h - 2), hi = clamp(hi, 0, h - 2);
        cv_rect(x + i, y + lo, 1, hi - lo + 2, c);      /* 2 px thick */
        for (j = a < m ? a + 3 : m; j < (a < m ? m : a - 2); j += 2)   /* a dotted fill to the middle */
            if (((uint32_t)(i + j) & 1u) == 0u)
                cv_pset(x + i, y + j, c);
        prev = a;
    }
}
static int32_t vz_y(int32_t s, int32_t g, int32_t h) { return h / 2 - ((s * g >> 8) * h >> 8) / 128; }   /* sample -> row */

/* the sound over time: the peak of each frame (~17 ms), the last 256 (~4 s); ui_draw feeds it every frame */
static int16_t vz_hist[256];
static uint8_t vz_hi;
static uint32_t vz_last_note;                           /* (ms) a key last held (the visuals by themselves) */
static void vz_tick(void)
{
    int32_t i, pk = 0;
    if (fm1_in.notes)
        vz_last_note = fm1_ms;
    for (i = 0; i < 380; i++) {                         /* (one frame of the mix: 22 kHz x 17 ms) */
        int32_t a = vz_s((uint32_t)i);
        pk = (a < 0 ? -a : a) > pk ? (a < 0 ? -a : a) : pk;
    }
    vz_hist[vz_hi++] = (int16_t)(pk > 32767 ? 32767 : pk);
}
static int32_t vz_h(int32_t back) { return back < 0 || back > 255 ? 0 : vz_hist[(uint8_t)(vz_hi - 1u - (uint32_t)back)]; }
/* the overview: column i shows the frame src[i] back (-1: nothing) at amp[i] (Q8), mirrored round the middle, solid;
 * the newest column white */
static void vz_over(int32_t x, int32_t y, int32_t w, int32_t h, const int16_t *src, const int16_t *amp, uint16_t c)
{
    int32_t i, mx = 1200;
    for (i = 0; i < w; i++)
        if (src[i] >= 0 && vz_h(src[i]) > mx)
            mx = vz_h(src[i]);
    for (i = 0; i < w; i++) {
        int32_t half;
        if (src[i] < 0)
            continue;
        half = vz_h(src[i]) * (h / 2 - 2) / mx * amp[i] >> 8;
        if (half < 1)
            half = 1;
        cv_rect(x + i, y + h / 2 - half, 1, 2 * half, src[i] == 0 ? NW : c);
    }
}

static int16_t vz_frz[120];                             /* FREEZE: the wave when it was pressed */

/* the visuals by themselves (menu VISUAL): a note played, or the loop playing, and the pages' four values show their
 * visuals, the numbers see-through. settings.zoom (the old ZOOM's word, a setting of the FM-1): 0x8000 | loop << 8 |
 * note, each 0..21: 0 OFF, else the number's opacity, (v - 1) x 5 %. Not set yet: a note 15 %, the loop 50 % */
static const uint8_t BAYER_N[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint32_t vz_set(uint32_t which)                  /* 0 a note, 1 the loop */
{
    if (!(settings.zoom & 0x8000u))
        return which ? 11u : 4u;
    return (settings.zoom >> (which * 8u)) & 31u;
}
static void vz_put(uint32_t which, uint32_t v)
{
    uint32_t n = vz_set(0), l = vz_set(1);
    if (which)
        l = v;
    else
        n = v;
    settings.zoom = 0x8000u | l << 8 | n;
}
static int32_t vz_alpha(void)                           /* the numbers' opacity (0..16) while the visuals play; -1: they don't */
{
    uint32_t v = vz_set(0);                             /* (the user's choice: the note's look all the time, visual behind,
                                                         * the numbers see-through; the menu's NOTE value sets how much) */
    return v ? (int32_t)((v - 1u) * 5u * 16u / 100u) : -1;
}
/* text with only a share of its pixels (a16 of 16, ordered dither): see-through over a visual */
static void n_textd(int32_t x, int32_t y, const char *s, uint32_t n, uint16_t c, int32_t a16)
{
    const felucca_font_t *f = &FONT_S;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t i = gy * w + gx;
                if ((gd[i >> 3] >> (7u - (i & 7u))) & 1u) {
                    int32_t px = x + ((int32_t)gx - f->pad) * (int32_t)n, py = y + (int32_t)(gy * n), u, v;
                    for (v = 0; v < (int32_t)n; v++)
                        for (u = 0; u < (int32_t)n; u++)
                            if ((int32_t)BAYER_N[(py + v) & 3][(px + u) & 3] < a16)
                                cv_pset(px + u, py + v, c);
                }
            }
        x += f->adv[gi] * (int32_t)n;
    }
}
static int8_t vz_frz_on = -1;

/* the visual `st` in (x, y, w, h); r: the knob 0..1000; c its colour; k: a variant (which value of the page, a count) */
static void n_vz(uint32_t st, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, int32_t k)
{
    static int16_t ys[240], y2[240];
    int32_t g = vz_gain(), i, mid = h / 2, t = (int32_t)fm1_ms;
    uint32_t step = 240u / (uint32_t)w;                 /* samples a column (the last ~240 samples: ~11 ms) */
    if (w > 240)
        w = 240;
    r = clamp(r, 0, 1000);
    if (!step)
        step = 1;
    for (i = 0; i < w; i += 4)                          /* the middle, dotted */
        cv_pset(x + i, y + mid, NP);
    switch (st) {
    case VZ_DRIVE: {                                    /* the wave pushed by the knob, its tops cut flat (white) */
        int32_t lim = h * 42 / 100, gain = 256 + r * 6 * 256 / 1000;
        for (i = 0; i < w; i++) {
            int32_t v = h / 2 - ys[0] * 0, s = vz_s((uint32_t)(w - 1 - i) * step);
            v = (s * g >> 8) * h / 256 / 128 * gain / 256;
            if (v > lim || v < -lim) {
                cv_rect(x + i, y + mid - (v > 0 ? lim : -lim) - 1, 1, 3, NW);
                v = v > 0 ? lim : -lim;
            }
            ys[i] = (int16_t)(mid - v);
        }
        vz_trace(x, y, w, h, ys, c);
        break;
    }
    case VZ_FILTER: {                                   /* the wave through the filter, its curve in white; k: 1 = both
                                                         * ways (left low-pass, right high-pass, middle open) */
        int32_t lp = k ? (r < 480 ? 480 - r : 0) : 1000 - r, hp = k && r > 520 ? r - 520 : 0, n, cx;
        n = 1 + lp / 25;
        for (i = 0; i < w; i++) {
            int32_t j, acc = 0, base = (w - 1 - i) * (int32_t)step;
            for (j = 0; j < n; j++)
                acc += vz_s((uint32_t)(base + j));
            acc /= n;
            if (hp)
                acc = (vz_s((uint32_t)base) - acc) * (1 + hp / 60);
            ys[i] = (int16_t)vz_y(acc, g, h);
        }
        vz_trace(x, y, w, h, ys, c);
        cx = k ? (r < 480 ? r * w / 480 : r > 520 ? (r - 520) * w / 480 : w) : r * w / 1000;   /* the curve */
        for (i = 0; i < w; i++) {
            int32_t d = hp ? cx - i : i - cx, cy = d <= 0 ? h / 5 : h / 5 + d * d * h / (w * w / 3 + 1);
            if (k && r >= 480 && r <= 520)
                cy = h / 5;
            if (cy < h - 1)
                cv_pset(x + i, y + cy, NW);
        }
        break;
    }
    case VZ_ENV: {                                      /* the envelope of the track: attack, decay, sustain, release;
                                                         * the part the knob moves in white; a dot runs along it */
        const track_t *tt = TSEL;
        int32_t a = 4 + tt->p[P_ATK] / 3, d = 4 + tt->p[P_DEC] / 3, s = tt->p[P_SUS], rl = 4 + tt->p[P_REL] / 3, hold = 18;
        int32_t tot = a + d + hold + rl, px[5], py[5], seg;
        px[0] = 0, py[0] = h - 2;
        px[1] = a * w / tot, py[1] = 2;
        px[2] = (a + d) * w / tot, py[2] = h - 2 - s * (h - 4) / 127;
        px[3] = (a + d + hold) * w / tot, py[3] = py[2];
        px[4] = w - 1, py[4] = h - 2;
        for (seg = 0; seg < 4; seg++) {
            int32_t xx;
            uint16_t col = (seg == k || (k == 3 && seg == 3)) ? NW : c;
            for (xx = px[seg]; xx <= px[seg + 1]; xx++) {
                int32_t yy = px[seg + 1] == px[seg] ? py[seg] : py[seg] + (py[seg + 1] - py[seg]) * (xx - px[seg]) / (px[seg + 1] - px[seg]);
                cv_rect(x + xx, y + yy, 1, 2, col);
                if (!(xx & 1))
                    cv_rect(x + xx, y + yy + 3, 1, h - yy - 4 > 0 ? (h - yy - 4) / 3 : 0, c);   /* a light fill */
            }
        }
        {
            int32_t ph = (t / 8) % (w + 20);
            if (ph < w)
                for (seg = 0; seg < 4; seg++)
                    if (ph >= px[seg] && ph <= px[seg + 1]) {
                        int32_t yy = px[seg + 1] == px[seg] ? py[seg] : py[seg] + (py[seg + 1] - py[seg]) * (ph - px[seg]) / (px[seg + 1] - px[seg]);
                        n_disc(x + ph, y + yy, 3, NW);
                    }
        }
        break;
    }
    case VZ_LFO: {                                      /* the LFO's shape scrolling at its rate (two cycles in view) */
        int32_t wave = TSEL->p[P_LWAVE], rate = 1 + TSEL->p[P_LRATE];
        uint32_t ph0 = (uint32_t)(t * rate / 60);
        for (i = 0; i < w; i++) {
            uint32_t ph = (ph0 + (uint32_t)(i * 2048 / w)) & 1023u;
            int32_t v;
            switch (wave % 5) {
            case 0: v = SINE[ph] >> 7; break;                                   /* sine */
            case 1: v = ph < 512u ? (int32_t)ph - 256 : 767 - (int32_t)ph; break;   /* triangle */
            case 2: v = 255 - (int32_t)(ph >> 1); break;                        /* saw */
            case 3: v = ph < 512u ? 200 : -200; break;                          /* square */
            default: v = (int32_t)(g_hash_ui(ph >> 7) & 511u) - 256; break;     /* random */
            }
            ys[i] = (int16_t)(mid - v * (h * 42 / 100) / 256);
        }
        vz_trace(x, y, w, h, ys, c);
        break;
    }
    case VZ_ECHO: {                                     /* the last peak and its repeats, each softer, scrolling */
        int32_t pk = 0, n, sp, fb = k == 1 ? r : song.g[G_DFDBK] * 1000 / 120, hgt;
        for (i = 0; i < 24; i++)
            pk = vz_h(i) > pk ? vz_h(i) : pk;
        hgt = pk * (h - 4) / 32768 + h / 4;
        sp = k == 2 ? 10 + r * (w / 3) / 1000 : w / 6;  /* (TIME: the spacing) */
        if (k == 0)                                     /* (a send: how loud the first echo is) */
            hgt = hgt * r / 1000;
        for (n = 0; n < 12 && hgt > 1; n++) {
            int32_t xx = (n * sp + (t / 20) % sp) % (w + sp) - 2;
            cv_rect(x + xx, y + mid - hgt / 2, 3, hgt, n ? c : NW);
            hgt = hgt * (200 + fb * 6 / 10) / 1000;
        }
        break;
    }
    case VZ_SPACE: {                                    /* a cloud of dots: how many (the amount), how wide (the size),
                                                         * the newest white */
        int32_t pk = 0, n, spread = w * (300 + r * 7 / 10) / 1000;
        for (i = 0; i < 64; i++) {
            int32_t a = vz_s((uint32_t)i);
            pk = (a < 0 ? -a : a) > pk ? (a < 0 ? -a : a) : pk;
        }
        n = 8 + r * 70 / 1000 + (pk >> 10);
        for (i = 0; i < n; i++) {
            uint32_t hh = g_hash_ui((uint32_t)i * 977u + (uint32_t)(t / 90));
            int32_t xx = w / 2 + (int32_t)(hh % (uint32_t)(spread + 1)) - spread / 2;
            int32_t yy = mid + (int32_t)((hh >> 10) % (uint32_t)(h * spread / w + 1)) - h * spread / w / 2;
            cv_rect(x + xx, y + yy, 2, 2, i < 4 ? NW : c);
        }
        n_ring(x + w / 2, y + mid, 3 + (int32_t)((uint32_t)(t / 30) % (uint32_t)(spread / 2 + 1)), 1, c);
        break;
    }
    case VZ_CHORUS: {                                   /* the wave and a copy of it drifting (white), as a chorus or a
                                                         * flanger hears it */
        int32_t dep = 2 + r * 24 / 1000, off = (SINE[(uint32_t)(t / 3) & 1023u] * dep) >> 15;
        for (i = 0; i < w; i++) {
            ys[i] = (int16_t)vz_y(vz_s((uint32_t)(w - 1 - i) * step), g, h);
            y2[i] = (int16_t)vz_y(vz_s((uint32_t)clamp((w - 1 - i) * (int32_t)step + dep + off, 0, 500)), g, h);
        }
        vz_trace(x, y, w, h, ys, c);
        for (i = 0; i < w; i += 2)
            cv_pset(x + i, y + clamp(y2[i], 0, h - 1), NW);
        break;
    }
    case VZ_PAN: {                                      /* the sound as a ball between left and right; the two sides'
                                                         * levels as bars */
        int32_t pk = 0, rad, bx;
        for (i = 0; i < 128; i++) {
            int32_t a = vz_s((uint32_t)i);
            pk = (a < 0 ? -a : a) > pk ? (a < 0 ? -a : a) : pk;
        }
        rad = 3 + (pk * g >> 8) * (h / 3) / 32768 / 1;
        rad = clamp(rad, 3, h / 3);
        bx = 6 + r * (w - 12) / 1000;
        n_disc(x + bx, y + mid, rad, c);
        n_disc(x + bx, y + mid, 2, NW);
        cv_rect(x + 1, y + h - 2 - (1000 - r) * (h - 4) / 1000 * rad / (h / 3), 3, (1000 - r) * (h - 4) / 1000 * rad / (h / 3) + 1, c);
        cv_rect(x + w - 4, y + h - 2 - r * (h - 4) / 1000 * rad / (h / 3), 3, r * (h - 4) / 1000 * rad / (h / 3) + 1, c);
        break;
    }
    case VZ_DUST: {                                     /* the wave, faint, and the crackle: specks, more as it turns up */
        int32_t n = 4 + r * 90 / 1000;
        for (i = 0; i < w; i += 2)
            cv_pset(x + i, y + clamp(vz_y(vz_s((uint32_t)(w - 1 - i) * step), g, h), 0, h - 1), c);
        for (i = 0; i < n; i++) {
            uint32_t hh = g_hash_ui((uint32_t)i * 131u + ui.frame * 7919u);
            cv_rect(x + (int32_t)(hh % (uint32_t)w), y + (int32_t)((hh >> 12) % (uint32_t)h), 1 + (int32_t)((hh >> 24) & 1u), 1,
                    (hh >> 20) & 3u ? c : NW);
        }
        break;
    }
    case VZ_DUCK: {                                     /* the sound over time pumping: down on each beat, back up (the
                                                         * curve in white); k 1: a wobble (fast, round) */
        uint32_t bu = BEAT_U, now = clk_pos;
        int32_t bms = song.g[G_BPM] > 0 ? 60000 / song.g[G_BPM] : 500;
        for (i = 0; i < w; i++) {
            int32_t back = w - 1 - i, e, ph = (int32_t)(now * 256u / bu) - back * 17 * 256 / bms;
            ph = ((ph % 256) + 256) % 256;
            e = k ? 128 + (SINE[(uint32_t)(ph * 16) & 1023u] >> 8) : 256 - (256 - ph) * (256 - ph) / 256 * r / 1000;
            ys[i] = (int16_t)back, y2[i] = (int16_t)e;
            cv_pset(x + i, y + h - 2 - e * (h - 4) / 256, NW);
        }
        vz_over(x, y, w, h, ys, y2, c);
        break;
    }
    case VZ_PITCH: {                                    /* a tone at the knob's pitch: more cycles as it goes up; the
                                                         * semitone lines behind */
        int32_t cyc = 1 + r * 7 / 1000, j;
        uint32_t ph0 = (uint32_t)(t / 4);
        for (j = 1; j < 6; j++)
            for (i = 0; i < w; i += 6)
                cv_pset(x + i, y + j * h / 6, NP);
        for (i = 0; i < w; i++)
            ys[i] = (int16_t)(mid - (SINE[(ph0 + (uint32_t)(i * cyc * 1024 / w)) & 1023u] * (h * 40 / 100) >> 15));
        vz_trace(x, y, w, h, ys, c);
        break;
    }
    case VZ_STEPS: {                                    /* 16 steps and the playhead; the swing pushes every second
                                                         * one; the knob's share lit */
        const track_t *tt = TSEL;
        int32_t cw = w / 16, sw = tt->p[P_SSWING] + song.g[G_SWING], len = clamp(tt->p[P_SLEN], 1, NSTEP);
        for (i = 0; i < 16; i++) {
            int32_t xx = i * cw + ((i & 1) ? cw * sw / 250 : 0), on = i < len && (int32_t)(tt->seq_idx % 16u) == i && song.playing;
            int32_t hh = trk_level(song.sel) * (h - 6) / 127 + 2, lit = i * 1000 / 16 < r;
            if (on)
                cv_rect(x + xx, y + h - 2 - hh, cw - 1, hh, NW);
            else if (lit)
                cv_rect(x + xx, y + h - 2 - hh / 2, cw - 1, hh / 2, c);
            else
                cv_rect(x + xx, y + h - 3, cw - 1, 1, c);
        }
        break;
    }
    case VZ_CRUSH: {                                    /* the wave in steps: fewer levels, held longer (k 1: aliasing,
                                                         * held longer still) */
        int32_t lv = 2 + (1000 - r) * 14 / 1000, hold = 1 + (k ? 6 : 2) * r / 300, held = 0;
        for (i = 0; i < w; i++) {
            if (i % hold == 0) {
                int32_t v = vz_y(vz_s((uint32_t)(w - 1 - i) * step), g, h) - mid;
                held = v / (h / lv + 1) * (h / lv + 1);
            }
            ys[i] = (int16_t)(mid + held);
        }
        vz_trace(x, y, w, h, ys, c);
        break;
    }
    case VZ_GATE: {                                     /* the sound over time cut into slices: on, off (k: slices) */
        int32_t sl = k ? k : 8, duty = 300 + r * 6 / 10, per = w / sl + 1;
        for (i = 0; i < w; i++) {
            int32_t pos = ((i + t / 15) % per) * 1000 / per;
            ys[i] = (int16_t)(pos < duty ? w - 1 - i : -1);
            y2[i] = 256;
        }
        vz_over(x, y, w, h, ys, y2, c);
        for (i = 0; i < w; i++)
            if (ys[i] < 0)
                cv_pset(x + i, y + mid, NW);
        break;
    }
    case VZ_LOOP: {                                     /* the last slice of sound, again and again (k times), the joins
                                                         * white */
        int32_t seg = w / (k > 0 ? k : 2);
        for (i = 0; i < w; i++)
            ys[i] = (int16_t)(seg - 1 - i % seg), y2[i] = 256;
        vz_over(x, y, w, h, ys, y2, c);
        for (i = seg; i < w; i += seg)
            cv_rect(x + i, y + 1, 1, h - 2, NW);
        break;
    }
    case VZ_REV: {                                      /* the sound running backwards (the newest on the left); k 1:
                                                         * winding back (a spiral) */
        if (k) {
            int32_t a;
            for (a = 0; a < 1024; a += 8) {
                int32_t rr = 2 + a * (h / 2 - 3) / 1024, aa = (a * 3 - t) & 1023;
                cv_rect(x + w / 2 + ((SINE[(uint32_t)(aa + 256) & 1023u] * rr) >> 15), y + mid + ((SINE[(uint32_t)aa] * rr) >> 15),
                        2, 2, a > 900 ? NW : c);
            }
            break;
        }
        for (i = 0; i < w; i++)
            ys[i] = (int16_t)i, y2[i] = 256;
        vz_over(x, y, w, h, ys, y2, c);
        for (i = 0; i < 4; i++) {                       /* arrows to the left */
            int32_t ax = w - 1 - (int32_t)((uint32_t)(t / 10 + i * w / 4) % (uint32_t)w);
            cv_line(x + ax, y + 3, x + ax + 4, y + 1, NW), cv_line(x + ax, y + 3, x + ax + 4, y + 5, NW);
        }
        break;
    }
    case VZ_STOP: {                                     /* the sound slowing to a halt: stretched and sinking to the right */
        int32_t pos = 0;
        for (i = 0; i < w; i++) {
            ys[i] = (int16_t)(w - 1 - pos / 256);
            y2[i] = (int16_t)(256 * (w - i) / w);
            pos += 256 * (w - i) / w;
        }
        vz_over(x, y, w, h, ys, y2, c);
        break;
    }
    case VZ_FREEZE: {                                   /* the wave as it was when pressed, still, with sparkles */
        for (i = 0; i < w; i++)
            ys[i] = vz_frz[i * 120 / w];
        vz_trace(x, y, w, h, ys, c);
        for (i = 0; i < 10; i++) {
            uint32_t hh = g_hash_ui((uint32_t)i * 31u + ui.frame / 3u);
            cv_pset(x + (int32_t)(hh % (uint32_t)w), y + (int32_t)((hh >> 10) % (uint32_t)h), NW);
        }
        break;
    }
    case VZ_KILL: {                                     /* low, middle, high as three blocks; the one killed crossed out */
        int32_t b, bw = w / 3;
        for (b = 0; b < 3; b++) {
            int32_t e = 0, n = b == 0 ? 24 : b == 1 ? 6 : 1, hh;
            for (i = 0; i < 96; i++) {
                int32_t j, acc = 0;
                for (j = 0; j < n; j++)
                    acc += vz_s((uint32_t)(i + j));
                acc /= n;
                if (b == 2)
                    acc = vz_s((uint32_t)i) - vz_s((uint32_t)i + 1u);
                e += acc < 0 ? -acc : acc;
            }
            hh = clamp((e / 96 * g >> 8) * h / 9000 + 3, 3, h - 6);
            if (b == k) {
                n_frame(x + b * bw + 3, y + h - 3 - hh, bw - 6, hh, 1, c);
                cv_line(x + b * bw + 3, y + h - 3 - hh, x + b * bw + bw - 4, y + h - 4, NW);
                cv_line(x + b * bw + bw - 4, y + h - 3 - hh, x + b * bw + 3, y + h - 4, NW);
            } else {
                cv_rect(x + b * bw + 3, y + h - 3 - hh, bw - 6, hh, c);
            }
        }
        break;
    }
    case VZ_FADE: {                                     /* the sound getting smaller to the right */
        for (i = 0; i < w; i++)
            ys[i] = (int16_t)(w - 1 - i), y2[i] = (int16_t)(256 * (w - i) / w);
        vz_over(x, y, w, h, ys, y2, c);
        break;
    }
    case VZ_BARS: {                                     /* the sound in 16 slices, as bars */
        int32_t n = 16, bw = w / n;
        for (i = 0; i < n; i++) {
            int32_t j, e = 0, hh;
            for (j = 0; j < 16; j++) {
                int32_t a = vz_s((uint32_t)(i * 16 + j));
                e += a < 0 ? -a : a;
            }
            hh = clamp((e / 16 * g >> 8) * h / 9000 + 1, 1, h - 2);
            cv_rect(x + i * bw, y + h - 1 - hh, bw - 1, hh, i * 1000 / n < r ? c : NP);
        }
        break;
    }
    default:                                            /* VZ_WAVE: the sound over time, as loud as the knob says */
        for (i = 0; i < w; i++)
            ys[i] = (int16_t)(w - 1 - i), y2[i] = (int16_t)(26 + r * 230 / 1000);
        vz_over(x, y, w, h, ys, y2, c);
        break;
    }
}

/* which visual a parameter gets: by what it is (glob: a G_ id, else a track's P_ id) */
static uint32_t vz_of(uint32_t id, int glob, const param_desc_t *d, int32_t *k)
{
    *k = 0;
    if (glob) {
        switch (id) {
        case G_BPM: case G_SWING: case G_CLOCK: case G_ROLL: return VZ_STEPS;
        case G_TUNE: case G_BEND: return VZ_PITCH;
        case G_DTIME: *k = 2; return VZ_ECHO;
        case G_DFDBK: *k = 1; return VZ_ECHO;
        case G_DMIX: case G_DRDLY: return VZ_ECHO;
        case G_DCOLOR: case G_TXTONE: return VZ_FILTER;
        case G_RSIZE: case G_RDAMP: case G_DRREV: return VZ_SPACE;
        case G_CRATE: case G_CDEPTH: return VZ_CHORUS;
        case G_DUST: case G_TXSRC: case G_TXLVL: case G_TXMOVE: case G_TXRATE: case G_TXWHEN: return VZ_DUST;
        case G_DUCK: case G_TXDUCK: return VZ_DUCK;
        case G_FILT: *k = 1; return VZ_FILTER;
        case G_DRLVL: return VZ_WAVE;
        default: return VZ_BARS;
        }
    }
    if (id >= P_ATK && id <= P_REL) {
        *k = (int32_t)(id - P_ATK);
        return VZ_ENV;
    }
    switch (id) {
    case P_ED_FLT: case P_ED_PIT: case P_ED_SHP: case P_ESYNC: *k = 1; return VZ_ENV;
    case P_LRATE: case P_LWAVE: case P_LPHASE: case P_LFADE: case P_LD_PIT: case P_LD_FLT: case P_LD_SHP: case P_LD_AMP:
    case P_LSYNC: case P_LTRIG: case P_LPOL: return VZ_LFO;
    case P_AMODE: case P_ARATE: case P_AOCT: case P_AGATE: case P_ASWING: case P_APROB: case P_AHOLD: case P_AORDER:
    case P_SLEN: case P_SDIV: case P_SSWING: case P_SGATE: return VZ_STEPS;
    case P_ROOT: case P_SCALE: case P_QUANT: case P_TRANS: case P_CHORD: case P_STRUM: case P_VLEAD: case P_DETUNE:
    case P_GLIDE: return VZ_PITCH;
    case P_DIST: return VZ_DRIVE;
    case P_CHOR: return VZ_CHORUS;
    case P_DLY: return VZ_ECHO;
    case P_REV: return VZ_SPACE;
    case P_PAN: return VZ_PAN;
    case P_SLCR: case P_SLPAT: case P_SLRATE: case P_SLDEPTH: *k = 8; return VZ_GATE;
    case P_TFLT: *k = 1; return VZ_FILTER;
    case P_FTYPE: return VZ_FILTER;
    case P_ITYPE: case P_IA: case P_IB: case P_IC: case P_IMIX: {   /* the INSERT: as its type sounds */
        int32_t it = TSEL->p[P_ITYPE];
        return it >= 1 && it <= 4 ? VZ_DRIVE : it == 5 ? VZ_CRUSH : it >= 6 ? VZ_CHORUS : VZ_WAVE;
    }
    case P_VOICE: case P_PRIO: case P_ALLOC: case P_GLMODE: case P_MUTE: return VZ_BARS;
    default: break;
    }
    if (id >= P_E0 && d && d->label) {                  /* an engine's own: by its name, then by its kind */
        const char *l = d->label;
        static const char *const FW[] = {"CUT", "RES", "FLT", "FILT", "TONE", "COL", "BRI", "DARK", 0};
        static const char *const DW[] = {"DRV", "DIST", "FOLD", "GAIN", "SAT", "DRIVE", "GRIT", 0};
        static const char *const PW[] = {"DET", "PIT", "TUNE", "FINE", "OCT", "SEMI", "RAT", "HARM", "FRQ", "FREQ", "MOD", 0};
        static const char *const EW[] = {"ATK", "DEC", "REL", "SUS", "ENV", "DCY", 0};
        static const char *const CW[] = {"BIT", "CRU", "LOFI", "SR", 0};
        static const char *const *const ALL[5] = {FW, DW, PW, EW, CW};
        static const uint8_t TO[5] = {VZ_FILTER, VZ_DRIVE, VZ_PITCH, VZ_ENV, VZ_CRUSH};
        uint32_t a, b;
        for (a = 0; a < 5u; a++)
            for (b = 0; ALL[a][b]; b++) {
                const char *p = l, *q = ALL[a][b];
                for (; *p; p++) {
                    uint32_t m = 0;
                    while (q[m] && p[m] == q[m])
                        m++;
                    if (!q[m])
                        return TO[a];
                }
            }
        return d->fmt == F_CUTOFF ? VZ_FILTER : d->fmt == F_TIME ? VZ_ENV : d->fmt == F_LFOHZ ? VZ_LFO :
               d->fmt == F_SEMI || d->fmt == F_NOTE ? VZ_PITCH : d->fmt == F_ENUM ? VZ_BARS : VZ_WAVE;
    }
    return VZ_WAVE;
}

/* the punch-in effects (punch.c PUNCH_NAME order): their visual, its variant; their names on the keys */
static const uint8_t PX_VZ[27][2] = {
    {VZ_LOOP, 2}, {VZ_LOOP, 3}, {VZ_LOOP, 4}, {VZ_LOOP, 6}, {VZ_GATE, 12}, {VZ_REV, 0}, {VZ_STOP, 0}, {VZ_LOOP, 1},
    {VZ_FILTER, 0}, {VZ_FILTER, 1}, {VZ_DRIVE, 0}, {VZ_CRUSH, 0}, {VZ_CRUSH, 1}, {VZ_GATE, 6}, {VZ_ECHO, 1}, {VZ_DUCK, 1},
    {VZ_KILL, 0}, {VZ_KILL, 1}, {VZ_KILL, 2}, {VZ_REV, 1}, {VZ_PITCH, 0}, {VZ_CHORUS, 0}, {VZ_PAN, 0}, {VZ_FREEZE, 0},
    {VZ_LOOP, 3}, {VZ_PAN, 0}, {VZ_FADE, 0}};
static const char *const PX_SHORT[27] = {"LOOP 4", "LOOP 8", "LOOP16", "LOOP32", "STUTT", "REV", "STOP", "HALF",
                                         "LOW", "HIGH", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOBBLE",
                                         "KL", "KM", "KH", "RW", "O+", "FL", "PN", "FZ", "8T", "MO", "FD"};
/* the r each effect is drawn at (its fixed amount) */
static int32_t px_r(uint32_t fx)
{
    switch (fx) {
    case 8: return 120;                                 /* LOW: closed far */
    case 9: return 900;                                 /* HIGH: a high-pass */
    case 10: return 900;                                /* PHONE: pushed hard */
    case 11: return 250;
    case 12: return 700;
    case 14: return 750;
    case 20: return 1000;                               /* OCT UP: twice */
    case 21: return 900;
    case 22: return 500 + (SINE[(fm1_ms / 2u) & 1023u] >> 6);   /* AUTO-PAN: swinging */
    case 25: return 500;                                /* MONO: the middle */
    default: return 600;
    }
}

/* ---------------------------------------------------------------- HOME --- */
static void n_home_head(uint32_t *cache)               /* the tempo, the transport, bar.beat, the beat */
{
    char b[12];
    uint32_t beat = clk_beat % 4u, k, pl = song.playing, rec = song.rec != 0;
    uint32_t sig = nh(nh(nh((uint32_t)song.g[G_BPM], pl * 3u + rec * 5u + mlu.on * 9u), pl ? beat + TSEL->seq_idx * 7u : 0u),
                      (uint32_t)(song.octave + 8));
    if (ui.msg_t)
        sig = nhs(sig ^ 0x9E3779B9u, ui.msg);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 56, NK);
    if (ui.msg_t) {
        n_msg(10, 34);
        cv_blit(0, 0);
        return;
    }
    fmt_int(b, song.g[G_BPM]);
    n_text(6, 4, b, 3, NW);
    if (song.octave) {                                  /* the octave, small, when not 0 */
        str_cpy(b, song.octave > 0 ? "+" : "", 4);
        fmt_int(b + str_len(b), song.octave);
        n_text(6 + n_w("000", 3) + 6, 4, b, 1, NW);
    }
    if (mlu.on)
        n_ring(130, 16, 6, 2, NW);
    if (rec)
        n_disc(222, 16, 9, NW);
    else if (pl) {
        int32_t i;
        for (i = 0; i < 18; i++)
            cv_rect(212 + i, 7 + i / 2, 1, 19 - i, NW);
    } else {
        n_frame(212, 7, 18, 18, 2, NW);
    }
    if (pl) {                                           /* bar.beat of the selected track's loop */
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP), s = TSEL->seq_idx % len;
        fmt_int(b, (int32_t)(s / 16u + 1u));
        k = str_len(b);
        b[k] = '.';
        b[k + 1] = (char)('1' + (s % 16u) / 4u);
        b[k + 2] = 0;
        n_text(204 - n_w(b, 1), 9, b, 1, NW);
    }
    for (k = 0; k < 4u; k++) {                          /* the four beats */
        int32_t x = 168 + (int32_t)k * 16;
        if (pl && beat == k)
            cv_rect(x, 38, 12, 12, NW);
        else
            n_frame(x, 38, 12, 12, 1, NW);
    }
    cv_blit(0, 0);
}

/* ---------------------------------------------------------------- PAGE --- */
/* the page's place among the pages of its family the selected track has: "2/3" ("" when it is alone) */
static void n_page_pos(char *b)
{
    uint32_t i, n = 0, cur = 0, fam = cur_page()->fam;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && page_shown(&PAGES[i])) {
            n++;
            if (i == ui.page)
                cur = n;
        }
    b[0] = 0;
    if (n < 2u)
        return;
    fmt_int(b, (int32_t)cur);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), (int32_t)n);
}

/* a parameter's short label, written out (DLY: DELAY); the others as they are */
static const char *rw_long(const char *l)
{
    static const char *const LN[][2] = {{"DST", "DRIVE"}, {"CHO", "CHORUS"}, {"DLY", "DELAY"}, {"REV", "REVERB"},
        {"ATK", "ATTACK"}, {"DEC", "DECAY"}, {"SUS", "SUSTAIN"}, {"REL", "RELEASE"}, {"FLT", "FILTER"}, {"FILT", "FILTER"},
        {"PIT", "PITCH"}, {"GLD", "GLIDE"}, {"LVL", "LEVEL"}, {"SWG", "SWING"}, {"SHP", "SHAPE"}, {"PHS", "PHASE"},
        {"FDBK", "FEEDBACK"}, {"DTUNE", "DETUNE"}, {"COLR", "COLOR"}, {"AMP", "AMOUNT"}, {"SECT", "SECTION"}};
    uint32_t i;
    for (i = 0; i < sizeof LN / sizeof LN[0]; i++)
        if (str_eq(l, LN[i][0]))
            return LN[i][1];
    return l;
}
/* one quarter (120 x 96) of a page: a value, its bar in the knob's colour; while its knob turns, the knob's texture
 * behind. kind BK_NONE + "" = empty; act: an action knob (LOAD, SAVE...), a square: of checks when it can act */
static void n_quad(uint32_t k, const char *v, int32_t r, uint32_t kind, int act, uint32_t vz, int32_t vk, int32_t al,
                   const char *lb, uint32_t *cache)
{
    int32_t hot = ui.hot_t && ui.hot_col == k, show = act < 0 && (v[0] || kind != BK_NONE) && (hot || al >= 0);
    uint32_t sig = nhs(nhs(nh(nh((uint32_t)r / 4u, kind * 3u + (uint32_t)hot * 5u), (uint32_t)(act + 2) * 7u), v), lb ? lb : ""), n;
    if (show)
        sig = nh(sig, ui.frame + (uint32_t)al * 7919u);  /* (its visual moves: every frame) */
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(120, 96, NK);
    if (lb && lb[0] && act < 0) {                       /* what the knob edits, under its bar: its full name, large,
                                                         * behind the visual (the number stays over it) */
        const char *ln = rw_long(lb);
        uint32_t z = n_fit(ln, 112, 2);
        n_texto(60 - n_w(ln, z) / 2, z == 2u ? 62 : 72, ln, z, NCOL[k & 3u] == NP ? NW : NCOL[k & 3u]);
    }
    if (show)                                           /* turned, a note played, the loop playing: its visual behind */
        n_vz(vz, 2, 2, 116, 92, r, NCOL[k & 3u], vk);
    else if (hot)
        n_tex(2, 2, 116, 92, KTEX[k & 3u], NCOL[k & 3u]);
    if (act >= 0) {                                     /* an action: a square, checks in it when it can act */
        if (act)
            n_tex(40, 22, 40, 40, PT_CHECK, NCOL[k & 3u]);
        n_frame(40, 22, 40, 40, 2, NW);
    } else if (v[0] || kind != BK_NONE) {
        static uint32_t turned[4];                      /* (ms) its knob last turned */
        int32_t a, f = 0;
        n = n_fit(v, 108, 4);
        if (hot)
            turned[k & 3u] = fm1_ms;
        if (fm1_ms - turned[k & 3u] < 1000u)            /* turning: the number half-way to solid; let go: back in 1 s */
            f = hot ? 256 : 256 - (int32_t)(fm1_ms - turned[k & 3u]) * 256 / 1000;
        a = al + (((16 + al) / 2 - al) * f >> 8);
        if (al >= 0)                                    /* (the visuals by themselves: the number see-through) */
            n_textd(60 - n_w(v, n) / 2, n == 4u ? 0 : n == 3u ? 8 : 18, v, n, NW, a);
        else
            n_texto(60 - n_w(v, n) / 2, n == 4u ? 0 : n == 3u ? 8 : 18, v, n, NW);
        n_knob(10, 52, 100, 16, r, kind, NW, NCOL[k & 3u]);
    }
    if (lb && lb[0] && act >= 0) {                      /* an action's name (LOAD, SAVE...) */
        const char *ln = rw_long(lb);
        uint32_t z = n_fit(ln, 112, 2);
        n_texto(60 - n_w(ln, z) / 2, z == 2u ? 62 : 72, ln, z, NW);
    }
    cv_blit((k & 1u) * 120u, 48u + (k >> 1) * 96u);
}

/* the pages' names on top, a carousel (SELECT): the page shown in the middle, large; the ones beside it small, to be
 * read before going there; the rest out of the screen. A name coming in eases in slowly, one going out leaves fast,
 * the ones moving inside the screen in between; its size follows how near the middle it is. Under them a thin
 * scroll bar in the track's colour. */
#define CAR_MAXP 48u
static int32_t car_x[CAR_MAXP];                         /* (Q8) the centre x of each page's name */
static uint8_t car_fam = 0xFF, car_on;                  /* the family shown, animating */
static int32_t car_bar;                                 /* (Q8) the scroll bar's place */
static int32_t car_target(uint32_t i, uint32_t cur, int32_t rel)
{
    int32_t wb = n_wq(PAGES[cur].title, 512), ws = n_wq(PAGES[i].title, 256);
    if (rel == 0)
        return 120;
    if (rel == 1 || rel == -1)
        return 120 + rel * (wb / 2 + 14 + ws / 2);
    return rel < 0 ? -140 - ws : 380 + ws;
}
static void n_carousel(uint32_t *cache)
{
    uint32_t i, n = 0, cur = 0, fam = cur_page()->fam, moving = 0, sig;
    uint8_t idx[16];
    for (i = 0; i < NPAGES && n < 16u; i++)
        if (PAGES[i].fam == fam && page_shown(&PAGES[i])) {
            if (i == ui.page)
                cur = n;
            idx[n++] = (uint8_t)i;
        }
    for (i = 0; i < n; i++) {                           /* move every name towards its place */
        int32_t t = car_target(idx[i], ui.page, (int32_t)i - (int32_t)cur) << 8, x = car_x[idx[i] % CAR_MAXP], d = t - x, k;
        int32_t on_now = x > -40 * 256 && x < 280 * 256, on_then = t > -40 * 256 && t < 280 * 256;
        if (car_fam != fam || (ui.force && !car_on)) {
            car_x[idx[i] % CAR_MAXP] = t;               /* (a family opened: in place at once) */
            continue;
        }
        k = !on_then ? 100 : !on_now ? 20 : 52;         /* (Q8 a frame: out fast, in slowly, inside between) */
        if (d > 256 || d < -256) {
            car_x[idx[i] % CAR_MAXP] = x + d * k / 256;
            moving = 1;
        } else {
            car_x[idx[i] % CAR_MAXP] = t;
        }
    }
    {
        int32_t t = n > 1u ? (int32_t)cur << 8 : 0, d;
        d = t - car_bar;
        if (car_fam != fam || d * d < 4 * 256)
            car_bar = t;
        else
            car_bar += d * 60 / 256, moving = 1;
    }
    car_fam = (uint8_t)fam;
    car_on = (uint8_t)moving;
    sig = nh(nh(ui.page, song.sel), (uint32_t)car_bar);
    for (i = 0; i < n; i++)
        sig = nh(sig, (uint32_t)car_x[idx[i] % CAR_MAXP]);
    if (ui.msg_t)
        sig = nhs(sig ^ 0x9E3779B9u, ui.msg);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 48, NK);
    if (ui.msg_t) {
        n_msg(4, 36);
        cv_blit(0, 0);
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = car_x[idx[i] % CAR_MAXP] >> 8, d = x > 120 ? x - 120 : 120 - x, q;
        const char *t = PAGES[idx[i]].title;
        q = 512 - (d > 80 ? 256 : d * 256 / 80);        /* the size: 2x in the middle, 1x from a neighbour's place out */
        if (x + n_wq(t, q) / 2 < 0 || x - n_wq(t, q) / 2 > 240)
            continue;
        n_textq(x - n_wq(t, q) / 2, 38 - ((16 * q) >> 8), t, q, NW);
    }
    if (n > 1u) {                                       /* the scroll bar: a thin line, the page's place in the colour */
        int32_t tw = 236 / (int32_t)n, tx = 2 + (car_bar * (236 - tw) / (int32_t)(n - 1u) >> 8);
        cv_rect(2, 44, 236, 1, NP);
        cv_rect(tx, 43, tw, 3, NCOL[song.sel & 3u]);
    }
    cv_blit(0, 0);
}

static void n_page(void)
{
    static uint32_t top, quad[4];
    const page_t *pg = cur_page();
    char v[4][8], pos[8];
    int32_t r[4] = {0, 0, 0, 0}, act[4] = {-1, -1, -1, -1};
    const char *lb[4] = {0, 0, 0, 0};                    /* (the knobs' names, under their bars) */
    uint32_t kind[4] = {BK_NONE, BK_NONE, BK_NONE, BK_NONE}, c, vz[4] = {VZ_BARS, VZ_BARS, VZ_BARS, VZ_BARS};
    int32_t vk[4] = {0, 0, 0, 0};
    n_screen(SCR_PAGE);
    for (c = 0; c < 4u; c++)
        v[c][0] = 0;
    n_page_pos(pos);
    if (is_drum(TSEL) && !page_for_drum(pg)) {
        /* (a page the drum track has not: empty) */
    } else if (pg->graph == GR_BROWSE) {                /* PRESETS: the sound's number of how many, its engine */
        uint32_t total, cur = preset_pos(&total);
        fmt_int(v[0], (int32_t)cur + 1);
        r[0] = total > 1u ? (int32_t)(cur * 1000u / (total - 1u)) : 0;
        kind[0] = BK_GROW;
        fmt_int(v[1], (int32_t)TSEL->eng_req % NENGINES + 1);
        r[1] = (int32_t)(TSEL->eng_req % NENGINES) * 1000 / (NENGINES - 1);
        kind[1] = BK_TURN;
    } else if (pg->graph == GR_DSOUND) {                /* GLO > DRUM SOUND: the sound, TUNE, DECAY, the level */
        int32_t tu = drum_tune[dsnd_lane % DRUM_LANES], de = drum_dec[dsnd_lane % DRUM_LANES];
        fmt_int(v[0], (int32_t)dsnd_lane + 1);
        r[0] = (int32_t)dsnd_lane * 1000 / (DRUM_LANES - 1);
        kind[0] = BK_GROW;
        if (tu > 0)
            v[1][0] = '+', fmt_int(v[1] + 1, tu);
        else
            fmt_int(v[1], tu);
        r[1] = (tu - DRUM_TUNE_MIN) * 1000 / (DRUM_TUNE_MAX - DRUM_TUNE_MIN);
        kind[1] = BK_MID;
        if (de > 0)
            v[2][0] = '+', fmt_int(v[2] + 1, de);
        else
            fmt_int(v[2], de);
        r[2] = (de - DRUM_DEC_MIN) * 1000 / (DRUM_DEC_MAX - DRUM_DEC_MIN);
        kind[2] = BK_MID;
        fmt_int(v[3], song.g[G_DRLVL] * 100 / 127);
        r[3] = song.g[G_DRLVL] * 1000 / 127;
        kind[3] = BK_GROW;
        vz[1] = VZ_PITCH, vz[2] = VZ_GATE;
        lb[0] = "SOUND", lb[1] = "TUNE", lb[2] = "DECAY", lb[3] = "LEVEL";
    } else if (pg->graph == GR_USER) {                  /* SAVE > USER: the slot, LOAD ERASE SAVE */
        int used = up_used(ui.uslot);
        fmt_int(v[0], (int32_t)ui.uslot + 1);
        r[0] = (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u);
        kind[0] = BK_GROW;
        act[1] = used, act[2] = used, act[3] = 1;
        lb[0] = "SLOT", lb[1] = "LOAD", lb[2] = "ERASE", lb[3] = "SAVE";
    } else if (pg->scope == SC_STEP) {                  /* STEP: the step, its note, its length, its flags */
        vz[0] = VZ_STEPS, vz[1] = VZ_PITCH, vz[2] = VZ_GATE, vz[3] = VZ_STEPS;
        const step_t *st = &TSEL->step[ui.cursor];
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP);
        fmt_int(v[0], (int32_t)ui.cursor + 1);
        r[0] = len > 1u ? (int32_t)(ui.cursor * 1000u / (len - 1u)) : 0;
        kind[0] = BK_GROW;
        if (st->n) {
            fmt_int(v[1], st->note[0]);
            r[1] = st->note[0] * 1000 / 127;
            kind[1] = BK_GROW;
        } else {
            str_cpy(v[1], "-", 8);
        }
        if (step_on(st)) {
            fmt_int(v[2], (int32_t)note_len(TSEL, ui.cursor));
            r[2] = (int32_t)note_len(TSEL, ui.cursor) * 1000 / 16;
            kind[2] = BK_GROW;
        } else {
            fmt_int(v[2], st->time % 3u + 1u);          /* NOTE TIE REST: 1 2 3 */
            r[2] = (int32_t)(st->time % 3u) * 500;
            kind[2] = BK_TURN;
        }
        c = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        fmt_int(v[3], (int32_t)c);
        r[3] = (int32_t)c * 333;
        kind[3] = BK_TURN;
        lb[0] = "STEP", lb[1] = "NOTE", lb[2] = "LEN", lb[3] = "FLAGS";
    } else {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = page_desc(pg, c, &vp);
            if (pg->graph == GR_SLOTS && c == 1u) {     /* PROJECT: ERASE, when the slot holds a project */
                act[1] = project_used((uint32_t)song.g[G_SLOT] - 1u), lb[1] = "ERASE";
                continue;
            }
            if (!d || !d->label || d->label[0] == '-')
                continue;
            lb[c] = d->label;
            if (d->fmt == F_ENUM && d->names == N_GO) { /* an action (LOAD, SAVE, CLEAR...) */
                act[c] = 1;
                continue;
            }
            if (pg->id[c] == G_INFO && pg->scope == SC_GLOBAL)
                fmt_int(v[c], (int32_t)(song.cpu_q8 * 100u / 256u));
            else
                n_val(d, *vp, v[c]);
            kind[c] = n_kind(d);
            vz[c] = vz_of(pg->id[c], pg->scope == SC_GLOBAL, d, &vk[c]);
            r[c] = RATIO(d, *vp);
            if (kind[c] == BK_TURN && d->max - d->min < 1)
                kind[c] = BK_NONE;
        }
    }
    (void)pos;
    n_carousel(&top);
    for (c = 0; c < 4u; c++)
        n_quad(c, v[c], r[c], kind[c], act[c], vz[c], vk[c], vz_alpha(), lb[c], &quad[c]);
}

/* --------------------------------------------------------------- LAYER --- */
/* 16 squares, the white keys, 4 x 4. T_OFF an outline, T_HAS a thick one (something there), T_ON filled
 * (on, held, heard); fill: how much of it (a drum hit's level); mark: ratchets (notches), tag: a nudge / a lock (a
 * dot), cut: a cross (muted), under: a bar under it (the playhead, the next section) */
enum { T_NONE, T_OFF, T_HAS, T_ON };
typedef struct {
    uint8_t st, fill, mark, tag, cut, under;
    const char *lab;                                    /* (FX: the effect's name) */
} nt_t;

static uint16_t n_tile_col;                             /* the colour of the layer held (n_layer) */
static uint8_t n_tile_tex;
static void n_tiles(const nt_t *tl, uint32_t *cache)
{
    uint32_t r, c, sig = nh(11u, n_tile_col + n_tile_tex * 65536u);
    for (r = 0; r < 16u; r++)
        sig = nh(sig, (uint32_t)(uintptr_t)tl[r].lab), sig = nh(sig, (uint32_t)tl[r].st | (uint32_t)tl[r].fill << 2 | (uint32_t)tl[r].mark << 8 | (uint32_t)tl[r].tag << 11 |
                          (uint32_t)tl[r].cut << 12 | (uint32_t)tl[r].under << 13);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    for (r = 0; r < 4u; r++) {
        cv_begin(240, 38, NK);
        for (c = 0; c < 4u; c++) {
            const nt_t *t = &tl[r * 4u + c];
            int32_t x = 4 + (int32_t)c * 60, m;
            uint16_t ink = NW;
            if (t->st == T_NONE)
                continue;
            if (t->st == T_ON) {                        /* on: the layer's texture in its colour, a white frame */
                int32_t h = t->fill && t->fill < 3u ? 30 * t->fill / 4 : 30;   /* (a hit: ghost a quarter, soft half) */
                n_tex(x, 32 - h, 52, h, n_tile_tex, n_tile_col);
                n_frame(x, 2, 52, 30, 2, NW);
                if (t->fill == 4u)                      /* hard: a frame round it */
                    n_frame(x - 3, 0, 58, 34, 1, NW);
            } else {
                n_frame(x, 2, 52, 30, t->st == T_HAS ? 4 : 1, NW);
            }
            for (m = 0; m < t->mark; m++)               /* ratchets */
                cv_rect(x + 18 + m * 6, 24, 4, 4, ink);
            if (t->tag)
                cv_rect(x + 44, 6, 4, 4, ink);
            if (t->cut) {                               /* muted: a cross */
                cv_line(x + 6, 6, x + 46, 28, NW);
                cv_line(x + 46, 6, x + 6, 28, NW);
            }
            if (t->under)
                cv_rect(x, 34, 52, 3, NW);
            if (t->lab)                                 /* the name, outlined (over a texture too) */
                n_texto(x + 26 - n_w(t->lab, 1) / 2, 9, t->lab, 1, NW);
        }
        cv_blit(0, 36u + r * 38u);
    }
}

/* FX: the top band holds the 11 black keys, a 2-letter name in each (the held one filled); locked: a bar under them.
 * An effect held: the band becomes its visual, its name over it */
static void n_blacks(uint32_t on, uint32_t locked, uint32_t *cache)
{
    uint32_t i, sig = nh(on, locked);
    int32_t fx = punch.req;
    if (fx >= 0)
        sig = nh(sig ^ 0x5bd1e995u, ui.frame + (uint32_t)fx * 977u);
    if (ui.msg_t || (!ui.force && sig == *cache)) {
        if (ui.msg_t)
            *cache = ~0u;
        return;
    }
    *cache = sig;
    cv_begin(240, 32, NK);
    if (fx >= 0 && fx < 27) {
        if (PX_VZ[fx][0] == VZ_FREEZE && vz_frz_on != (int8_t)fx) {   /* FREEZE: the wave of that moment */
            int32_t g = vz_gain(), k;
            for (k = 0; k < 120; k++)
                vz_frz[k] = (int16_t)vz_y(vz_s((uint32_t)(119 - k) * 2u), g, 32);
        }
        vz_frz_on = (int8_t)fx;
        n_vz(PX_VZ[fx][0], 0, 0, 240, 32, px_r((uint32_t)fx), NO, PX_VZ[fx][1]);
        n_texto(120 - n_w(PUNCH_NAME[fx], 1) / 2, 8, PUNCH_NAME[fx], 1, NW);
        cv_blit(0, 0);
        return;
    }
    vz_frz_on = -1;
    for (i = 0; i < 11u; i++) {
        int32_t x = 10 + (int32_t)i * 20;
        if ((on >> i) & 1u)
            n_tex(x, 4, 18, 20, PT_CHECK, NO), n_frame(x, 4, 18, 20, 2, NW);
        else
            n_frame(x, 4, 18, 20, 1, NW);
        n_texto(x + 1, 6, PX_SHORT[16u + i], 1, NW);
    }
    if (locked)
        n_tex(10, 27, 218, 4, PT_CHECK, NW);
    cv_blit(0, 0);
}

static uint8_t n_lvl_fill(uint32_t lv)                  /* a hit's level: ghost 1, soft 2, norm 3, hard 4 */
{
    return (uint8_t)(lvl_rank(lv) + 1u);
}

static void n_layer(void)
{
    static uint32_t top, tiles, foot, blk;
    static char v[4][8];
    nt_t tl[16];
    int32_t r[4] = {0, 0, 0, 0};
    uint8_t kind[4] = {BK_NONE, BK_NONE, BK_NONE, BK_NONE}, vz[4][2] = {{0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}};
    char mid[8];
    uint32_t i, layer = ui.layer;
    track_t *t = TSEL;
    n_screen(SCR_LAYER);
    mid[0] = 0;
    memset(tl, 0, sizeof tl);
    for (i = 0; i < 4u; i++)
        v[i][0] = 0;
    for (i = 0; i < 16u; i++)
        tl[i].st = T_OFF;
    {   /* each layer its colour and texture */
        static const uint8_t LTEX[LY_COUNT] = {PT_CHECK, PT_CHECK, PT_DIAG, PT_CHEV, PT_CHECK2, PT_GRID, PT_HLINE, PT_VLINE};
        n_tile_col = layer == LY_FX || layer == LY_ERASE ? NO : layer == LY_ROLL || layer == LY_SONG ? NP :
                     layer == LY_SCALE ? NG : layer == LY_MIX ? NB : NCOL[song.sel & 3u];
        n_tile_tex = LTEX[layer % LY_COUNT];
    }
    switch (layer) {
    case LY_FX: {                                       /* the punch-in effects: 16 white keys, 11 black */
        uint32_t blacks = 0;
        for (i = 0; i < 16u; i++) {
            tl[i].st = punch.req == (int8_t)i ? T_ON : T_OFF;
            tl[i].lab = PX_SHORT[i];
        }
        vz[0][0] = VZ_FILTER, vz[0][1] = 1, vz[1][0] = VZ_DUST, vz[2][0] = VZ_DUCK, vz[3][0] = VZ_FILTER, vz[3][1] = 1;
        if (punch.req >= 16)
            blacks = 1u << (punch.req - 16);
        n_blacks(blacks, ly_lock != LY_PLAY, &blk);
        n_val(&GP[G_FILT], song.g[G_FILT], v[0]);
        fmt_int(v[1], song.g[G_DUST] * 100 / 127);
        fmt_int(v[2], song.g[G_DUCK] * 100 / 127);
        n_val(&TP[P_TFLT], t->p[P_TFLT], v[3]);
        r[0] = (song.g[G_FILT] + 64) * 1000 / 127;
        r[1] = song.g[G_DUST] * 1000 / 127;
        r[2] = song.g[G_DUCK] * 1000 / 127;
        r[3] = (t->p[P_TFLT] + 64) * 1000 / 127;
        kind[0] = BK_MID, kind[1] = BK_GROW, kind[2] = BK_GROW, kind[3] = BK_MID;
        break;
    }
    case LY_ERASE:
    case LY_ROLL: {
        if (layer == LY_ERASE)
            vz[0][0] = VZ_STEPS, vz[1][0] = VZ_STEPS, vz[2][0] = VZ_PITCH;
        else
            vz[0][0] = VZ_GATE, vz[0][1] = 8;                                     /* the keys' sounds: there (thick), held (filled) */
        uint32_t held = fm1_in.notes;
        int mute = layer == LY_ERASE && is_drum(t) && er_mute;
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), down = (held >> k) & 1u, present = 0, j;
            if (is_drum(t)) {
                for (j = 0; j < trk_len(t); j++)
                    present |= dstep_has(&t->dstep[j], i);
            } else {
                uint32_t n = kb_map(t, k);
                if (n == KB_SILENT) {
                    tl[i].st = T_NONE;
                    continue;
                }
                for (j = 0; j < trk_len(t); j++)
                    if (t->step[j].time == ST_NOTE) {
                        uint32_t q;
                        for (q = 0; q < t->step[j].n; q++)
                            present |= t->step[j].note[q] == n;
                    }
            }
            tl[i].st = down ? T_ON : present ? T_HAS : T_OFF;
            if (mute)
                tl[i].cut = (uint8_t)((drum_mute >> i) & 1u);
        }
        if (layer == LY_ERASE) {                        /* SHIFT (turns), LENGTH, TRANSPOSE (turns) */
            fmt_int(v[1], t->p[P_SLEN]);
            r[1] = (t->p[P_SLEN] - 1) * 1000 / 63;
            kind[1] = BK_GROW;
            if (undo.valid)                             /* undo ready: 1 (OCT-), redo: 2 (OCT+) */
                fmt_int(mid, undo.undone ? 2 : 1);
        } else {
            fmt_int(v[0], clamp(song.g[G_ROLL], 0, 4) + 1);
            r[0] = song.g[G_ROLL] * 250;
            kind[0] = BK_TURN;
        }
        break;
    }
    case LY_STEP: {                                     /* the 16 steps of the page */
        vz[0][0] = VZ_PITCH, vz[1][0] = VZ_STEPS, vz[2][0] = VZ_STEPS, vz[3][0] = VZ_STEPS;
        uint32_t len = trk_len(t), page = ui.step_page;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = page * 16u + i, on, lv = LV_NORM, rt = 0;
            if (idx >= len) {
                tl[i].st = T_NONE;
                continue;
            }
            if (is_drum(t)) {
                const dstep_t *d = &t->dstep[idx];
                on = dstep_has(d, pen_lane);
                if (on) {
                    lv = dstep_lvl(d, pen_lane);
                    rt = dstep_rat(d, pen_lane);
                }
            } else {
                const step_t *st = &t->step[idx];
                on = step_on(st);
                if (on) {
                    lv = st->lvl & 3u;
                    rt = st->rat & 3u;
                } else if (st->time == ST_TIE) {
                    on = 0;
                    tl[i].st = T_HAS;
                }
            }
            if (on) {
                tl[i].st = T_ON;
                tl[i].fill = n_lvl_fill(lv);
                tl[i].mark = (uint8_t)rt;
            }
            tl[i].tag = (uint8_t)(step_locked(t, idx) || step_fill(t, idx) % 3u);   /* a nudge, a lock, a condition */
            tl[i].under = (uint8_t)((song.playing && idx == t->seq_idx) || ((ui.step_held >> i) & 1u));
        }
        if (len > 16u) {                                /* the page of steps: "1/4" */
            fmt_int(mid, (int32_t)page + 1);
            str_cpy(mid + str_len(mid), "/", 2);
            fmt_int(mid + str_len(mid), (int32_t)((len + 15u) / 16u));
        }
        if (is_drum(t)) {
            fmt_int(v[0], pen_lane + 1);
            r[0] = pen_lane * 1000 / 15;
        } else {
            fmt_int(v[0], pen_note[0]);
            r[0] = pen_note[0] * 1000 / 127;
        }
        kind[0] = BK_GROW;
        if (ui.step_held) {                             /* the first step held: its level / velocity, ratchet, nudge */
            vz[1][0] = VZ_WAVE, vz[2][0] = VZ_GATE, vz[2][1] = 4;
            uint32_t w, idx;
            int32_t m;
            for (w = 0; w < 16u && !((ui.step_held >> w) & 1u); w++)
                ;
            idx = (page * 16u + w) % NSTEP;
            if (is_drum(t)) {
                const dstep_t *d = &t->dstep[idx];
                if (dstep_has(d, pen_lane)) {
                    fmt_int(v[1], n_lvl_fill(dstep_lvl(d, pen_lane)));
                    r[1] = (n_lvl_fill(dstep_lvl(d, pen_lane)) - 1) * 333;
                    kind[1] = BK_TURN;
                    fmt_int(v[2], (int32_t)dstep_rat(d, pen_lane) + 1);
                    r[2] = (int32_t)dstep_rat(d, pen_lane) * 333;
                    kind[2] = BK_TURN;
                }
            } else {
                const step_t *st = &t->step[idx];
                if (step_on(st)) {
                    fmt_int(v[1], (int32_t)step_vel_shown(st));
                    r[1] = (int32_t)step_vel_shown(st) * 1000 / 127;
                    kind[1] = BK_GROW;
                    fmt_int(v[2], (int32_t)(st->rat & 3u) + 1);
                    r[2] = (int32_t)(st->rat & 3u) * 333;
                    kind[2] = BK_TURN;
                }
            }
            m = t->micro[idx];
            fmt_int(v[3], m);
            r[3] = (m - MICRO_MIN) * 1000 / (MICRO_MAX - MICRO_MIN);
            kind[3] = BK_MID;
            {   /* the lock (PRESETS / ALGORITHM): its parameter's number on top */
                uint32_t id = ui.lock_par % P_COUNT;
                fmt_int(mid, (int32_t)id);
            }
        } else {
            fmt_int(v[1], t->p[P_SDIV] % NDIV_STEP + 1);
            fmt_int(v[2], clamp(t->p[P_SSWING], 0, 100));
            fmt_int(v[3], t->p[P_SLEN]);
            r[1] = t->p[P_SDIV] * 1000 / (NDIV_STEP - 1);
            r[2] = t->p[P_SSWING] * 10;
            r[3] = (t->p[P_SLEN] - 1) * 1000 / 63;
            kind[1] = BK_TURN, kind[2] = BK_GROW, kind[3] = BK_GROW;
        }
        break;
    }
    case LY_SCALE: {                                    /* the white keys: the root filled, the scale thick */
        vz[0][0] = vz[1][0] = vz[2][0] = vz[3][0] = VZ_PITCH;
        uint32_t root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), pc = (53u + k) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            if (!is_drum(t) && t->p[P_CHORD]) {
                uint8_t c[4];
                uint32_t n = kb_map(t, k);
                if (n != KB_SILENT && chord_notes(t, n, c) != 0u) {
                    pc = c[0] % 12u;
                    in = 1;
                }
            }
            tl[i].st = pc == root ? T_ON : in ? T_HAS : T_OFF;
        }
        fmt_int(mid, (int32_t)root + 1);                /* the key: 1 = C .. 12 = B */
        if (!is_drum(t)) {
            fmt_int(v[0], clamp(t->p[P_CHORD], 0, 5) + 1);
            fmt_int(v[2], clamp(t->p[P_QUANT], 0, 2) + 1);
            fmt_int(v[3], t->p[P_TRANS]);
            r[0] = t->p[P_CHORD] * 200;
            r[2] = t->p[P_QUANT] * 500;
            r[3] = (t->p[P_TRANS] + 24) * 1000 / 48;
            kind[0] = BK_TURN, kind[2] = BK_TURN, kind[3] = BK_MID;
        }
        fmt_int(v[1], clamp(trk[0].p[P_SCALE], 0, NSCALES - 1) + 1);
        r[1] = trk[0].p[P_SCALE] * 1000 / (NSCALES - 1);
        kind[1] = BK_TURN;
        break;
    }
    case LY_MIX: {                                      /* 1-4 heard, 5-8 solo, 9 fill, 10 fill bar, 16 tap (the beat) */
        vz[0][0] = vz[1][0] = vz[2][0] = vz[3][0] = VZ_WAVE;
        for (i = 0; i < 4u; i++) {
            uint32_t lv = trk_level(i);
            tl[i].st = trk[i].p[P_MUTE] ? T_OFF : T_ON;
            tl[4 + i].st = (song.solo >> i) & 1u ? T_ON : T_OFF;
            fmt_int(v[i], (int32_t)lv * 100 / 127);
            r[i] = (int32_t)lv * 1000 / 127;
            kind[i] = BK_GROW;
        }
        tl[8].st = fill_now ? T_ON : T_OFF;
        tl[9].st = fill_bar_on ? T_ON : fill_arm ? T_HAS : T_OFF;
        for (i = 10; i < 15u; i++)
            tl[i].st = T_NONE;
        tl[15].st = song.playing && clk_pos < BEAT_U / 4u ? T_ON : T_OFF;
        fmt_int(mid, song.g[G_BPM]);
        break;
    }
    case LY_SONG: {                                     /* 1-4 the sections (playing filled, the next one underlined),
                                                         * 5-8 store, 13 loop / song, 14 song rec */
        uint32_t ready = arrangement_ready();
        for (i = 0; i < 4u; i++) {
            int used = (ready >> i) & 1u, playing = live_sec == (int8_t)i && !arrangement_clock.running;
            int next = live_req == (int8_t)i ||
                       (chain_n && live_req < 0 && (chain_sec[((chain_i + 1u) % chain_n) % CHAIN_MAX] & 3u) == i);
            tl[i].st = playing ? T_ON : used ? T_HAS : T_OFF;
            tl[i].under = (uint8_t)next;
            tl[4 + i].st = sec_armed == i + 1u ? T_ON : T_OFF;
        }
        for (i = 8; i < 16u; i++)
            tl[i].st = T_NONE;
        tl[12].st = arrangement_enabled ? T_ON : T_OFF;
        tl[13].st = srec == 2u ? T_ON : srec ? T_HAS : T_OFF;
        if (srec == 2u)                                 /* recording the song: its parts so far */
            fmt_int(mid, (int32_t)srec_n);
        else if (chain_taps >= 2u || chain_n)
            fmt_int(mid, (int32_t)(chain_n ? chain_n : chain_taps));
        break;
    }
    default:
        break;
    }
    if (layer != LY_FX || ui.msg_t) {                   /* (FX: the black keys there, unless a message) */
        if (layer == LY_FX)
            top = ~0u;
        n_top(mid, ly_lock != LY_PLAY, &top);
    } else {
        top = ~0u;
    }
    if (layer != LY_FX)
        blk = ~0u;
    n_tiles(tl, &tiles);
    {
        uint8_t m = ui.msg_t;                           /* (a message shows on top: the knobs stay) */
        ui.msg_t = 0;
        n_knobs(188, v, r, kind, (const uint8_t (*)[2])vz, &foot);
        ui.msg_t = m;
    }
}

/* ---------------------------------------------------------------- HOLD --- */
/* REC held (clear the track) or SAVE held (save the project): its number, a bar that fills */
#define HOLD_CLEAR_MS 1300u                             /* after the 0.7 s that make REC a hold */
#define HOLD_SAVE_MS 1000u
static void n_hold(void)
{
    static uint32_t cache;
    uint32_t span = ui.hold_kind == 1u ? HOLD_CLEAR_MS : HOLD_SAVE_MS, el = fm1_ms - ui.hold_t0;
    int32_t ratio = el >= span ? 1000 : (int32_t)(el * 1000u / span);
    uint32_t sig = nh(ui.hold_kind * 7u + ui.hold_trk * 131u, (uint32_t)ratio / 20u);
    char b[4];
    n_screen(SCR_HOLD);
    if (sig == cache && !ui.force)
        return;
    cache = sig;
    cv_begin(240, 120, NK);
    if (ui.hold_kind == 1u) {                           /* clear: a disc (REC) and the track */
        b[0] = (char)('1' + ui.hold_trk);
        n_disc(60, 60, 18, NW);
    } else {                                            /* save: a square (SAVE) and the slot */
        b[0] = (char)('0' + clamp(song.g[G_SLOT], 1, 4));
        cv_rect(42, 42, 36, 36, NW);
    }
    b[1] = 0;
    n_text(116, 12, b, 6, NW);
    cv_blit(0, 40);
    cv_begin(240, 40, NK);
    n_knob(20, 8, 200, 24, ratio, BK_GROW, NW, ui.hold_kind == 1u ? NO : NB);
    cv_blit(0, 170);
    if (cv_keyreq) {                                    /* (over the game: the rows it leaves show it too) */
        cv_begin(240, 40, NK), cv_blit(0, 0);
        cv_begin(240, 10, NK), cv_blit(0, 160);
        cv_begin(240, 30, NK), cv_blit(0, 210);
    }
}

#define RW_WH 53                                        /* (the REC windows: n_wavs below) */
static void rw_frame(int32_t h, const char *title, int on);
static const char *rw_name(uint32_t i, const char **role);
static void rw_foot(int32_t y, const char *s, int play);
static void rw_time(char *b, uint32_t ds);
static void n_wavs_at(int32_t bot, int32_t room, int armed);
/* ----------------------------------------------------------------- REC --- */
/* armed (a ring: the first note, or PLAY, starts it), the count-in (4 3 2 1), a free take (the seconds; the
 * bars and the tempo it makes). The knobs: MODE, LENGTH, START, QUANT (2.7: ON / OFF) as the SLOOP REC screen */
static void n_rec(void)
{
    static uint32_t head, body, foot;
    static char v[4][8];
    uint32_t take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0, count = ci_on, sig;
    uint32_t free = empty && !rec_tempo && !take;
    int32_t r[4] = {0, 0, 0, 0};
    uint8_t kind[4] = {BK_NONE, BK_NONE, BK_NONE, BK_NONE};
    char b[8];
    n_screen(SCR_REC);
    if (take)
        bars = ft_fit(ft_t, &bpm);
    n_home_head(&head);
    sig = nh(nh(nh(take * 3u + count * 5u + free * 7u + rec_count * 11u + blink * 13u, rt), secs * 131u + bars),
             bpm * 7u + ci_beat);
    if (!take && !count) {                              /* armed: the tracks' window (the one armed blinks), over the knobs */
        n_wavs_at(184, 148, 1);
    } else if (ui.force || sig != body) {
        const char *role, *name = rw_name(rt, &role);
        char s[40];
        int32_t j;
        cv_begin(240, RW_WH, NK);                       /* the track it records: its WAV window, empty yet */
        rw_frame(RW_WH, name, take || count || blink);
        for (j = 12; j < 228; j += 2)
            cv_pset(j, 27, NW);
        if (take) {                                     /* a free take: its time; the loop it makes, the tempo */
            rw_time(s, ft_t * CTL * 10u / FS);
            if (bars) {
                str_cpy(s + 7, " | ", 4), fmt_int(s + str_len(s), (int32_t)bars);
                str_cpy(s + str_len(s), bars == 1u ? " BAR " : " BARS ", 7), fmt_int(s + str_len(s), (int32_t)bpm);
                str_cpy(s + str_len(s), " BPM", 5);
            } else {
                str_cpy(s + 7, " | FREE | ", 11), str_cpy(s + str_len(s), role, 8);
            }
            rw_foot(39, s, 1);
        } else {
            if (count) {                                /* 4 3 2 1, in the middle of the wave */
                b[0] = (char)('0' + 4u - (ci_beat > 3u ? 3u : ci_beat)), b[1] = 0;
                cv_rect(112, 17, 16, 21, NK);
                n_text(116, 18, b, 1, NW);
            }
            str_cpy(s, count ? "COUNT IN | " : rec_count ? "ARMED: PLAY | " : "ARMED | ", 16);
            fmt_int(s + str_len(s), song.g[G_BPM]), str_cpy(s + str_len(s), " BPM | ", 8);
            str_cpy(s + str_len(s), role, 8);
            rw_foot(39, s, 0);
        }
        body = sig;
        cv_blit(0, 130);
    }
    if (!take && !count) {
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP);
        if (empty) {                                    /* MODE: free (1), tempo (2) */
            fmt_int(v[0], rec_tempo ? 2 : 1);
            r[0] = rec_tempo ? 1000 : 0;
            kind[0] = BK_TURN;
        }
        if (!free) {                                    /* LENGTH in bars (or steps), START: note (1), count (2) */
            fmt_int(v[1], (int32_t)(len % 16u ? len : len / 16u));
            r[1] = (int32_t)(len - 1u) * 1000 / 63;
            kind[1] = BK_GROW;
            fmt_int(v[2], rec_count ? 2 : 1);
            r[2] = rec_count ? 1000 : 0;
            kind[2] = BK_TURN;
        }
    }
    if (!count) {                                       /* (2.7) QUANT: ON, or OFF (the notes keep their timing) */
        str_cpy(v[3], rec_quant_off ? "OFF" : "ON", 8);
        r[3] = rec_quant_off ? 0 : 1000;
        kind[3] = BK_TURN;
    }
    {
        uint32_t k;
        for (k = 0; k < 4u; k++)
            if (kind[k] == BK_NONE)
                v[k][0] = 0;
    }
    {
        static const uint8_t RVZ[4][2] = {{0xFF, 0}, {VZ_STEPS, 0}, {0xFF, 0}, {0xFF, 0}};
        n_knobs(188, v, r, kind, RVZ, &foot);
    }
}

/* ---------------------------------------------------------- REC: WAVS --- */
/* Recording over a playing loop, and 6 s after: each track a window of a WAV on an old desktop (its sound's name.WAV),
 * the sound over its loop drawn as a waveform, the part played this pass solid, the rest (the pass before) a dither, the
 * head a line; the track recording has a red dot and its time, the others ACCEPTED. Three or more tracks done: one
 * FINAL_RESULT.WAV window, a lane each. The waveform is the track's real peak (fx.c / drums.c), a column per RW_W-th of
 * its loop, fed every frame by rw_tick (ui_draw), so a track recorded earlier is already drawn */
#define RW_W 216
static uint8_t rw_amp[NTRK][RW_W], rw_was;
static uint16_t rw_fr[NTRK], rw_mn[NTRK], rw_pl = 256; /* each lane's height (Q8: 0 hidden), as a minimised window
                                                         * (Q8), the loop stopped (256 playing, 205: 20 % lower); eased */
static uint16_t rw_idx[NTRK];
static int16_t rw_last[NTRK], rw_rs[NTRK];             /* rw_rs: the column a take began at */
static uint32_t rw_t0[NTRK], rw_hold, rw_done[NTRK];   /* rw_done: the columns recorded since (past RW_W: over it again) */
static uint32_t rw_step_ms(void) { return 15000u / (uint32_t)clamp(song.g[G_BPM], 20, 300); }
static uint32_t rw_loop_ds(uint32_t i) { return trk_len(&trk[i]) * rw_step_ms() / 100u; }   /* the loop, 1/10 s */
static int32_t rw_col(uint32_t i)                       /* the head's column in track i's loop */
{
    const track_t *t = &trk[i];
    uint32_t len = trk_len(t), sm = rw_step_ms(), f = fm1_ms - rw_t0[i];
    if (f > sm)
        f = sm;
    return (int32_t)((((uint32_t)t->seq_idx % len) * sm + f) * RW_W / (len * sm)) % RW_W;
}
static uint16_t rw_ease(uint16_t v, int32_t tg)
{
    int32_t df = tg - (int32_t)v;
    return (uint16_t)((int32_t)v + df / 5 + (df > 0) - (df < 0));
}
static void rw_tick(void)
{
    uint32_t i, rec = song.playing ? song.rec : 0u;
    rw_pl = rw_ease(rw_pl, song.playing ? 256 : 205);
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        int32_t pk, lg = 0, a = 0, c, p, on = (int32_t)((rec >> i) & 1u), loop = !track_empty(t);
        if (t->seq_idx != rw_idx[i])
            rw_idx[i] = t->seq_idx, rw_t0[i] = fm1_ms;
        if (i == TRK_DRUM)
            pk = drums.peak, drums.peak = 0;
        else
            pk = t->peak, t->peak = 0;
        rw_fr[i] = rw_ease(rw_fr[i], loop || on ? 256 : 0);   /* its lane: a loop or recording; the selected one, empty: */
        rw_mn[i] = rw_ease(rw_mn[i], !loop && !on && i == song.sel ? 256 : 0);   /* a minimised window */
        if (!song.playing)
            continue;
        if (pk >= 64) {                                 /* log, as the TRACKS meters: 48 (-54 dB) .. 128 */
            while ((pk >> lg) > 1)
                lg++;
            a = clamp((lg * 8 + (((pk << 3) >> lg) & 7) - 62) * 11 / 56, 0, 11);
        }
        c = rw_col(i), p = rw_last[i];
        if (on && !((rw_was >> i) & 1u))                /* a take begins here */
            rw_rs[i] = (int16_t)c, rw_done[i] = 0, p = c;
        if (c == p) {
            if (a > rw_amp[i][c])
                rw_amp[i][c] = (uint8_t)a;
        } else {
            while (p != c) {                            /* (the columns a frame jumped over: the same peak) */
                p = (p + 1) % RW_W;
                rw_amp[i][p] = (uint8_t)a;
                rw_done[i] += (uint32_t)on;
            }
        }
        rw_last[i] = (int16_t)c;
    }
    if (rw_was && !rec)
        rw_hold = fm1_ms + 6000u;
    rw_was = (uint8_t)rec;
}
static int rw_show(void) { return (song.playing && song.rec) || (rw_hold && (int32_t)(rw_hold - fm1_ms) > 0); }

static const char *const RW_ROLE[8] = {"BASS", "KEYS", "ORGAN", "PAD", "LEAD", "PLUCK", "STAB", "FX"};
static const char *rw_name(uint32_t i, const char **role)
{
    uint32_t tot, pos;
    if (i == TRK_DRUM) {
        *role = "DRUMS";
        return DRUM_KIT_NAMES[drum_kit()];
    }
    pos = preset_pos_of(&trk[i], &tot);
    *role = pos < NBANK ? RW_ROLE[BANK[pos].kind & 7u] : "USER";
    return pos < NBANK ? BANK[pos].name : "USER";
}
static void rw_time(char *b, uint32_t ds)               /* 00:05.6 */
{
    uint32_t s = ds / 10u;
    b[0] = (char)('0' + s / 600u % 10u), b[1] = (char)('0' + s / 60u % 10u), b[2] = ':';
    b[3] = (char)('0' + s % 60u / 10u), b[4] = (char)('0' + s % 10u), b[5] = '.', b[6] = (char)('0' + ds % 10u), b[7] = 0;
}
static void rw_dith(int32_t x, int32_t y, int32_t w, int32_t h)   /* a 50 % checker */
{
    int32_t i, j;
    for (j = 0; j < h; j++)
        for (i = (x + y + j) & 1; i < w; i += 2)
            cv_pset(x + i, y + j, NW);
}
/* the frame of a window h tall on the band: border, title bar (solid when it records, a dither when done), its name,
 * the three little boxes, a dot (red: recording) */
static void rw_frame(int32_t h, const char *title, int on)
{
    char b[24];
    int32_t k;
    cv_rect(4, 0, 232, h, NW);
    cv_rect(5, 1, 230, h - 2, NK);
    if (!on)
        rw_dith(5, 1, 230, 13);
    cv_rect(5, 14, 230, 1, NW);
    str_cpy(b, title, 18);
    str_cpy(b + str_len(b), ".WAV", 6);
    for (k = 0; b[k]; k++)
        if (b[k] == ' ')
            b[k] = '_';
    cv_rect(9, 5, 4, 4, on ? ER : NW);
    n_texto(17, -1, b, 1, NW);
    for (k = 0; k < 3; k++) {                           /* _ [] x */
        int32_t x = 203 + k * 10;
        cv_rect(x, 3, 9, 9, NW), cv_rect(x + 1, 4, 7, 7, NK);
        if (k == 0)
            cv_rect(x + 2, 8, 5, 2, NW);
        else if (k == 1)
            cv_rect(x + 2, 5, 5, 5, NW), cv_rect(x + 3, 6, 3, 3, NK);
        else {
            int32_t d;
            for (d = 0; d < 5; d++)
                cv_pset(x + 2 + d, 5 + d, NW), cv_pset(x + 6 - d, 5 + d, NW);
        }
    }
}
/* track i's waveform in x0.. (w columns), mid row y, half height hh, in colour c: the part played this pass a pixel
 * gradient (dense in the middle, thinning out), the rest (the pass before) sparse, the middle a line, the head white.
 * Recording (rec): ahead of the head a fading veil covers what is there; above and below, a contour: what this take has
 * recorded (white), what is left of the loop before it comes round (dotted), and once round, what it records over
 * again (yellow) */
static void rw_wave(uint32_t i, int32_t x0, int32_t w, int32_t y, int32_t hh, int32_t head, uint16_t c, int rec)
{
    static const uint8_t BAY[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};   /* (4 x 4 ordered dither) */
    int32_t j, done = (int32_t)rw_done[i], pass = done / RW_W, part = done % RW_W;
    for (j = 0; j < w; j++) {
        int32_t col = j * RW_W / w, a = rw_amp[i][col] * hh / 11, x = x0 + j, k, ahead = (col - head + RW_W) % RW_W;
        int32_t veil = rec && ahead > 0 && ahead < 40 ? (40 - ahead) * 16 / 40 : 0;   /* 16 at the head .. 0 */
        for (k = -a; k <= a; k++) {
            int32_t ak = k < 0 ? -k : k, lv = col <= head ? 16 - ak * 12 / (a + 1) : 4;
            uint32_t b = BAY[((y + k) & 3) * 4 + (x & 3)];
            if (b < (uint32_t)lv && b >= (uint32_t)veil)
                cv_pset(x, y + k, c);
        }
        if (veil)                                       /* the veil itself: a haze of the colour, fading */
            for (k = -hh; k <= hh; k += 2)
                if (BAY[((y + k) & 3) * 4 + (x & 3)] < (uint32_t)(veil / 3))
                    cv_pset(x, y + k, c);
        if ((col > head || !a) && !veil)
            cv_pset(x, y, c);
        if (rec) {                                      /* the take's contour */
            int32_t ds = (col - rw_rs[i] + RW_W) % RW_W;
            uint16_t lc = pass ? (ds <= part ? EY : NW) : (ds <= part ? NW : 0);
            if (lc)
                cv_pset(x, y - hh - 2, lc), cv_pset(x, y + hh + 2, lc);
            else if ((x & 3) == 0)
                cv_pset(x, y - hh - 2, NW), cv_pset(x, y + hh + 2, NW);
        }
    }
    cv_rect(x0 + head * w / RW_W, y - hh - 2, 1, 2 * hh + 5, NW);
}
static void rw_foot(int32_t y, const char *s, int play)  /* the status line: > 00:01.8 | 170 BPM | DRUMS */
{
    int32_t k;
    cv_rect(5, y, 230, 1, NW);
    if (play)
        for (k = 0; k < 4; k++)
            cv_rect(9 + k, y + 4 + k, 1, 7 - 2 * k, NW);
    n_text(play ? 17 : 9, y - 1, s, 1, NW);
}
static uint8_t rw_view;                                 /* HOME tapped on the game: the tracks' WAV, open */
static void rw_toggle(void) { rw_view = (uint8_t)((rw_view + 1u) % 3u); }   /* game, the tracks' WAVs, the FX bay */
static int rw_view_on(void) { return rw_view; }
/* one window, its footer at bot: a lane a track with a loop, by number (the empty ones hidden; the one selected, empty:
 * minimised, its name on a bar); a track starting to record grows its lane in among the others (rw_fr, rw_tick); the loop
 * stopped: the lanes 20 % lower, their waves 20 % more. A lane: the number in the track's colour (the one selected or
 * recording framed; past one pass of a take: x2, x3...), the level, the waveform in that colour. The title: the sound
 * recording (.WAV) or FINAL_RESULT.WAV, the active track's number by the little boxes; the footer: the time (recording:
 * of the loop's length), the tempo, the part recording or how many tracks. armed: REC waits (n_rec), its dot blinks */
static void n_wavs_at(int32_t bot, int32_t room, int armed)
{
    uint32_t i, rec = song.playing ? song.rec : 0u, cur = song.sel, bpm = (uint32_t)song.g[G_BPM], m = 0, full = 0, lng = cur;
    int32_t lh, hh[NTRK], mh[NTRK], sum = 0, mins = 0, top, y, blink = (int32_t)((fm1_ms / 300u) & 1u);
    const char *role, *name;
    char s[40], t[8], d[2];
    for (i = 0; i < NTRK; i++) {
        full += !track_empty(&trk[i]);
        m += rw_fr[i] > 128u;
        mins += rw_mn[i] > 128u;
        if ((rec >> i) & 1u)
            cur = i;
        if (rw_fr[i] && trk_len(&trk[i]) > trk_len(&trk[lng]))
            lng = i;
    }
    lh = m ? (room - 30 - 15 * mins) / (int32_t)m : 46;
    lh = clamp(lh, 20, m > 3u ? 38 : 46);
    for (i = 0; i < NTRK; i++) {
        hh[i] = (lh * (int32_t)rw_fr[i] >> 8) * rw_pl >> 8;
        mh[i] = 15 * (int32_t)rw_mn[i] >> 8;
        sum += hh[i] + mh[i];
    }
    top = bot - (15 + sum + 15);
    name = rw_name(cur, &role);
    cv_begin(240, 15, NK);                              /* the title */
    rw_frame(16, rec ? name : "FINAL RESULT", 1);
    if (!rec)
        cv_rect(9, 5, 4, 4, NW);
    d[0] = (char)('1' + cur), d[1] = 0;                 /* the active track's number, by the boxes */
    cv_rect(187, 2, 12, 11, NCOL[cur & 3u]);
    n_text(189, 0, d, 1, NW);
    cv_blit(0, (uint32_t)top);
    for (i = 0, y = top + 15; i < NTRK; y += hh[i] + mh[i], i++) {
        int32_t h = hh[i], pk = rw_amp[i][rw_col(i)], on = (int32_t)((rec >> i) & 1u);
        uint16_t c = NCOL[i & 3u];
        d[0] = (char)('1' + i);
        if (mh[i] >= 2) {                               /* minimised: a title bar, its name.WAV */
            const char *r2, *nm = rw_name(i, &r2);
            char b[24];
            int32_t k, h2 = mh[i];
            cv_begin(240, (uint32_t)h2, NK);
            cv_rect(4, 0, 1, h2, NW), cv_rect(235, 0, 1, h2, NW), cv_rect(5, 0, 230, h2, NK);
            rw_dith(5, 1, 230, h2 - 2);
            cv_rect(5, h2 - 1, 230, 1, NW);
            cv_rect(9, 2, 12, 11, c);
            n_text(11, 0, d, 1, NW);
            str_cpy(b, nm, 18), str_cpy(b + str_len(b), ".WAV", 6);
            for (k = 0; b[k]; k++)
                if (b[k] == ' ')
                    b[k] = '_';
            n_texto(26, -1, b, 1, NW);
            if (armed && blink)
                cv_rect(193, 5, 4, 4, ER);
            for (k = 0; k < 3; k++)
                cv_rect(203 + k * 10, 3, 9, 9, NW), cv_rect(204 + k * 10, 4, 7, 7, NK);
            cv_rect(205, 8, 5, 2, NW);
            cv_blit(0, (uint32_t)y);
        }
        if (h < 2)
            continue;
        cv_begin(240, (uint32_t)h, NK);
        cv_rect(4, 0, 1, h, NW), cv_rect(235, 0, 1, h, NW), cv_rect(5, 0, 230, h, NK);
        {
            uint16_t fc = i == cur ? (on || (armed && blink) ? ER : EY) : NW;
            int32_t in = i == cur ? 2 : 1;
            cv_rect(8, 2, 52, h - 4, fc), cv_rect(8 + in, 2 + in, 52 - 2 * in, h - 4 - 2 * in, NK);
        }
        cv_rect(13, 6, 18, 18, c);
        n_text(18, 8, d, 1, NW);
        if (on && rw_done[i] >= RW_W) {                 /* over the take again: x2, x3... */
            char px[3] = {'x', (char)('1' + clamp((int32_t)(rw_done[i] / RW_W), 1, 8)), 0};
            n_texto(35, 7, px, 1, EY);
        }
        {                                               /* the level meter: the peak of the last few columns, full at 7 of 11 */
            int32_t q, hd = rw_col(i);
            for (q = 1; q < 6; q++)
                if (rw_amp[i][(hd - q + RW_W) % RW_W] > pk)
                    pk = rw_amp[i][(hd - q + RW_W) % RW_W];
            n_knob(12, h - 13, 44, 9, song.playing ? clamp(pk * 1000 / 7, 0, 1000) : 0, BK_GROW, NW, c);
        }
        rw_wave(i, 64, 168, h / 2, ((h / 2 - 4 > 1 ? h / 2 - 4 : 1) * rw_pl) >> 8, rw_col(i), c, on);
        cv_rect(5, h - 1, 230, 1, NW);
        cv_blit(0, (uint32_t)y);
    }
    cv_begin(240, 15, NK);                              /* the footer */
    cv_rect(4, 0, 232, 15, NW), cv_rect(5, 0, 230, 14, NK);
    if (rec) {                                          /* > 00:01.8 / 00:05.6 | KEYS (over it again: | OVER x2) */
        rw_time(t, (uint32_t)rw_col(cur) * rw_loop_ds(cur) / RW_W);
        str_cpy(s, t, 8), str_cpy(s + 7, " / ", 4);
        rw_time(t, rw_loop_ds(cur));
        str_cpy(s + str_len(s), t, 8), str_cpy(s + str_len(s), " | ", 4);
        if (rw_done[cur] >= RW_W) {
            char px[3] = {'x', (char)('1' + clamp((int32_t)(rw_done[cur] / RW_W), 1, 8)), 0};
            str_cpy(s + str_len(s), "OVER ", 6), str_cpy(s + str_len(s), px, 3);
        } else {
            str_cpy(s + str_len(s), role, 8);
        }
    } else {                                            /* > 00:04 | 170 BPM | 3 TRACKS (whole seconds: it fits) */
        rw_time(t, (uint32_t)rw_col(lng) * rw_loop_ds(lng) / RW_W);
        str_cpy(s, t, 6), str_cpy(s + 5, " | ", 4);
        fmt_int(s + str_len(s), (int32_t)bpm), str_cpy(s + str_len(s), " BPM | ", 8);
        fmt_int(s + str_len(s), (int32_t)full), str_cpy(s + str_len(s), full == 1u ? " TRACK" : " TRACKS", 8);
    }
    rw_foot(0, s, song.playing);
    cv_blit(0, (uint32_t)(top + 15 + sum));
}
/* ------------------------------------------------------------- FX BAY --- */
/* HOME twice on the game (after the tracks' WAVs): the ship's bay, after a mod shop's blueprint. A row a track: its
 * number on a yellow diamond, its sound, a bar of slanted cells that grows with the effects it uses (two cells each,
 * pink to orange), how many in a small diamond; lines run from it to the effects' boxes on the right (each box lit
 * when a track uses it), in the track's colour. On top how many tracks hold a loop and how many effects are on;
 * under it the shortcuts: REC + FX clears a track's effects, EDIT + OCT- undoes */
#define BY_BG RGB(0x0A, 0x0A, 0x2A)                     /* (the user: darker, the text reads over the game) */
#define BY_GRID RGB(0x24, 0x2C, 0x78)
#define BY_TEAL RGB(0x5E, 0xD8, 0xC8)
#define BY_DIM RGB(0x2E, 0x6E, 0x86)
#define BY_GOLD RGB(0xE8, 0xC0, 0x40)
static const char *const BY_FX[7] = {"DRV", "CHO", "DLY", "REV", "FLT", "SLC", "INS"};
#include "nms_nave_cima.h"
/* the ship's four parts (its picture points up): 0 the engines (the back), 1 the wings (the sides), 2 the cabin (the
 * nose), 3 the cargo (the middle); the anchors the selected track's line ends at */
static uint32_t by_part(int32_t x, int32_t y)
{
    return y >= 26 ? 0u : (x < 15 || x > 32) ? 1u : y < 13 ? 2u : 3u;
}
static const int16_t BY_ANCH[4][2] = {{192, 226}, {156, 196}, {192, 174}, {192, 200}};
static uint32_t by_nc(int32_t x, int32_t y)            /* the ship's picture from above: a pixel (>= 10: none) */
{
    uint32_t k;
    if ((uint32_t)x >= (uint32_t)NMS_NC_W || (uint32_t)y >= (uint32_t)NMS_NC_H)
        return 15u;
    k = (uint32_t)(y * NMS_NC_W + x);
    return (NMS_NC[k >> 1] >> ((k & 1u) ? 0u : 4u)) & 15u;
}
static uint32_t by_fx(uint32_t i)                       /* the effects track i uses: a bit each (BY_FX) */
{
    const track_t *t = &trk[i];
    uint32_t m = 0;
    m |= (t->p[P_DIST] > 0) << 0 | (t->p[P_CHOR] > 0) << 1;
    m |= (i == TRK_DRUM ? song.g[G_DRDLY] > 0 : t->p[P_DLY] > 0) << 2;
    m |= (i == TRK_DRUM ? song.g[G_DRREV] > 0 : t->p[P_REV] > 0) << 3;
    m |= (t->p[P_TFLT] != 0) << 4 | (t->p[P_SLCR] != 0) << 5 | (t->p[P_ITYPE] != 0) << 6;
    return m;
}
static void by_diam(int32_t cx, int32_t cy, int32_t r, uint16_t c, int fill)   /* a diamond */
{
    int32_t i, j;
    for (j = -r; j <= r; j++)
        for (i = -r; i <= r; i++) {
            int32_t m = (i < 0 ? -i : i) + (j < 0 ? -j : j);
            if (m == r || (fill && m < r))
                cv_pset(cx + i, cy + j, m == r ? c : BY_BG);
        }
}
static void n_bay(void)
{
    static int32_t shown[NTRK];                         /* (the bars grow: Q8 cells) */
    uint32_t i, k, used = 0, loops = 0, fxn = 0, m[NTRK];
    int32_t band;
    char b[24];
    for (i = 0; i < NTRK; i++) {
        uint32_t q = m[i] = by_fx(i), c = 0;
        used |= q, loops += !track_empty(&trk[i]);
        while (q)
            c += q & 1u, q >>= 1;
        fxn += c;
        shown[i] += ((int32_t)c * 2 * 256 - shown[i]) / 5 + ((int32_t)c * 512 > shown[i]) - ((int32_t)c * 512 < shown[i]);
    }
    for (band = 0; band < 2; band++) {
        int32_t y0 = band * 120, x, y;
        cv_begin(240, 120, NK);
        cv_oy = -y0;
        for (y = y0; y < y0 + 120; y++) {               /* (the user) see-through at the top, solid at the bottom: a
                                                         * gradient in pixels (the game shows through the gaps) */
            int32_t lv = 12 + y * 5 / 240;              /* (the user: denser, darker: 12/16 at the top) */
            for (x = 0; x < 240; x++)
                if ((int32_t)BAYER_N[y & 3][x & 3] < lv)
                    cv_pset(x, y, BY_BG);
        }
        for (y = 18; y < 240; y += 12)                  /* the blueprint's grid */
            for (x = 0; x < 240; x++)
                if (x >= 150 || (x & 1))
                    cv_pset(x, y, BY_GRID);
        for (x = 150; x < 240; x += 12)
            for (y = 18; y < 206; y++)
                cv_pset(x, y, BY_GRID);
        n_texto(8, 0, "FX BAY", 1, BY_TEAL);            /* the title, the counts */
        fmt_int(b, (int32_t)loops), str_cpy(b + str_len(b), " TRK  ", 7);
        fmt_int(b + str_len(b), (int32_t)fxn), str_cpy(b + str_len(b), " FX", 4);
        n_texto(232 - n_w(b, 1), 0, b, 1, BY_GOLD);
        for (k = 0; k < 7u; k++) {                      /* the effects' boxes, a bus down to the ship */
            int32_t bx = 196, byy = 22 + (int32_t)k * 17;
            uint16_t c = (used >> k) & 1u ? BY_TEAL : BY_DIM;
            cv_rect(bx, byy, 38, 15, c), cv_rect(bx + 1, byy + 1, 36, 13, BY_BG);
            n_text(bx + 7, byy - 2, BY_FX[k], 1, c);
        }
        for (y = 141; y < 168; y++)
            cv_pset(215, y, used ? BY_TEAL : BY_DIM);
        {   /* the ship, as a blueprint: its picture from above at 2 x, in four parts, a track each: 1 its engines, 2 its
             * wings, 3 its cabin, 4 its cargo. The outline teal, the lines inside dimmer; the selected track's part lit in
             * its colour (white outline); with the music playing each part pulses with its own track's sound */
            int32_t sx, sy;
            for (sy = 0; sy < NMS_NC_H; sy++)
                for (sx = 0; sx < NMS_NC_W; sx++) {
                    uint32_t v = by_nc(sx, sy), e, pt = by_part(sx, sy), lv;
                    int32_t X = 144 + sx * 2, Y = 168 + sy * 2;
                    if (v >= 10u)
                        continue;
                    lv = song.playing && !track_empty(&trk[pt]) ? rw_amp[pt][rw_col(pt)] : 0u;
                    e = by_nc(sx - 1, sy) >= 10u || by_nc(sx + 1, sy) >= 10u || by_nc(sx, sy - 1) >= 10u || by_nc(sx, sy + 1) >= 10u;
                    if ((pt == song.sel ? 7u : 0u) + lv * 10u / 11u > 0u) {   /* lit: the part's track colour in a mesh */
                        uint32_t a = (pt == song.sel ? 7u : 0u) + lv * 9u / 11u;
                        int32_t q;
                        for (q = 0; q < 4; q++)
                            if (BAYER_N[(Y + q / 2) & 3][(X + q % 2) & 3] < (a > 16u ? 16u : a))
                                cv_pset(X + q % 2, Y + q / 2, NCOL[pt & 3u]);
                    }
                    if (e)
                        cv_rect(X, Y, 2, 2, pt == song.sel ? NW : BY_TEAL);
                    else if (by_nc(sx + 1, sy) != v || by_nc(sx, sy + 1) != v)
                        cv_rect(X, Y, 2, 2, BY_DIM);
                }
        }
        for (i = 0; i < NTRK; i++) {                    /* the tracks */
            int32_t ry = 26 + (int32_t)i * 44, cells = shown[i] >> 8, j;
            const char *role, *nm = rw_name(i, &role);
            uint16_t tc = NCOL[i & 3u];
            by_diam(14, ry + 10, 10, BY_GOLD, 1), by_diam(14, ry + 10, 7, BY_GOLD, 0);
            b[0] = (char)('1' + i), b[1] = 0;
            n_text(11, ry + 1, b, 1, track_empty(&trk[i]) ? BY_DIM : NW);
            n_texto(30, ry - 4, nm, 1, track_empty(&trk[i]) ? BY_DIM : BY_TEAL);
            for (j = 0; j < 14; j++) {                  /* the bar: slanted cells, pink to orange */
                int32_t cx0 = 30 + j * 8, q;
                uint16_t c = j < cells ? (j < 5 ? RGB(0xC8, 0x50, 0x8C) : j < 10 ? RGB(0xE6, 0x74, 0x6A) : RGB(0xF4, 0xA8, 0x5A)) : BY_DIM;
                for (q = 0; q < 6; q++) {
                    if (j < cells)
                        cv_rect(cx0 + 5 - q, ry + 14 + q, 4, 1, c);
                    else
                        cv_pset(cx0 + 5 - q, ry + 14 + q, c), cv_pset(cx0 + 8 - q, ry + 14 + q, c);
                }
            }
            {   /* its LEVEL, a thin row of cells under it (the drum track: its own level) */
                int32_t lv = i == TRK_DRUM ? song.g[G_DRLVL] : trk[i].p[P_LEVEL];
                n_knob(30, ry + 22, 112, 9, lv * 1000 / 127, BK_GROW, NW, tc);
            }
            by_diam(152, ry + 17, 7, m[i] ? NW : BY_DIM, 1);   /* how many, in its diamond */
            fmt_int(b, (int32_t)(cells / 2));
            n_text(149, ry + 9, b, 1, m[i] ? NW : BY_DIM);
            if (i == song.sel) {                        /* the selected one: a line to its part of the ship */
                int32_t ax = BY_ANCH[i & 3u][0], ay = BY_ANCH[i & 3u][1], yy;
                for (x = 160; x <= 164; x++)
                    cv_rect(x, ry + 16, 1, 3, tc);
                for (yy = ry + 17 < ay ? ry + 17 : ay; yy <= (ry + 17 < ay ? ay : ry + 17); yy++)
                    cv_rect(164, yy, 2, 1, tc);
                for (x = 164; x <= ax; x++)
                    cv_rect(x, ay, 1, 2, tc);
                cv_rect(ax - 2, ay - 2, 5, 5, NW);
            }
            for (k = 0; k < 7u; k++)                    /* its lines to the effects it uses */
                if ((m[i] >> k) & 1u) {
                    int32_t lx = 162 + (int32_t)i * 7, ty = 29 + (int32_t)k * 17, yy;
                    for (x = 160; x <= lx; x++)
                        cv_pset(x, ry + 17, tc);
                    for (yy = ry + 17 < ty ? ry + 17 : ty; yy <= (ry + 17 < ty ? ty : ry + 17); yy++)
                        cv_pset(lx, yy, tc);
                    for (x = lx; x < 196; x++)
                        cv_pset(x, ty, tc);
                }
        }
        n_texto(8, 206, "REC+FX CLEAR FX", 1, BY_GOLD);  /* the shortcuts */
        n_texto(8, 222, "EDIT+OCT- UNDO", 1, BY_TEAL);
        cv_oy = 0;
        cv_blit(0, (uint32_t)y0);
    }
}
static void n_wavs(void)
{
    n_screen(SCR_REC);
    if (rw_view == 2u && !rw_show()) {                  /* HOME twice: the ship's FX bay */
        n_bay();
        return;
    }
    n_wavs_at(236, 232, 0);
}

/* ---------------------------------------------------------------- MENU --- */
/* HOME held: the section (n/N), its rows (a name, the value large; the cursor's row inverted); ABOUT */
static void n_menu(void)
{
    uint32_t i, sec = mi_sec(ui.menu_sel % MI_COUNT);
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u + settings.lowcut * 7919u + lights_lvl * 1299709u +
                   lights_keys * 15485863u + ml_count() * 2654435u + lights_notes * 32452843u + usb_full * 49979687u +
                   usb_serial * 86028121u + usb_rate * 15486719u + knob_accel_off * 179424673u + cpu1_off * 122949829u +
                   (settings.zoom >> 16) * 2246822519u + settings.zoom * 3266489917u + (ui.msg_t ? nhs(3u, ui.msg) : 0u);
    n_screen(SCR_MENU);
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.menu == 2) {                                 /* ABOUT: the names (GPL: they stay) */
        const char *const L[] = {"NO MAN SKY FM", FELUCCA_VERSION, "BASED ON SLOOP 2.7.1", "BY @ISOD89",
                                        "AND FELUCCA BY", "LEO KUROSHITA", "H\xDCGELTON INSTRUMENTS",
                                        "GPL-3.0, NO WARRANTY", "FONT: TERMINUS (OFL)", "SAMPLES: CC0", "PHASE: CRISPYZEBRA",
                                        "VOICE: KLATTSCH (MIT)", __DATE__,
#if FELUCCA_CPU1
                                        cpu1_label()
#else
                                        "1 CORE"
#endif
                                        };
        uint32_t pass;
        for (pass = 0; pass < 2u; pass++) {
            cv_begin(240, 120, NK);
            for (i = 0; i < sizeof L / sizeof L[0]; i++) {
                int32_t y = 6 + (int32_t)i * 18 - (int32_t)pass * 120;
                if (y > -18 && y < 120)
                    n_text(8, y, L[i], 1, NW);
            }
            cv_blit(0, pass * 120u);
        }
        return;
    }
    {   /* the section's number, or a message */
        char b[8];
        cv_begin(240, 40, NK);
        if (ui.msg_t) {
            n_msg(4, 30);
        } else {
            fmt_int(b, (int32_t)sec + 1);
            str_cpy(b + str_len(b), "/", 2);
            fmt_int(b + str_len(b), MS_COUNT);
            n_text(120 - n_w(b, 2) / 2, 4, b, 2, NW);
        }
        cv_blit(0, 0);
    }
    {
    uint32_t nr = MS_FIRST[sec + 1u] - MS_FIRST[sec] > 3u ? 4u : 3u, rh = nr == 4u ? 47u : 64u;   /* (4 rows: tighter) */
    for (i = 0; i < nr; i++) {                          /* a row a knob: KNOB 1, 2, 3 (4) */
        uint32_t it = MS_FIRST[sec] + i;
        cv_begin(240, rh + 2u, NK);                     /* (with the gap under it: nothing left over) */
        if (it < MS_FIRST[sec + 1u]) {
            int cur = it == ui.menu_sel;
            uint16_t fg = NW;
            const char *v = mi_value(it);
            if (cur)                                    /* the cursor: its knob's texture, a bar on the left */
                n_tex(0, 0, 240, (int32_t)rh - 2, KTEX[i & 3u], NCOL[i & 3u]), cv_rect(0, 0, 4, (int32_t)rh - 2, NW);
            n_texto(10, nr == 4u ? 1 : 4, MI_NAME[it], 1, fg);
            if (v[0])
                n_texto(10, nr == 4u ? 15 : 24, v, n_fit(v, 224, 2), fg);
            else
                n_frame(10, nr == 4u ? 18 : 26, 28, 28, 2, fg);   /* an action: OCT+ opens it */
#if FELUCCA_CDC
            if (it == MI_SERIAL && usb_serial != usb_cdc_now())
                n_disc(224, 40, 5, fg);                 /* (at the next start) */
#endif
            if (it == MI_RATE && usb_rate != usb_rate_now())
                n_disc(224, (int32_t)rh / 2, 5, fg);    /* (at the next start) */
        }
        cv_blit(0, 42u + i * (rh + 2u));
    }
    if (nr == 3u) {                                     /* (a section of three: the fourth band clears) */
        cv_begin(240, 2, NK);
        cv_blit(0, 238u);
    }
    }
}
