/* ---------------------------------------------------------------- CDs: every time the sound of a track changes (PRESETS, the PRESETS page, a drum
 * kit) the screen shows the sounds as CDs in a carousel, the old one rolling out and the new one rolling in, then goes back to what was there.
 * After winampFM's TAPES (ui_winamp.c wt_make / wt_quad): the same 42 models' colours, taken in the same shuffled order, the same PS1 look
 * (low resolution, affine texture, a dithered shade on the far side, the ones behind see-through); here a CD instead of a cassette.
 * The art is made in code into a 64 x 64 texture (cd_make) and drawn as a turning disc (cd_quad). Documented in CDS.md */
#define CD_T 64
#define CD_HOLD 1500u                        /* the screen stays this long after the last change */
#define CD_IN 320u                           /* it comes up ... */
#define CD_OUT 300u                          /* ... and goes down */
#define CD_ROLL 340u                         /* the carousel to the next CD */
#define CD_TURN 760u                         /* the middle one's whole turn: long, it starts and ends soft */
static uint8_t cd_tex[CD_T][CD_T];           /* palette indices, 0 = nothing (outside the disc, the hole) */
static uint16_t cd_pal[16];
static uint32_t cd_t0, cd_open, cd_flip;     /* the last change, when the screen opened, when the middle CD arrived */
static int32_t cd_from;                      /* where the roll began, and when (cd_roll) */
static uint32_t cd_roll;
static int32_t cd_cur, cd_tgt;               /* the carousel, 1/256 CD, chasing the sound's place */
static uint32_t cd_n;                        /* how many in the list */
static uint8_t cd_drum, cd_full, cd_wide;    /* cd_full: the screen to clear; cd_wide: frames of the whole carousel */
/* (smooth) each CD's art is made once: the one in the middle in cd_tex, the others small, here, by their place */
#define CD_MT 24
static uint8_t cd_mini[5][CD_MT * CD_MT];
static uint16_t cd_mpal[5][16];
static uint16_t cd_mid[5];                   /* whose art each holds, sound + 1 */
static uint16_t cd_big;                      /* whose art cd_tex holds, sound + 1 */
static uint32_t cd_txt;                      /* the name last drawn */
/* 0 nothing 1 rim 2 rim dark 3 print 4 label 5 label 2 6 small print 7 the name 8..13 accents 14 the clear ring 15 white */
enum { CM_CLASSIC, CM_ART, CM_COMPUTER, CM_AUDITION, CM_SONYB, CM_FUTURE, CM_TOLAKU, CM_DOODLE, CM_PINK, CM_STAR,
       CM_FISHER, CM_POLIMER, CM_AGFASC, CM_TEAC, CM_AGFAFC, CM_AGFALNS, CM_BASFR, CM_BASFO, CM_HAPPY, CM_BARBIE,
       CM_HOLO, CM_FEB, CM_BRITPOP, CM_EMO, CM_HORTEN, CM_EXCL, CM_SKC, CM_OIL, CM_FLORAL, CM_AGFAY, CM_CLASSICBR,
       CM_SCOTCH, CM_SEIKO, CM_TDKD, CM_KITTYP, CM_KITTYB, CM_SPANKP, CM_SPANKG, CM_AGFAC90, CM_APPLEPIE, CM_3M, CM_DEMAG, CM_N };
#define C3(r, g, b) RGB(0x##r, 0x##g, 0x##b)
static const struct { uint16_t shell, label, label2, ink, name, a[6]; } CD_M[CM_N] = {   /* (winampFM's WT_M, colour for colour) */
    [CM_CLASSIC] = {C3(e6, dc, b8), C3(f6, f4, ee), C3(40, 46, 4a), C3(60, 60, 60), C3(e8, e8, ec), {C3(60, b8, 40), C3(f8, c0, 30), C3(f0, 80, 28), C3(e0, 40, 38), C3(90, 40, c0), C3(30, 70, d0)}},
    [CM_CLASSICBR] = {C3(e8, e0, a0), C3(f8, f8, f0), C3(10, 60, 30), C3(10, 60, 30), C3(f8, d8, 20), {C3(10, 90, 40), C3(10, 90, 40), C3(f8, d8, 20), C3(f8, d8, 20), C3(20, 50, b0), C3(20, 50, b0)}},
    [CM_ART] = {C3(c8, d0, d8), C3(f8, f8, f8), C3(10, 10, 14), C3(30, a0, 80), C3(20, 80, 60), {C3(40, 50, d8), C3(f8, e0, c8), C3(10, 10, 14), C3(d8, e8, f8), C3(f0, 90, 98), C3(20, 80, 60)}},
    [CM_COMPUTER] = {C3(f4, f2, ea), C3(f4, f2, ea), C3(20, 20, 20), C3(20, 20, 20), C3(20, 20, 20), {C3(60, b8, 40), C3(f8, c8, 30), C3(f0, 80, 28), C3(e0, 40, 38), C3(90, 40, a0), C3(30, 80, d0)}},
    [CM_AUDITION] = {C3(1c, 1c, 1c), C3(f0, f0, e8), C3(d8, 28, 20), C3(30, 30, 30), C3(f8, f8, f8), {C3(20, 40, a0), C3(d0, 30, 30), C3(b0, b0, b0), 0, 0, 0}},
    [CM_SONYB] = {C3(c0, c8, d0), C3(f0, ec, dc), C3(20, 90, 50), C3(30, 30, 30), C3(18, 18, 18), {C3(20, 90, 50), C3(30, 30, 30), 0, 0, 0, 0}},
    [CM_FUTURE] = {C3(b8, 18, 10), C3(f4, f2, ee), C3(b8, 18, 10), C3(20, 20, 20), C3(20, 20, 20), {C3(30, f0, 80), C3(d0, 30, 30), C3(e8, 50, 40), 0, 0, 0}},
    [CM_TOLAKU] = {C3(d8, 20, 20), C3(f4, f4, f0), C3(40, b8, e8), C3(20, 20, 20), C3(d8, 20, 30), {C3(20, 90, e0), C3(f8, f8, f8), C3(d8, 20, 30), 0, 0, 0}},
    [CM_DOODLE] = {C3(2c, 2c, 30), C3(f8, c8, 40), C3(90, c8, e8), C3(30, 30, 30), C3(e0, 48, 30), {C3(f8, f8, f8), C3(90, 70, c8), C3(f0, 70, 30), C3(e0, 48, 30), 0, 0}},
    [CM_PINK] = {C3(f4, c0, c8), C3(f8, f8, f8), C3(60, b8, b8), C3(20, 20, 20), C3(18, 18, 18), {C3(e0, 20, 20), C3(f8, f8, f8), C3(e0, a0, a8), 0, 0, 0}},
    [CM_STAR] = {C3(30, 30, 34), C3(c8, 18, 30), C3(c8, 18, 30), C3(f0, c8, 30), C3(f8, d0, 30), {C3(20, 30, 90), C3(10, 18, 50), C3(f0, c8, 30), C3(40, 60, d0), 0, 0}},
    [CM_FISHER] = {C3(f8, d8, 20), C3(f0, e8, d0), C3(e0, 30, 40), C3(30, 30, 30), C3(30, 28, 20), {C3(30, 70, c0), C3(40, a8, 50), C3(f0, 90, 30), C3(f8, f8, f8), 0, 0}},
    [CM_POLIMER] = {C3(f0, 78, 18), C3(30, b0, d0), C3(30, b0, d0), C3(f8, f8, f8), C3(10, 10, 20), {C3(f8, d0, 30), C3(f0, 80, 30), C3(e0, 40, 60), C3(90, 50, c0), 0, 0}},
    [CM_AGFASC] = {C3(30, 80, d0), C3(50, c8, d8), C3(50, c8, d8), C3(10, 20, 70), C3(10, 20, 70), {C3(10, 20, 70), C3(e0, 30, 30), C3(f0, e0, c0), C3(f8, f8, f8), 0, 0}},
    [CM_TEAC] = {C3(b8, d0, f0), C3(b8, d0, f0), C3(48, 20, 18), C3(30, 30, 40), C3(f8, f8, f8), {C3(48, 20, 18), C3(a0, 70, c0), 0, 0, 0, 0}},
    [CM_AGFAFC] = {C3(e0, 28, 28), C3(f0, f0, ea), C3(e0, 28, 28), C3(20, 20, 20), C3(20, 20, 20), {C3(10, 20, 70), C3(e0, 30, 30), C3(f8, f8, f8), C3(10, 10, 10), 0, 0}},
    [CM_AGFALNS] = {C3(f8, 70, 20), C3(f8, 70, 20), C3(f8, 70, 20), C3(20, 20, 20), C3(20, 20, 20), {C3(10, 20, 70), C3(e0, 30, 30), C3(f8, f8, f8), 0, 0, 0}},
    [CM_BASFR] = {C3(20, 20, 22), C3(f0, 40, 20), C3(10, 10, 10), C3(10, 10, 10), C3(f8, f8, f8), {C3(f0, 40, 20), C3(f8, f8, f8), 0, 0, 0, 0}},
    [CM_BASFO] = {C3(20, 20, 22), C3(f8, f8, f4), C3(f8, a0, 30), C3(20, 20, 20), C3(10, 10, 10), {C3(e0, 30, 30), C3(f8, f8, f8), C3(b0, b0, b8), 0, 0, 0}},
    [CM_HAPPY] = {C3(f0, 50, 18), C3(40, b8, d0), C3(40, b8, d0), C3(10, 10, 20), C3(f8, f8, f8), {C3(70, 30, a0), C3(f8, f8, f8), 0, 0, 0, 0}},
    [CM_BARBIE] = {C3(f0, 40, b0), C3(e0, f0, 40), C3(f0, 70, 80), C3(10, 10, 20), C3(e0, 20, 90), {C3(20, 30, 90), C3(f0, 70, 80), C3(a0, 90, b8), 0, 0, 0}},
    [CM_HOLO] = {C3(b0, 88, e8), C3(d8, e0, f0), C3(d8, e0, f0), C3(20, 20, 20), C3(10, 10, 10), {C3(f0, b0, d0), C3(a0, e0, f0), C3(f8, f0, a0), C3(c0, b0, f0), 0, 0}},
    [CM_FEB] = {C3(f4, b8, c4), C3(f8, d8, dc), C3(c8, 18, 30), C3(f8, d8, dc), C3(c8, 18, 30), {C3(d8, 20, 40), C3(f0, a0, b0), 0, 0, 0, 0}},
    [CM_BRITPOP] = {C3(90, b0, d0), C3(f4, f0, e8), C3(70, a8, d8), C3(20, 20, 20), C3(d0, 40, 40), {C3(d0, 40, 40), C3(20, 20, 20), C3(70, a8, d8), 0, 0, 0}},
    [CM_EMO] = {C3(20, 20, 20), C3(f0, ec, e4), C3(d0, 40, 40), C3(20, 20, 20), C3(20, 20, 20), {C3(d0, 40, 40), C3(f0, ec, e4), C3(20, 20, 20), 0, 0, 0}},
    [CM_HORTEN] = {C3(c8, cc, c4), C3(f0, 50, 90), C3(f0, 50, 90), C3(20, 20, 20), C3(f8, f8, f8), {C3(10, 10, 10), C3(30, 30, 90), 0, 0, 0, 0}},
    [CM_EXCL] = {C3(e8, 28, 20), C3(f8, f4, f0), C3(e8, 28, 20), C3(20, 20, 20), C3(f8, d8, 20), {C3(f8, d8, 20), C3(c0, 30, 30), 0, 0, 0, 0}},
    [CM_SKC] = {C3(d0, d8, e0), C3(d0, d8, e0), C3(20, 20, 28), C3(20, 20, 30), C3(f8, f8, f8), {C3(f0, 30, 70), C3(f8, c0, 30), C3(30, a0, b0), C3(20, 20, 28), C3(40, 60, 90), 0}},
    [CM_OIL] = {C3(d8, d8, e0), C3(f0, b0, c0), C3(f0, b0, c0), C3(30, 30, 30), C3(f8, f8, f8), {C3(f0, a0, 60), C3(a0, d0, f0), C3(c0, 90, d8), C3(f8, f0, 90), C3(60, 50, 70), 0}},
    [CM_FLORAL] = {C3(d8, d8, e0), C3(20, 28, 20), C3(20, 28, 20), C3(f8, f8, f8), C3(f8, f8, f8), {C3(e0, 70, 60), C3(f0, b0, a0), C3(40, 80, 40), C3(f8, f8, f0), 0, 0}},
    [CM_AGFAY] = {C3(f8, c0, 20), C3(f8, c0, 20), C3(f8, c0, 20), C3(20, 20, 20), C3(20, 20, 20), {C3(10, 20, 70), C3(e0, 30, 30), C3(f8, f8, f8), 0, 0, 0}},
    [CM_SCOTCH] = {C3(80, b8, f0), C3(a8, d0, d8), C3(70, 48, 30), C3(10, 10, 20), C3(10, 10, 20), {C3(70, 48, 30), C3(f0, e0, a0), 0, 0, 0, 0}},
    [CM_SEIKO] = {C3(c8, c8, c4), C3(f4, f4, f0), C3(30, a8, e0), C3(20, 20, 20), C3(f8, f8, f8), {C3(30, a8, e0), C3(10, 10, 20), 0, 0, 0, 0}},
    [CM_TDKD] = {C3(50, 48, d8), C3(50, 48, d8), C3(10, 10, 14), C3(f8, f8, f8), C3(e8, d8, 90), {C3(10, 10, 14), C3(70, 90, f0), C3(d8, c0, 60), C3(f8, f8, f8), 0, 0}},
    [CM_KITTYP] = {C3(f8, c8, d4), C3(f8, d8, e0), C3(30, 18, 20), C3(e0, 30, 70), C3(e0, 30, 70), {C3(e0, 30, 70), C3(30, 18, 20), C3(f8, f8, f8), 0, 0, 0}},
    [CM_KITTYB] = {C3(a8, c8, f0), C3(b8, d4, f8), C3(20, 30, 40), C3(30, 50, c0), C3(30, 50, c0), {C3(30, 50, c0), C3(20, 30, 40), C3(f8, f8, f8), 0, 0, 0}},
    [CM_SPANKP] = {C3(f8, b0, c0), C3(f8, b0, c0), C3(20, 18, 20), C3(e0, 50, 70), C3(e0, 50, 70), {C3(60, d0, b0), C3(f0, 60, 80), C3(60, a0, d0), C3(20, 18, 20), 0, 0}},
    [CM_SPANKG] = {C3(60, f0, 60), C3(60, f0, 60), C3(20, 18, 20), C3(e0, 40, 90), C3(e0, 40, 90), {C3(60, d0, b0), C3(f0, 50, 90), C3(60, a0, e0), C3(20, 18, 20), 0, 0}},
    [CM_AGFAC90] = {C3(f8, b0, 20), C3(f8, b0, 20), C3(f8, b0, 20), C3(20, 20, 20), C3(f8, f8, f8), {C3(10, 20, 70), C3(e0, 30, 30), C3(f8, f8, f8), 0, 0, 0}},
    [CM_APPLEPIE] = {C3(22, 22, 22), C3(f8, b8, 10), C3(f8, b8, 10), C3(20, 20, 20), C3(20, 20, 20), {C3(20, 20, 20), C3(a0, a0, a0), C3(f8, f8, f8), 0, 0, 0}},
    [CM_3M] = {C3(f8, 50, 20), C3(e0, 28, 28), C3(f8, f0, e8), C3(f8, f0, e8), C3(f8, f0, e8), {C3(f8, f0, e8), 0, 0, 0, 0, 0}},
    [CM_DEMAG] = {C3(d0, d8, e0), C3(e8, e4, d8), C3(30, 28, 28), C3(30, 28, 28), C3(20, 20, 20), {C3(b0, 78, 40), C3(30, 30, 30), C3(90, 80, c0), C3(c0, 60, 40), C3(c0, a0, 40), C3(e0, 40, 40)}},
};
static const uint8_t CD_ORDER[CM_N] = {      /* every model once, shuffled (winampFM's WT_ORDER): the list takes them in turn */
    CM_TEAC, CM_3M, CM_COMPUTER, CM_AGFAY, CM_TDKD, CM_EXCL, CM_TOLAKU, CM_BASFR, CM_BASFO, CM_BARBIE, CM_HOLO, CM_POLIMER, CM_CLASSIC, CM_SEIKO,
    CM_SKC, CM_SPANKP, CM_AGFAC90, CM_KITTYB, CM_FISHER, CM_DOODLE, CM_SCOTCH, CM_HAPPY, CM_CLASSICBR, CM_BRITPOP, CM_ART, CM_FUTURE, CM_STAR,
    CM_EMO, CM_KITTYP, CM_OIL, CM_AGFASC, CM_AUDITION, CM_SPANKG, CM_DEMAG, CM_FLORAL, CM_FEB, CM_HORTEN, CM_APPLEPIE, CM_PINK, CM_AGFAFC,
    CM_SONYB, CM_AGFALNS};
/* the print of each model, after its tape's art: 0 rainbow rings (the classic tapes' rainbow), 1 a label band across, 2 sunburst, 3 dots,
 * 4 stripes, 5 the square-and-diamond logo with lines, 6 stars, 7 holographic, 8 checker band, 9 petals */
static const uint8_t CD_ART[CM_N] = {
    [CM_CLASSIC] = 0, [CM_CLASSICBR] = 0, [CM_ART] = 9, [CM_COMPUTER] = 4, [CM_AUDITION] = 1, [CM_SONYB] = 1, [CM_FUTURE] = 2, [CM_TOLAKU] = 8,
    [CM_DOODLE] = 3, [CM_PINK] = 3, [CM_STAR] = 6, [CM_FISHER] = 4, [CM_POLIMER] = 2, [CM_AGFASC] = 5, [CM_TEAC] = 1, [CM_AGFAFC] = 5,
    [CM_AGFALNS] = 5, [CM_BASFR] = 1, [CM_BASFO] = 0, [CM_HAPPY] = 2, [CM_BARBIE] = 3, [CM_HOLO] = 7, [CM_FEB] = 9, [CM_BRITPOP] = 4,
    [CM_EMO] = 8, [CM_HORTEN] = 2, [CM_EXCL] = 6, [CM_SKC] = 4, [CM_OIL] = 7, [CM_FLORAL] = 9, [CM_AGFAY] = 5, [CM_SCOTCH] = 1,
    [CM_SEIKO] = 0, [CM_TDKD] = 7, [CM_KITTYP] = 3, [CM_KITTYB] = 3, [CM_SPANKP] = 6, [CM_SPANKG] = 6, [CM_AGFAC90] = 5, [CM_APPLEPIE] = 8,
    [CM_3M] = 2, [CM_DEMAG] = 0};
UI_SMALL static void up_name(uint32_t k, char *b);         /* (upreset.c) */
static uint32_t cd_model(uint32_t n) { return CD_ORDER[(n + (cd_drum ? 7u : 0u)) % CM_N]; }
static const char *cd_name(uint32_t n, const char **kind)    /* the sound n of the list: its name and its kind */
{
    static char b[13];
    if (cd_drum) {
        *kind = "DRUMS";
        return DRUM_KIT_NAMES[n % DRUM_KITS];
    }
    if (n < NBANK) {
        *kind = RW_ROLE[BANK[n].kind & 7u];
        return BANK[n].name;
    }
    *kind = "USER";
    n -= NBANK;
    if (n < up_count()) {
        up_name(up_nth(n), b);
        return b;
    }
    return "USER";
}
static void cd_px(int32_t x, int32_t y, uint8_t c) { if ((uint32_t)x < CD_T && (uint32_t)y < CD_T && cd_tex[y][x]) cd_tex[y][x] = c; }
static int32_t cd_text(int32_t x, int32_t y, const char *s, uint8_t c)   /* the small font at half size (2 x 2 pixels of a glyph = 1 texel) */
{
    const felucca_font_t *f = &FONT_S;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t i = gy * w + gx;
                if ((gd[i >> 3] >> (7u - (i & 7u))) & 1u)
                    cd_px(x + ((int32_t)gx - f->pad) / 2, y + (int32_t)gy / 2, c);
            }
        x += f->adv[gi] / 2;
    }
    return x;
}
static int32_t cd_tw(const char *s) { return text_w(&FONT_S, s) / 2; }
static uint32_t cd_ang(int32_t dx, int32_t dy)          /* 0..255 round from the right, clockwise (an octant atan, good enough for art) */
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, a;
    if (!ax && !ay)
        return 0;
    a = ax >= ay ? ay * 32 / ax : 64 - ax * 32 / ay;     /* 0..64 in the first quadrant */
    if (dx < 0)
        a = 128 - a;
    if (dy < 0)
        a = 256 - a;
    return (uint32_t)a & 255u;
}
/* the CD of sound n into cd_tex / cd_pal */
static void cd_art(uint32_t n, uint16_t *pal, uint8_t *dst, int32_t t)   /* the art of sound n, t x t texels (64: with its words) */
{
    uint32_t md = cd_model(n), art = CD_ART[md], i;
    int32_t xx, yy;
    const char *kind, *nm = cd_name(n, &kind);
    char b[16];
    pal[0] = NK;
    pal[1] = RGB(0xc8, 0xcc, 0xd4), pal[2] = RGB(0x70, 0x74, 0x80);
    pal[3] = CD_M[md].shell, pal[4] = CD_M[md].label, pal[5] = CD_M[md].label2, pal[6] = CD_M[md].ink, pal[7] = CD_M[md].name;
    for (i = 0; i < 6u; i++)
        pal[8 + i] = CD_M[md].a[i] ? CD_M[md].a[i] : CD_M[md].a[i & 1u];
    pal[14] = RGB(0xa8, 0xb0, 0xbc), pal[15] = NW;
    for (yy = 0; yy < t; yy++)
        for (xx = 0; xx < t; xx++) {
            int32_t x = xx * CD_T / t, y = yy * CD_T / t, dx = 2 * x - 63, dy = 2 * y - 63, d2 = dx * dx + dy * dy, r = g_isqrt(d2) / 2;   /* (r in texels from the middle) */
            uint32_t a = cd_ang(dx, dy), c = 3;
            if (r >= 32 || r < 4) {
                dst[yy * t + xx] = 0;                   /* outside, the hole */
                continue;
            }
            if (r >= 31)
                c = 1;                                  /* the silver rim */
            else if (r < 8)
                c = 14;                                 /* the clear ring round the hole (solid) */
            else if (r < 10)
                c = 2;                                  /* the stacking ring */
            else if (r < 13)
                c = 4;                                  /* the label round the hub */
            else
                switch (art) {
                case 0: c = r > 27 ? 3 : 8 + (uint32_t)(r - 13) / 3u % 6u; break;                             /* rainbow rings */
                case 1: c = y > 23 && y < 40 ? (y == 24 || y == 39 ? 6 : 4) : y >= 40 && y < 44 ? 8 : 3; break;   /* a label band */
                case 2: c = (a >> 4) & 1u ? 8 + (a >> 5) % 3u : 3; break;                                       /* sunburst */
                case 3: c = ((x % 9 - 4) * (x % 9 - 4) + (y % 9 - 4) * (y % 9 - 4)) < 6 ? 8 + (uint32_t)(x / 9 + y / 9) % 4u : 3; break;   /* dots */
                case 4: c = y % 10 < 3 ? 8 + (uint32_t)(y / 10) % 6u : 3; break;                               /* stripes */
                case 5: c = x > 14 && x < 27 && y > 14 && y < 27 ? (x - 20 + y - 20 < 4 && x - 20 + y - 20 > -4 && x - y < 4 && y - x < 4 ? 9 : 8)
                          : y > 36 && y < 50 && (y & 1) ? 6 : 3; break;                                       /* the square and diamond, lines */
                case 6: { uint32_t h = (uint32_t)(x * 73 + y * 151) % 97u; c = h < 3 ? 15 : h < 6 ? 8 + h % 3u : 3; } break;   /* stars */
                case 7: c = 8 + (a / 11u + (uint32_t)r / 4u) % 4u; break;                                        /* holographic */
                case 8: c = y > 22 && y < 42 ? (((x >> 2) + (y >> 2)) & 1 ? 6 : 4) : 3; break;                  /* a checker band */
                default: c = (((a * 6u) >> 4) & 15u) < (uint32_t)(20 - r / 2 > 0 ? 20 - r / 2 : 0) / 2 ? 8 + (a * 6u >> 8) % 2u : 3; break;   /* petals */
                }
            dst[yy * t + xx] = (uint8_t)c;
        }
    if (t != CD_T)
        return;
    str_cpy(b, kind, sizeof b);                         /* the kind on top, the name below (one line, as on the tapes) */
    cd_text(32 - cd_tw(b) / 2, 15, b, 6);
    i = (uint32_t)cd_tw(nm);
    cd_text(32 - (int32_t)(i > 46u ? 46u : i) / 2, 44, nm, 7);
}
/* the disc drawn turning: centre cx, cy; diameter w0; yaw a (1024 a turn), spin s (the print round the hole, 1024 a turn); bs: block size;
 * the ones behind are solid too, only darker (shade). The texture mapped linearly (the PS1 wobble), the far side darker in a 4 x 4 dither; a
 * light line sweeps the face as it turns. Only the rows of this band, stepping along the texture, the colours worked out once */
static void cd_quad(const uint8_t *tex, int32_t t, const uint16_t *pal, int32_t cx, int32_t cy, int32_t w0, uint32_t a, uint32_t s, int32_t shade, int32_t bs)
{
    static const uint8_t BY[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int32_t c = g_cos(a), sn = g_sin(a), hw = (w0 / 2) * c >> 15, zl = -(w0 / 2) * sn >> 15, zr = -zl, hl, hr, sx;
    int32_t sc = g_cos(s) >> 1, ss = g_sin(s) >> 1, flip = hw < 0, T = w0 / 40 + 1, sw = T * (sn < 0 ? -sn : sn) >> 15;
    int32_t b0 = -cv_oy, b1 = b0 + (int32_t)cv_h, tq = t << 11;
    uint16_t lit[16], dim[16], sil[2][6];
    uint32_t i, k;
    if (flip)
        hw = -hw, zl = -zl, zr = -zr;
    hl = w0 * 420 / (420 + zl), hr = w0 * 420 / (420 + zr);   /* (a mild perspective: the disc stays in its rows) */
    if (sw > 0) {                                       /* the edge: a thin silver side beside the near edge */
        int32_t ex = zl < 0 ? cx - hw - sw : cx + hw, hn = zl < 0 ? hl : hr;
        cv_rect(ex, cy - hn / 3, sw + 1, hn * 2 / 3, pal[2]);
    }
    if (hw < 2)
        return;
    for (i = 0; i < 16u; i++) {
        lit[i] = g_mix565(pal[i], NW, 7);
        dim[i] = g_mix565(pal[i], NK, 7);
    }
    for (k = 0; k < 6u; k++)                            /* the silver catches a rainbow as it turns */
            sil[0][k] = g_mix565(pal[1], pal[8 + k], 6), sil[1][k] = g_mix565(pal[2], pal[8 + k], 6);
    for (sx = cx - hw; sx < cx + hw; sx += bs) {
        int32_t u = (sx - (cx - hw)) * 1024 / (2 * hw), h = hl + (hr - hl) * u / 1024, top = cy - h / 2, sy = top, dv, fu, fv, tu, tv, du, dw;
        int32_t dark = shade + (flip ? 2 : 0) + (u < 512 ? (zl > 0 ? zl / 6 : 0) : (zr > 0 ? zr / 6 : 0));
        uint32_t sxb = (uint32_t)(sx / bs) & 3u;
        if (sx + bs + cv_ox <= 0 || sx + cv_ox >= (int32_t)cv_w || h < 1)
            continue;
        if (dark > 15)
            dark = 15;
        if (sy < b0)
            sy = top + ((b0 - top + bs - 1) / bs) * bs;
        dv = tq / h;                                    /* (texels a row, 1 / 2048) */
        fu = ((flip ? 1023 - u : u) - 512) * t * 2;
        fv = (sy - top) * dv - tq / 2;
        tu = (int32_t)(((int64_t)fu * sc - (int64_t)fv * ss) >> 14) + tq / 2;
        tv = (int32_t)(((int64_t)fu * ss + (int64_t)fv * sc) >> 14) + tq / 2;
        du = -(dv * bs * ss >> 14), dw = dv * bs * sc >> 14;
        for (; sy < top + h && sy < b1; sy += bs, tu += du, tv += dw) {
            uint32_t ti;
            uint16_t col;
            if ((uint32_t)tu >= (uint32_t)tq || (uint32_t)tv >= (uint32_t)tq || !(ti = tex[(tv >> 11) * t + (tu >> 11)]))
                continue;
            col = ti <= 2u ? sil[ti - 1u][((uint32_t)(sy + (int32_t)(a >> 4)) / 6u) % 6u] : pal[ti];
            if (((sx + sy - (int32_t)(a >> 2)) & 63) < 4)
                col = lit[ti];                           /* the light across the face */
            else if (dark > 0 && BY[((uint32_t)(sy / bs) & 3u) * 4u + sxb] < (uint32_t)dark)
                col = dim[ti];
            if (bs == 1)
                cv_px[(uint32_t)(sy + cv_oy) * cv_w + (uint32_t)(sx + cv_ox)] = swap16(col);
            else
                cv_rect(sx, sy, bs, bs, col);
        }
    }
}
/* a change: called with the sound's new place in the list and which way it went */
static void cd_note(uint32_t pos, uint32_t total, int32_t dir)
{
    uint32_t now = fm1_ms ? fm1_ms : 1u, k;             /* (not fm1_ms | 1: one ms ahead, the age would wrap) */
    if (!total)
        return;
    cd_drum = (uint8_t)is_drum(TSEL);
    if (!cd_t0 || cd_n != total) {                      /* the screen opens: the carousel starts one CD away and rolls in */
        cd_open = now, cd_n = total, cd_full = 1;
        for (k = 0; k < 5u; k++)
            cd_mid[k] = 0;
        cd_big = 0;
        cd_cur = (int32_t)pos * 256, cd_flip = now;
        (void)dir;
    }
    cd_tgt = (int32_t)pos * 256;
    if (cd_tgt - cd_cur > (int32_t)total * 128)        /* (round the end of the list: the short way) */
        cd_cur += (int32_t)total * 256;
    else if (cd_cur - cd_tgt > (int32_t)total * 128)
        cd_cur -= (int32_t)total * 256;
    cd_from = cd_cur, cd_roll = now;
    cd_t0 = now;
    ui.force = 1;
}
/* everything by the clock, expo-out: fast at once, soft at the end; q 0..1024 -> 0..1024 */
static int32_t cd_ease(int32_t q)
{
    int32_t r = 1024 - (q > 1024 ? 1024 : q), r2 = r * r >> 10;
    return 1024 - (r2 * r2 >> 10);
}
static int32_t cd_smooth(int32_t q)                     /* 0..1024, soft at both ends (a smoothstep of a smoothstep) */
{
    int32_t i;
    q = q > 1024 ? 1024 : q;
    for (i = 0; i < 2; i++)
        q = q * q / 1024 * (3072 - 2 * q) / 1024;
    return q;
}
static int cd_on(void)
{
    if (cd_t0 && fm1_ms - cd_t0 >= CD_HOLD + CD_OUT)
        cd_t0 = 0, ui.force = 1;
    return cd_t0 != 0;
}
static uint32_t cd_idx(int32_t d) { return (uint32_t)((cd_tgt / 256 + d) % (int32_t)cd_n + (int32_t)cd_n) % cd_n; }
static uint32_t cd_slot(int32_t d) { return (uint32_t)(((cd_tgt / 256 + d) % 5 + 5) % 5); }
static void cd_prep(void)                               /* the art that is missing: the middle one now, the small ones two a frame */
{
    static const int8_t ORD[5] = {0, 1, -1, 2, -2};
    uint32_t o, made = 0, c = cd_idx(0);
    if (cd_big != c + 1u)
        cd_art(c, cd_pal, &cd_tex[0][0], CD_T), cd_big = (uint16_t)(c + 1u);
    for (o = 0; o < 5u && made < 2u; o++) {
        uint32_t i = cd_idx(ORD[o]), sl = cd_slot(ORD[o]);
        if (cd_mid[sl] != i + 1u)
            cd_art(i, cd_mpal[sl], cd_mini[sl], CD_MT), cd_mid[sl] = (uint16_t)(i + 1u), made++;
    }
}
static int32_t cd_scale(void)                           /* the middle one grows in, and goes: 0..1024 */
{
    uint32_t now = fm1_ms, ag = now - cd_t0, op = now - cd_open;
    if (op < CD_IN)
        return 64 + cd_ease((int32_t)op * 1024 / (int32_t)CD_IN) * 960 / 1024;
    if (ag > CD_HOLD)
        return 1024 - cd_smooth((int32_t)(ag - CD_HOLD) * 1024 / (int32_t)CD_OUT);
    return 1024;
}
static void cd_discs(int32_t sc)                        /* the carousel in this canvas (cv_oy, cv_ox set by the caller); sc < 1024: the middle one only, that big */
{
    static const int8_t ORD[5] = {-2, 2, -1, 1, 0};     /* far ones first */
    uint32_t now = fm1_ms, o;
    int32_t frac = cd_cur - cd_tgt;
    for (o = 0; o < 5u; o++) {
        int32_t d = ORD[o], p = d * 256 + frac, ap = p < 0 ? -p : p, x, cy, w0;
        uint32_t idx = cd_idx(d), sl = cd_slot(d), a, s = 0;
        if (ap > 640 || (sc < 1024 && d))
            continue;
        x = 120 + p * 74 / 256;
        w0 = ap < 256 ? 124 - ap * 54 / 256 : 70 - (ap - 256) * 16 / 256;
        cy = 104 - ap * 10 / 256;
        w0 = w0 * sc / 1024;
        if (ap < 40) {                                  /* the middle one: a whole turn as it arrives, then it faces you and spins */
            uint32_t t = now - cd_flip;
            a = t < CD_TURN ? (uint32_t)cd_smooth((int32_t)t * 1024 / (int32_t)CD_TURN) & 1023u : 0u;
            s = song.playing ? now * 3u / 4u : now / 6u;
            if (cd_big == idx + 1u)
                cd_quad(&cd_tex[0][0], CD_T, cd_pal, x, cy, w0, a, s & 1023u, ap / 90, a || sc < 1024 ? 2 : 1);   /* (moving: 2 x 2 blocks, lighter) */
        } else {
            a = (uint32_t)((p > 0 ? 1 : -1) * (ap > 256 ? 110 : ap * 110 / 256)) & 1023u;   /* the others turned toward the middle */
            if (cd_mid[sl] == idx + 1u)
                cd_quad(cd_mini[sl], CD_MT, cd_mpal[sl], x, cy, w0, a, 0, ap / 90, 2);
        }
    }
}
static void cd_words(void)                              /* the name, its kind, where in the list */
{
    const char *kind, *nm = cd_name(cd_idx(0), &kind);
    char b[24];
    int32_t y = 182;
    n_texto(120 - n_w(nm, 2) / 2, y, nm, 2, NW);
    str_cpy(b, kind, 12);
    str_cpy(b + str_len(b), "  ", 24 - str_len(b));
    fmt_int(b + str_len(b), (int32_t)cd_idx(0) + 1);
    str_cpy(b + str_len(b), "/", 24 - str_len(b));
    fmt_int(b + str_len(b), (int32_t)cd_n);
    n_texto(120 - n_w(b, 1) / 2, y + 34, b, 1, NCOL[song.sel & 3u]);
}
static void cd_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t sc)   /* one window of the screen: black, the CDs in it */
{
    cv_begin(w, h, NK);
    cv_ox = -(int32_t)x, cv_oy = -(int32_t)y;
    cd_discs(sc);
    cv_ox = 0, cv_oy = 0;
    cv_blit(x, y);
}
/* (smooth) the LCD's SPI is the slow part, so only the smallest window that changes goes to it: the disc's square while
 * it spins (a fifth of the screen) or scales or turns, the carousel's rows while it rolls; black behind, no see-through */
static void n_cd(void)
{
    uint32_t sig, el = fm1_ms - cd_roll, op = fm1_ms - cd_open, ag = fm1_ms - cd_t0;
    int32_t d, sc;
    cd_cur = el >= CD_ROLL ? cd_tgt : cd_from + (cd_tgt - cd_from) * cd_ease((int32_t)el * 1024 / (int32_t)CD_ROLL) / 1024;
    d = cd_tgt - cd_cur;
    if (d > 40 || d < -40)
        cd_flip = fm1_ms;                               /* (still rolling: the middle one's turn starts as it lands) */
    g_overlay = 1;
    g_step();
    cd_prep();
    sc = cd_scale();
    sig = cd_idx(0) + 1u + (uint32_t)song.sel * 4096u;
    if (cd_full) {                                      /* it opens: the screen goes black once */
        lcd_fill(0, 0, 240, 240, NK);
        cd_full = 0, cd_txt = 0, cd_wide = 2;
    }
    if (cd_txt != sig) {                                /* the words only when they change */
        cv_begin(240, 62, NK);
        cv_oy = -178;
        cd_words();
        cv_oy = 0;
        cv_blit(0, 178);
        cd_txt = sig;
    }
    if (d || cd_wide || (sc == 1024 && op < CD_IN + 80u) || (ag > CD_HOLD && ag < CD_HOLD + 60u)) {   /* the whole carousel: rolling, or the others come / go */
        cd_win(0, 30, 240, 124, sc);
        cd_win(0, 154, 240, 24, sc);
        if (cd_wide)
            cd_wide--;
    } else if (sc < 1024 || fm1_ms - cd_flip < CD_TURN + 40u) {
        cd_win(54, 30, 132, 148, sc);                   /* it scales or turns */
    } else {
        cd_win(58, 42, 124, 124, sc);                   /* it spins */
    }
    ui.force = 1;
}
