/* SPDX-License-Identifier: GPL-3.0-only */
/* No Man Sky FM · the game on HOME: a top-down space exploration in the spirit of Low Mem Sky (Paul Nicholas,
 * PICO-8, 2018), written anew in C with the same techniques (none of its code: it is not published under a licence).
 *   SPACE    KNOB 1 turns the ship, KNOB 2 is the throttle, OCT- held boosts (A). Three layers of stars at three
 *            depths (parallax; the near ones streak at speed). Planets are spinning globes: a noise map wrapped onto
 *            a disc with a horizontal squeeze toward the rim (the idea of gamax92's Pico World), lit from one side
 *            through ordered dither. The exhaust leaves particles where they were made, fading white, orange, purple.
 *            OCT+ near a planet lands (B).
 *   SURFACE  value noise in two octaves; the heights become textures (water that shimmers, sand, grass, hills,
 *            peaks), the coasts a white line. KNOB 1 / KNOB 2 walk left-right / up-down (OCT- held runs); OCT+
 *            collects a resource, or by the ship takes off; OCT+ held calls the ship. Creatures wander.
 *            Sentinels hover over some planets: they only follow you, never attack.
 *   GALAXY   sectors of 512 x 512 px, each with its planets (and in some a station) from a hash of its place:
 *            nothing is stored. A + B (OCT- held, OCT+) opens the map: KNOB 1 / KNOB 2 move the cursor, B warps
 *            there (fuel: a sector a unit, as far as the ship's class reaches), A closes. The aim: the centre.
 *   FUEL     the boost and the warp burn it; the orange resource refills it as you board the ship.
 *   STATION  B in reach docks: sell what you carry for credits, buy the next of 5 ship classes (speed, cargo,
 *            warp range, tank). KNOB 1 / 2 pick a row, B does it, A leaves.
 *   SAVE     the game (44 B) is appended to the free flash sector 0xFB000, 64 records before one erase.
 *   KNOB 3   the pixel size (2, 3, 4)       KNOB 4   the scanner: a pulse, then marks on what is near
 * Drawn in logical pixels of Z x Z straight into the canvas, in two halves of 120 rows (no frame buffer); the music's
 * head (tempo, play, the track) and the resources on top. The opening: the picture (assets/abertura.png, when there is
 * one: tools/gen_abertura.py), the logo and the credits, then the game fades in. */
#include "nms_abertura.h"
#define G_PIC_MS (NMS_ABERTURA ? 3400u : 0u)            /* the opening: the picture, */
#define G_LOGO_END (G_PIC_MS + 4000u)                   /* .. the ship from the side, 4 s (g_open_draw), */
#define G_ARRIVE_MS 2400u                               /* .. then it arrives in space: close up, fast, slowing and
                                                         * pulling out to the middle height (g_draw); the game */
#define GH(a, b) g_hash((a), (b), 0x9E37u)
/* a time stamp: now, never 0 (0 = none) and never ahead of now (a stamp ahead would make "now - stamp" wrap round) */
#define G_STAMP (fm1_ms ? fm1_ms : 1u)
static uint32_t g_hash(int32_t x, int32_t y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static int32_t g_isqrt(int32_t v)
{
    int32_t r = 0, b = 1 << 30;
    if (v <= 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}
static int32_t g_sin(uint32_t a) { return SINE[a & 1023u]; }            /* Q15, 1024 a turn */
static int32_t g_cos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }

/* value noise: 0..255, cells of 1 << sh px; wrap > 0: x repeats every wrap cells (a globe) */
static int32_t g_vnoise(int32_t x, int32_t y, uint32_t seed, uint32_t sh, int32_t wrap)
{
    int32_t cx = x >> sh, cy = y >> sh, fx = (x & ((1 << sh) - 1)) << (8 - sh), fy = (y & ((1 << sh) - 1)) << (8 - sh);
    int32_t c1 = cx + 1, a, b, c, d, top, bot;
    if (wrap) {
        cx = ((cx % wrap) + wrap) % wrap;
        c1 = (cx + 1) % wrap;
    }
    a = (int32_t)(g_hash(cx, cy, seed) & 255u), b = (int32_t)(g_hash(c1, cy, seed) & 255u);
    c = (int32_t)(g_hash(cx, cy + 1, seed) & 255u), d = (int32_t)(g_hash(c1, cy + 1, seed) & 255u);
    fx = fx * fx * (768 - 2 * fx) >> 16;                /* smoothstep */
    fy = fy * fy * (768 - 2 * fy) >> 16;
    top = a + ((b - a) * fx >> 8);
    bot = c + ((d - c) * fx >> 8);
    return top + ((bot - top) * fy >> 8);
}

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

enum { GM_INTRO, GM_SPACE, GM_LAND, GM_SURF, GM_TAKEOFF, GM_MAP, GM_WARP, GM_DOCK, GM_BATTLE, GM_RACE };
#define G_NPART 40u
#define G_NGOT 48u
#define G_NSEN 5u                                       /* (5 on the hostile planets) */
#define G_SECT 512                                      /* a sector: px a side */
#define G_HOME 256                                      /* the start: the middle of sector (0, 0) */
#define G_CX 40                                         /* the centre of the galaxy (a sector): the aim */
#define G_CY -30
#define G_FUEL_ORANGE 3                                 /* fuel units an orange resource makes */
typedef struct {
    int32_t x, y, r;                                    /* centre (world px), radius */
    uint32_t seed;
    uint8_t water, land, land2, ring;                   /* colours (GCOL index): the sea, the land, the hills; a ring
                                                         * (0 none, else its colour + 1) */
} planet_t;
/* the 5 ship classes: speed (px / s a throttle step), cargo (resources), warp range (sectors), tank, price */
static const struct { uint8_t speed, cargo, range, tank; uint16_t price; } G_CLASS[5] = {
    {7, 20, 2, 12, 0}, {8, 30, 3, 16, 60}, {9, 45, 5, 20, 150}, {10, 65, 8, 26, 320}, {12, 99, 12, 34, 600}};
static struct {
    uint8_t mode, z, fade, inited, after;               /* after: the mode the opening leads to (a save on foot) */
    uint32_t t_mode, t_last, t_scan, t_fade, frame;
    int32_t sx, sy, vx, vy;                             /* the ship: Q8 world px, Q8 px / s */
    uint32_t ang;                                       /* 0..1023 */
    int32_t thr;                                        /* KNOB 2: 0..10 */
    planet_t pl;                                        /* the planet landed on / landing on */
    int16_t psx, psy;                                   /* its sector, its number there */
    uint8_t pk;
    int32_t px, py, tx, ty;                             /* on foot: Q8 surface px, where the knobs ask */
    int32_t lx, ly;                                     /* the ship parked (surface px) */
    uint8_t walk, facing;
    uint32_t call_t;                                    /* the ship called (HOME / B held on a planet): when, 0 = none */
    uint32_t home_t;                                    /* HOME tapped once (waiting for a second tap: the radar) */
    uint8_t mz;                                         /* the map's zoom (KNOB 3 on the map) */
    uint8_t space_z;                                    /* the zoom you had in space, back after a take-off */
    uint8_t own, eng, wpn, pick;
    uint8_t q_on, q_k, q_n, q_pk;                       /* (the user) a quest: taken, the resource, how many, where */
    int16_t q_sx, q_sy, nx, ny;                         /* .. its planet's sector; the NPC's place on this planet */
    uint8_t fr_ev;                                      /* (the user) the passing ship hailed (OCT+): its event, 1..3 */
    uint32_t fr_e0;                                     /* .. since */
    uint32_t q_done, talk_t, fr_t;                      /* (quests done: a bit a planet); talking since; a passing ship */                        /* (the user) ships owned (bit a class), engine and weapon levels,
                                                         * the ship shown in the station */
    int16_t zc;                                         /* KNOB 3 turned down past the widest: the charge (256: the system view) */
    uint32_t zc_t, zin_t;                               /* .. last turned; the system view entered */
    int32_t cx_, cy_;                                   /* .. where it lands (surface px) */
    int32_t zx, zy;                                     /* the mining beam (B on foot): where it hits (surface px) */
    uint32_t zap_t;                                     /* .. when it was fired, 0 = none */
    uint16_t res[4], cred, warps;
    uint8_t cls, fuel, flags;                           /* flags: 2 the centre reached */
    uint32_t boost_ms;                                  /* boost time not yet paid in fuel */
    int32_t mx, my, wx, wy;                             /* the map's cursor, the warp's target (sectors) */
    int32_t stx, sty, row;                              /* docked: the station's sector, the row picked */
    struct { int32_t x, y; uint32_t hit; uint8_t hp; } sen[G_NSEN];   /* the sentinels (Q8 surface px; hit: when the beam
                                                         * last hit it; hp 3..1) */
    uint8_t sen_n, sen_seen, angry;                     /* angry: a hostile planet's (5, alert near you, shooting back) */
    uint32_t alert_t;                                   /* one blew up: all alert till then */
    int32_t boom_x, boom_y;                             /* .. where (surface px), when */
    uint32_t boom_t;
    uint8_t zap_sen;                                    /* the beam on a sentinel (its number + 1) */
    int32_t zrx, zry;                                   /* .. on a resource (its cell): taken when B is let go */
    uint8_t zres;
    struct { int32_t x, y, vx, vy; uint32_t shot; } foe[G_NSEN];   /* the sentinels' ships, after a take-off (Q8 world px) */
    uint8_t foe_n, hit_flash;
    uint32_t foe_t, hit_t;
    struct { int32_t x, y, vx, vy; uint32_t t; } shot[8];   /* their shots (Q8 world px, Q8 px / s) */
    uint8_t boost_on, pulse_on, cruise;                 /* cruise: OCT- clicked twice, the boost (then the pulse) kept on */
    uint32_t a_t0;                                      /* A held since (the pulse jump after 1.2 s) */
    const char *say_t, *say_s;                          /* the text in the corner: title, line under it */
    uint32_t say_t0;
    uint8_t dirty, slot;                                /* the save: to write, the next free record */
    uint32_t seq, t_save;
    int32_t svx, svy;                                   /* where the ship was saved (world px) */
    uint32_t got[G_NGOT];
    uint8_t got_n;
    struct { int32_t x, y; uint32_t t; } part[G_NPART];
    uint8_t part_n;
} g;
static void g_tune_stop(void);                          /* (the opening's music: g_tune_tick) */
static int32_t g_spd(void) { return G_CLASS[g.cls].speed + g.eng * 2; }   /* the speed: the ship's + its engine's */
static int32_t g_sector(int32_t v) { return (v >= 0 ? v : v - G_SECT + 1) / G_SECT; }   /* world px -> sector */
static int32_t g_cargo(void) { return g.res[0] + g.res[1] + g.res[2] + g.res[3]; }

static int32_t gz = 3, gw = 80, gh = 80;                /* the logical pixel and the view, in logical px */
static int32_t gcx, gcy;                                /* the camera: world px at the view's centre */
static uint8_t rc_space;                                /* (the user) the race is out in space (a gate by a station) */
static int32_t gcr = 32767, gsr = 0;                    /* (the user) the camera's turn, cos / sin Q15 (0: north up) */
static void g_rotv(int32_t *dx, int32_t *dy)            /* a vector (world px) turned with the view */
{
    int32_t x = *dx, y = *dy;
    if (!gsr && gcr == 32767)
        return;
    if (x < -4000 || x > 4000 || y < -4000 || y > 4000) {   /* (far: just its direction, kept far) */
        x /= 16, y /= 16;
        *dx = ((x * gcr - y * gsr) >> 15) * 16, *dy = ((x * gsr + y * gcr) >> 15) * 16;
        return;
    }
    *dx = (x * gcr - y * gsr) >> 15, *dy = (x * gsr + y * gcr) >> 15;
}
static void g_w2s(int32_t wx, int32_t wy, int32_t *x, int32_t *y)   /* world px -> the view's px, turned */
{
    int32_t dx = wx - gcx, dy = wy - gcy;
    if (!gsr && gcr == 32767) {
        *x = dx + gw / 2, *y = dy + gh / 2;
        return;
    }
    if (dx < -4000 || dx > 4000 || dy < -4000 || dy > 4000) {
        *x = *y = -9999;
        return;
    }
    *x = ((dx * gcr - dy * gsr) >> 15) + gw / 2, *y = ((dx * gsr + dy * gcr) >> 15) + gh / 2;
}

/* a logical pixel (x, y in the view); faded in through ordered dither */
static void gp(int32_t x, int32_t y, uint16_t c)
{
    if ((uint32_t)x >= (uint32_t)gw || (uint32_t)y >= (uint32_t)gh)
        return;
    if (g.fade < 16u && BAYER[y & 3][x & 3] >= g.fade)
        return;
    cv_rect(x * gz, y * gz, gz, gz, c);
}
static void gline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, n = 400;
    while (n--) {
        int32_t e2 = 2 * err;
        gp(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

/* ------------------------------------------------------------ the galaxy --- */
/* a planet in space is drawn 3 x its own radius (easier to see), and everything in flight goes by that size: the
   reach, the pull, the markers; the landing then grows it from there and the surface is as ever */
#define G_PR(p) ((p).r * 3)
#define G_NPL 4u                                        /* planets a system at most (the save: 2 bits) */
/* a system's sun: in the middle of its sector (the first sector's off to a side: the ship starts in the middle); its
 * colour, the map's star colour */
static void g_sun_at(int32_t sx, int32_t sy, int32_t *x, int32_t *y)
{
    *x = sx * G_SECT + G_SECT / 2, *y = sy * G_SECT + G_SECT / 2;
    if (sx == 0 && sy == 0)
        *x = G_HOME - 150, *y = G_HOME + 130;
}
/* (the user: suns of every colour) a star's kind: its body, its dark spots, its glow */
static const uint16_t G_SUN[8][3] = {
    {RGB(0xF0, 0x78, 0x20), RGB(0xA8, 0x30, 0x10), RGB(0xF8, 0xE4, 0x50)},   /* orange */
    {RGB(0xF8, 0xD0, 0x30), RGB(0xD0, 0x70, 0x18), RGB(0xF8, 0xF4, 0xA8)},   /* yellow */
    {RGB(0xE8, 0x28, 0x30), RGB(0x80, 0x10, 0x20), RGB(0xF8, 0x90, 0x40)},   /* red */
    {RGB(0x38, 0x90, 0xF0), RGB(0x18, 0x30, 0x90), RGB(0xA8, 0xE8, 0xF8)},   /* blue */
    {RGB(0xA0, 0x50, 0xE0), RGB(0x40, 0x18, 0x70), RGB(0xE0, 0xB0, 0xF8)},   /* violet */
    {RGB(0x50, 0xD8, 0x60), RGB(0x18, 0x70, 0x30), RGB(0xD8, 0xF8, 0x80)},   /* green */
    {RGB(0xF0, 0x78, 0x20), RGB(0xA8, 0x30, 0x10), RGB(0xF8, 0xE4, 0x50)},
    {RGB(0xF8, 0xD0, 0x30), RGB(0xD0, 0x70, 0x18), RGB(0xF8, 0xF4, 0xA8)}};
static uint32_t g_star_kind(int32_t sx, int32_t sy) { return (g_hash(sx, sy, 0x57A5u) >> 9) & 7u; }
static uint16_t g_star_col(int32_t sx, int32_t sy) { return G_SUN[g_star_kind(sx, sy)][0]; }
/* a sun at view (x, y), radius r (view px): a churning surface (its body, its dark spots slowly moving), a bright rim,
 * then a wide glow of its light thinning out in dither; drawn through gp */
static int32_t g_vnoise(int32_t x, int32_t y, uint32_t seed, uint32_t sh, int32_t wrap);
static uint16_t g_mix565(uint16_t a, uint16_t b, int32_t t)   /* a to b, t 0..16 */
{
    int32_t r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16, gg = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    int32_t bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)((r << 11) | (gg << 5) | bl);
}
static void g_sun_draw(int32_t x, int32_t y, int32_t r, uint32_t kind, int32_t y0, int32_t y1)
{
    const uint16_t *k = G_SUN[kind & 7u];
    int32_t i, j, gr = r * 3, L = r * 4, D = r * 5 / 2, step;
    uint16_t ring[6];
    ring[0] = NW, ring[1] = g_mix565(NW, k[2], 8), ring[2] = k[2], ring[3] = k[0], ring[4] = g_mix565(k[0], EN, 7), ring[5] = g_mix565(k[1], EN, 10);
    for (j = -gr; j <= gr; j++) {
        if (y + j < y0 || y + j > y1)
            continue;
        for (i = -gr; i <= gr; i++) {
            int32_t q = g_isqrt(i * i + j * j);
            if (q > gr)
                continue;
            step = q <= r / 2 ? 0 : q <= r ? 1 : q <= r * 3 / 2 ? 2 : q <= r * 2 ? 3 : q <= r * 5 / 2 ? 4 : 5;
            gp(x + i, y + j, ring[step]);
        }
    }
    for (i = r; i <= L; i++) {                          /* the shine: a cross, longer; the diagonals, shorter */
        uint16_t c = i < L * 2 / 3 ? ring[1] : ring[2];
        if ((i & 1) || i < L / 2) {
            gp(x + i, y, c), gp(x - i, y, c);
            if (y + i >= y0 && y + i <= y1)
                gp(x, y + i, c);
            if (y - i >= y0 && y - i <= y1)
                gp(x, y - i, c);
        }
        if (i <= D && (i & 1)) {
            if (y + i >= y0 && y + i <= y1)
                gp(x + i, y + i, ring[2]), gp(x - i, y + i, ring[2]);
            if (y - i >= y0 && y - i <= y1)
                gp(x + i, y - i, ring[2]), gp(x - i, y - i, ring[2]);
        }
    }
}
static int g_race_here(const planet_t *p) { return (p->seed >> 13) % 4u == 0u; }   /* (the race) a quarter of the planets */
static int g_npc_here(const planet_t *p);
static uint32_t g_npc_bit(const planet_t *p);
static int g_npc_quest(int32_t sx, int32_t sy, uint32_t k);
static void g_npc_icon(int32_t x, int32_t y, int ask);
static int g_planet(int32_t sx, int32_t sy, uint32_t k, planet_t *p)   /* planet k of sector (sx, sy); 0 = none */
{
    uint32_t h = g_hash(sx, sy, 0x51u + k), n = (sx == 0 && sy == 0) ? 2u : (sx == G_CX && sy == G_CY) ? 0u : 2u + g_hash(sx, sy, 0x50u) % 3u;   /* (the user: more: 2..4) */
    if (k >= n)
        return 0;
    p->r = 7 + (int32_t)(h % 13u);
    p->x = sx * G_SECT + 70 + (int32_t)((h >> 8) % (G_SECT - 140));
    p->y = sy * G_SECT + 70 + (int32_t)((h >> 17) % (G_SECT - 140));
    if (sx == 0 && sy == 0 && k == 0)                    /* the first one, in sight at the start */
        p->x = G_HOME + 80, p->y = G_HOME - 50, p->r = 14;   /* (its edge 52 px off: in sight, not pulling) */
    if (sx == 0 && sy == 0 && k == 1)                    /* (the second: never on top of it) */
        p->y = p->y < G_HOME ? p->y - 120 : p->y + 120;
    {   /* (the user) round its system's sun, apart (its own ring each), never on it; still: no orbiting */
        int32_t cx, cy, dx, dy, rr;
        g_sun_at(sx, sy, &cx, &cy);
        dx = p->x - cx, dy = p->y - cy, rr = g_isqrt(dx * dx + dy * dy);
        if (sx || sy) {                                 /* (each its own orbit, apart: 64, 114, 164, 214 px and a little) */
            uint32_t a0 = (h >> 8) & 1023u;
            rr = 90 + (int32_t)k * 78 + (int32_t)((h >> 3) % 16u);   /* (the user: further apart) */
            dx = (g_cos(a0) * rr) >> 15, dy = (g_sin(a0) * rr) >> 15;
        }
        if (rr < 60)
            dx = rr ? dx * 60 / rr : 60, dy = rr ? dy * 60 / rr : 0, rr = 60;
        p->x = cx + dx, p->y = cy + dy;
    }
    p->seed = h | 1u;
    {   /* (the user: more colours) seas: blue, purple, deep blue, green, red, orange (deep blue below); the land any
         * colour but the sea's; the hills a third one; a ring in a colour of its own */
        static const uint8_t SEA[8] = {0, 0, 2, 7, 1, 5, 3, 2};
        uint32_t h2 = g_hash(sx, sy, 0x7C01u + k);
        p->water = SEA[(h >> 4) & 7u];
        p->land = (uint8_t)((h >> 9) & 7u);
        if (p->land == p->water || (p->water == 7u && p->land == 0u))
            p->land = (uint8_t)((p->land + 3u) & 7u);
        p->land2 = (uint8_t)((h2 >> 3) & 7u);
        while (p->land2 == p->land || p->land2 == p->water)
            p->land2 = (uint8_t)((p->land2 + 1u) & 7u);
        if (sx == 0 && sy == 0 && k == 0)                /* (the first: blue seas, green land, bright green hills) */
            p->water = 0, p->land = 1, p->land2 = 4;
    }
    p->ring = (uint8_t)((h >> 6) % 3u == 0u ? 1u + ((h >> 20) & 7u) : 0u);
    return 1;
}

/* a globe: the planet's noise wrapped round it (a horizontal squeeze toward the rim), turning; lit from the upper
 * left, the night side through ordered dither; a rim of atmosphere on the lit side. Over it its clouds (our own layer):
 * puffs on a sphere a little bigger than the planet (they bulge out past its edge), turning faster than the ground,
 * cream, their upper left edge lit white, their lower right edge in shade, and their shadow on the ground below them;
 * how cloudy is the planet's own */
static uint8_t g_cmesh;
#define G_CLOUD RGB(0xF4, 0xEC, 0xC4)
#define G_CLOUD_SH RGB(0xA8, 0xA2, 0x8C)
static int32_t g_cloud(const planet_t *p, uint32_t rot, int32_t dx, int32_t dy, int32_t rc)   /* the cloud's density */
{
    int32_t w = g_isqrt(rc * rc - dy * dy), u = (int32_t)(rot * 2u) + dx * 40 / (w + 1), v = dy * 40 / (rc + 1) + 300;
    return (g_vnoise(u, v, p->seed ^ 0xC10Du, 3, 32) + g_vnoise(u / 2 + 9, v / 2, p->seed ^ 0xC20Du, 3, 16)) >> 1;   /* (puffs, in clusters) */
}
static void g_globe(int32_t cx, int32_t cy, int32_t r, const planet_t *p, uint32_t rot, int32_t y0, int32_t y1)
{
    int32_t dx, dy, rc = r + r / 16 + 2, ct = 152 + (int32_t)((p->seed >> 11) & 23u);
    uint16_t wc = GCOL[p->water & 7u], lc = GCOL[p->land & 7u];
    for (dy = -rc; dy <= rc; dy++) {
        int32_t w, wcl, y = cy + dy;
        if (y < y0 || y > y1)
            continue;
        w = dy >= -r && dy <= r ? g_isqrt(r * r - dy * dy) : -1;
        wcl = g_isqrt(rc * rc - dy * dy);
        for (dx = -wcl; dx <= wcl; dx++) {
            int32_t u, v, hgt, lit, x = cx + dx, cd, cs, in = w >= 0 && dx >= -w && dx <= w;
            uint16_t c = 0;
            if ((uint32_t)x >= (uint32_t)gw)
                continue;
            cd = g_cloud(p, rot, dx, dy, rc);
            if (cd > ct && !(g_cmesh && ((x + y) & 1))) {   /* a cloud (the system view: a mesh): a thin lit edge toward the light (up left), a shaded
                                                         * band away from it (down right), the body cream */
                if (g_cloud(p, rot, dx - 1, dy - 1, rc) <= ct)
                    c = NW;
                else if (g_cloud(p, rot, dx + 2, dy + 2, rc) <= ct)
                    c = G_CLOUD_SH;
                else
                    c = G_CLOUD;
                lit = 12 - ((dx + dy) * 12 / (r + 1));  /* (the night: the clouds go to their shade, then dark) */
                if (lit < 16 && BAYER[y & 3][x & 3] >= (lit + 6 < 0 ? 0 : lit + 6))
                    c = lit < -6 ? NK : G_CLOUD_SH;
                gp(x, y, c);
                continue;
            } else if (!in) {                           /* off the planet: only its clouds, and the rim of air */
                if (w >= 0 && (dx == -w - 1 || dx == w + 1) && dx < 0 && dy < r / 2)
                    gp(x, y, wc);
                continue;
            } else {
                u = (int32_t)rot + dx * 48 / (w + 1);
                v = (dy * 48) / (r + 1) + 128;
                hgt = (g_vnoise(u, v, p->seed, 5, 256 >> 5) * 3 + g_vnoise(u * 2, v * 2, p->seed ^ 7u, 5, 512 >> 5)) >> 2;
                c = hgt < 88 ? EN : hgt < 120 ? wc : hgt < 162 ? lc : hgt < 196 ? GCOL[p->land2 & 7u] : NW;   /* (the deep
                                                         * sea: deep blue; the hills their own colour) */
                cs = g_cloud(p, rot, dx - 3, dy - 3, rc);   /* the shadow a cloud up and left of it casts */
                if (cs > ct && BAYER[y & 3][x & 3] < (cs > ct + 8 ? 10 : 6))
                    c = NK;
            }
            lit = 12 - ((dx + dy) * 12 / (r + 1));      /* light from the upper left: >= 16 full .. <= 0 dark */
            if (lit < 16 && BAYER[y & 3][x & 3] >= (lit < 0 ? 0 : lit))
                c = NK;                                 /* (the night: black, so the nebula never shows through) */
            gp(x, y, c);
        }
    }
}

/* its ring: a band of rocks round it, each its own size (1 or 2 px), shade (the ring's colour, a lighter one, a deep
 * one) and place across the band; they go round, the inner ones faster. front: the half before the planet, else the
 * half behind it (hidden where the planet is) */
static void g_ring(int32_t cx, int32_t cy, int32_t r, int front, uint16_t c, uint32_t seed)
{
    uint32_t i;
    int32_t fl = 3 + (int32_t)((seed >> 5) % 7u);         /* (each ring its own tilt: 3 open .. 9 nearly edge on) */
    for (i = 0; i < 160u; i++) {
        uint32_t h = g_hash((int32_t)i, (int32_t)seed, 0xA57Eu), a = (h + fm1_ms / (24u + (h >> 9) % 24u)) & 1023u;
        int32_t rr = r * 16 / 10 + (int32_t)((h >> 12) % (uint32_t)(r * 7 / 10 + 1)), sz = (h >> 20) & 3u ? 2 : 1;
        int32_t x = cx + ((g_cos(a) * rr) >> 15), y = cy + ((g_sin(a) * (rr / fl + 1)) >> 15) + (int32_t)((h >> 24) % 3u) - 1;
        uint16_t k = (h >> 22) % 3u == 0u ? NW : (h >> 22) % 3u == 1u ? c : EN;
        if ((y >= cy) != (front != 0))
            continue;
        if (!front && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
            continue;                                   /* behind the planet */
        gp(x, y, k);
        if (sz > 1)
            gp(x + 1, y, k), gp(x, y + 1, k == NW ? c : k), gp(x + 1, y + 1, k == EN ? c : k);
    }
}

static void g_planet_draw(const planet_t *p, int32_t sxv, int32_t syv, int32_t r, int32_t y0, int32_t y1)
{
    uint32_t rot = fm1_ms / 70u + (p->seed & 255u);   /* (the user: slower, a third of before) */
    if (p->ring)
        g_ring(sxv, syv, r, 0, GCOL[(p->ring - 1u) & 7u], p->seed);
    g_globe(sxv, syv, r, p, rot, y0, y1);
    if (p->ring)
        g_ring(sxv, syv, r, 1, GCOL[(p->ring - 1u) & 7u], p->seed);
}

/* a station in sector (sx, sy)? (a fifth of them, and the first sector's, in sight at the start); *x, *y its place */
static int g_station(int32_t sx, int32_t sy, int32_t *x, int32_t *y)
{
    uint32_t h = g_hash(sx, sy, 0x57u);
    if (sx == 0 && sy == 0) {
        *x = G_HOME - 46, *y = G_HOME + 34;
        return 1;
    }
    if (h % 5u || (sx == G_CX && sy == G_CY))
        return 0;
    *x = sx * G_SECT + 60 + (int32_t)((h >> 8) % (G_SECT - 120));
    *y = sy * G_SECT + 60 + (int32_t)((h >> 17) % (G_SECT - 120));
    return 1;
}
/* a station's price for resource k (credits each): 2..8, its own */
static int32_t g_price(int32_t sx, int32_t sy, uint32_t k) { return 2 + (int32_t)((g_hash(sx, sy, 0x5717u) >> (k * 5u)) % 7u); }

/* a station at view (x, y): a ring that turns, four spokes, the hub, the dock's light blinking */
static void g_station_draw(int32_t x, int32_t y)       /* (the user) big, as if close: a turning ring on four spokes,
                                                         * panels out to the sides, the hub with its bay, lights blinking */
{
    uint32_t rot = fm1_ms / 30u, a;
    int32_t i, j;
    for (i = -34; i <= 34; i++)                         /* the solar panels: two wings, cells in blue, a white mast */
        if (i < -24 || i > 24)
            for (j = -5; j <= 5; j++)
                gp(x + i, y + j, j == 0 || (i & 3) == 0 ? NW : (((i + j) & 1) ? NB : EN));
    gline(x - 24, y, x - 6, y, NW), gline(x + 6, y, x + 24, y, NW);
    for (a = 0; a < 1024u; a += 6u) {                   /* the ring: two lines, its windows lit along it */
        int32_t cx = (g_cos(a + rot) * 20) >> 15, cy = (g_sin(a + rot) * 20) >> 15;
        gp(x + cx, y + cy, NW);
        gp(x + ((g_cos(a + rot) * 17) >> 15), y + ((g_sin(a + rot) * 17) >> 15), (a / 6u) % 5u == 0u ? EY : RGB(0x60, 0x68, 0x80));
    }
    for (a = 0; a < 4u; a++)                            /* the spokes */
        gline(x + ((g_cos(rot + a * 256u) * 6) >> 15), y + ((g_sin(rot + a * 256u) * 6) >> 15),
              x + ((g_cos(rot + a * 256u) * 17) >> 15), y + ((g_sin(rot + a * 256u) * 17) >> 15), NB);
    for (j = -5; j <= 5; j++)                           /* the hub: a block, its bay dark with a light either side */
        for (i = -5; i <= 5; i++)
            gp(x + i, y + j, (i == -5 || i == 5 || j == -5 || j == 5) ? NW : (j > -2 && j < 2 && i > -3 && i < 3) ? NK : NP);
    if ((fm1_ms / 300u) & 1u)
        gp(x - 3, y, EG), gp(x + 3, y, EG);
    if ((fm1_ms / 500u) & 1u)                           /* the beacons: the panels' tips */
        gp(x - 34, y - 5, ER), gp(x + 34, y - 5, ER);
    if ((fm1_ms / 400u) & 1u)
        gp(x + ((g_cos(rot + 128u) * 21) >> 15), y + ((g_sin(rot + 128u) * 21) >> 15), NO);
}

/* (the user) a race gate: by one station in two, 70 px east of it; a chequered hoop, lights running round it */
static int g_station(int32_t a, int32_t b, int32_t *x, int32_t *y);
static int g_gate_at(int32_t a, int32_t b, int32_t *x, int32_t *y)
{
    if (!g_station(a, b, x, y) || (g_hash(a, b, 0x6A7Eu) & 1u))
        return 0;
    *x += 70;
    return 1;
}
static void g_gate_draw(int32_t x, int32_t y)
{
    uint32_t a, run = fm1_ms / 20u;
    for (a = 0; a < 1024u; a += 8u) {
        int32_t px = x + ((g_cos(a) * 10) >> 15), py = y + ((g_sin(a) * 15) >> 15);
        gp(px, py, ((a / 64u) & 1u) ? NW : NK), gp(px + 1, py, ((a / 64u) & 1u) ? NK : NW);
        if (((a + run) & 127u) < 8u)
            gp(x + ((g_cos(a) * 8) >> 15), y + ((g_sin(a) * 13) >> 15), NO);
    }
    if ((fm1_ms / 400u) & 1u)
        gp(x, y - 18, EG);
}
/* a station's icon (the map, the radar, the edge of the screen): a yellow diamond ring, its four arms, a white hub;
 * pset: the map's pixel or the game's */
static void g_st_icon(int32_t x, int32_t y, void (*pset)(int32_t, int32_t, uint16_t))
{
    int32_t i, j;
    for (j = -4; j <= 4; j++)
        for (i = -4; i <= 4; i++) {
            int32_t m = (i < 0 ? -i : i) + (j < 0 ? -j : j);
            if (m == 3)
                pset(x + i, y + j, EY);
            else if ((m == 4 && (!i || !j)) || !m)
                pset(x + i, y + j, NW);
        }
}

/* the centre of the galaxy: a white core in dither, rings of blue and purple, rays that turn */
static void g_core(int32_t x, int32_t y)
{
    int32_t i, j, r = 18;
    uint32_t a, rot = fm1_ms / 16u;
    for (j = -r - 8; j <= r + 8; j++)
        for (i = -r - 8; i <= r + 8; i++) {
            int32_t d = i * i + j * j, lv;
            if (d > (r + 8) * (r + 8))
                continue;
            lv = 16 - d * 16 / ((r + 8) * (r + 8));      /* bright in the middle */
            if (BAYER[(y + j) & 3][(x + i) & 3] >= lv)
                continue;
            gp(x + i, y + j, d < r * r / 4 ? NW : d < r * r ? NB : NP);
        }
    for (a = 0; a < 8u; a++)
        gline(x + ((g_cos(rot + a * 128u) * (r + 2)) >> 15), y + ((g_sin(rot + a * 128u) * (r + 2)) >> 15),
              x + ((g_cos(rot + a * 128u) * (r + 14)) >> 15), y + ((g_sin(rot + a * 128u) * (r + 14)) >> 15), a & 1u ? NO : NW);
}

/* ------------------------------------------------------------- particles --- */
static void g_puff(int32_t x, int32_t y)                /* world px Q8 */
{
    uint32_t i = g.part_n++ % G_NPART;
    g.part[i].x = x, g.part[i].y = y, g.part[i].t = fm1_ms;
}
static void g_particles(void)
{
    uint32_t i;
    for (i = 0; i < G_NPART; i++) {
        uint32_t age = fm1_ms - g.part[i].t;
        if (!g.part[i].t || age > 900u)
            continue;
        int32_t px_, py_;
        g_w2s(g.part[i].x >> 8, g.part[i].y >> 8, &px_, &py_);
        gp(px_, py_,
           age < 120u ? NW : age < 300u ? EY : age < 560u ? ER : NP);
    }
}

/* the ship at view (x, y), heading a, a flame when it thrusts */
/* (the user) the ship against its sun: dark, its rim lit in the sun's light (g_lit: the rim's colour, 0 none) */
static uint16_t g_lit;
static void g_ship(int32_t x, int32_t y, uint32_t a, int flame, int32_t s)
{
    int32_t nx = x + ((g_cos(a) * 4 * s) >> 15), ny = y + ((g_sin(a) * 4 * s) >> 15);
    int32_t lx = x + ((g_cos(a + 370u) * 3 * s) >> 15), ly = y + ((g_sin(a + 370u) * 3 * s) >> 15);
    int32_t rx = x + ((g_cos(a - 370u) * 3 * s) >> 15), ry = y + ((g_sin(a - 370u) * 3 * s) >> 15);
    int32_t bx = x + ((g_cos(a + 512u) * 1 * s) >> 15), by = y + ((g_sin(a + 512u) * 1 * s) >> 15);
    int32_t i, j, x0 = x - 5 * s, y0 = y - 5 * s;
    if (flame) {                                        /* the flame behind, flickering */
        int32_t fl = 2 + (int32_t)((fm1_ms / 60u) & 1u) + (flame > 1 ? 2 : 0);
        gline(bx, by, x + ((g_cos(a + 512u) * (2 + fl) * s) >> 15), y + ((g_sin(a + 512u) * (2 + fl) * s) >> 15), flame > 1 ? EY : NO);
    }
    for (j = y0; j <= y + 5 * s; j++)                   /* the hull: two triangles, nose-wing-tail */
        for (i = x0; i <= x + 5 * s; i++) {
            int32_t e1 = (lx - nx) * (j - ny) - (ly - ny) * (i - nx), e2 = (bx - lx) * (j - ly) - (by - ly) * (i - lx);
            int32_t e3 = (nx - bx) * (j - by) - (ny - by) * (i - bx);
            int32_t f1 = (bx - nx) * (j - ny) - (by - ny) * (i - nx), f2 = (rx - bx) * (j - by) - (ry - by) * (i - bx);
            int32_t f3 = (nx - rx) * (j - ry) - (ny - ry) * (i - rx);
            if (((e1 >= 0 && e2 >= 0 && e3 >= 0) || (e1 <= 0 && e2 <= 0 && e3 <= 0)) ||
                ((f1 >= 0 && f2 >= 0 && f3 >= 0) || (f1 <= 0 && f2 <= 0 && f3 <= 0)))
                gp(i, j, g_lit == NW ? NW : g_lit ? NK : NW);
        }
    if (!g_lit)
        gp(x + ((g_cos(a) * 1 * s) >> 15), y + ((g_sin(a) * 1 * s) >> 15), NB);   /* the cockpit */
}

static const uint16_t G_HULL[4][11] = {   /* (the user) the ships for sale, classes 2..5, 11 x 11 each */
    {0x050, 0x0F8, 0x0D8, 0x0D8, 0x1FC, 0x5DD, 0x7FF, 0x6FB, 0x4D9, 0x088, 0x104},
    {0x020, 0x070, 0x050, 0x070, 0x653, 0x6FB, 0x7FF, 0x6FB, 0x673, 0x222, 0x000},
    {0x020, 0x070, 0x0D8, 0x18C, 0x1FC, 0x3FE, 0x6DB, 0x421, 0x020, 0x050, 0x000},
    {0x070, 0x0F8, 0x0D8, 0x4F9, 0x5FD, 0x777, 0x5FD, 0x4D9, 0x0F8, 0x050, 0x020}};
static const uint16_t G_HULL0[11] = {0x020, 0x020, 0x070, 0x070, 0x0F8, 0x0D8, 0x1FC, 0x3FE, 0x7FF, 0x707, 0x401};   /* the first: the triangle */
static void g_hull(int32_t cx, int32_t cy, uint32_t cls, int32_t k, uint16_t c)   /* class cls's ship, k px a cell, nose up */
{
    const uint16_t *hl = cls ? G_HULL[(cls - 1u) & 3u] : G_HULL0;
    int32_t i, j;
    for (j = 0; j < 11; j++)
        for (i = 0; i < 11; i++)
            if ((hl[j] >> (10 - i)) & 1u)
                cv_rect(cx - 11 * k / 2 + i * k, cy - 11 * k / 2 + j * k, k, k, j < 2 ? NW : c);
}
static void g_hull_at(int32_t x, int32_t y, uint32_t cls, int32_t c, int32_t sn, uint16_t col)   /* turned to (c, sn) */
{
    int32_t i, j, q;
    for (j = 0; j < 11; j++)
        for (i = 0; i < 11; i++)
            if ((G_HULL[(cls - 1u) & 3u][j] >> (10 - i)) & 1u)
                for (q = 0; q < 4; q++) {
                    int32_t u = 10 - 2 * j + (q & 1), v = 2 * i - 10 + (q >> 1);
                    gp(x + ((u * c - v * sn) >> 16), y + ((u * sn + v * c) >> 16), j < 2 ? NW : j > 8 ? EN : col);
                }
}
/* --------------------------------------------------------------- space --- */
/* the deep: clouds of deep blue (a slow noise, 1/8 of the camera's move), dithered, a little purple in their cores */
#include "nms_nebulosa.h"
#define G_NB_PX 6                                       /* the nebula: screen px a pixel of it (96 x 124 -> 576 x 744) */
static void g_nebula(int32_t y0, int32_t y1)
{
    int32_t x, y, ox = (gcx >> 3) - gw / 2, oy = (gcy >> 3) - gh / 2;
    if (y0 < 0)
        y0 = 0;
    if (y1 > gh - 1)
        y1 = gh - 1;
#if NMS_NEBULOSA
    /* the picture (gen_nebulosa.py), giant, far behind everything: it moves 1/32 of the ship's way, mirrored at its
       edges (no seam however far you fly); in 2 x 2 screen cells whatever the zoom, each tone through Bayer at its own
       solidity so the stars show through */
    {
        static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
        static int16_t cu[120];
        const int32_t PW = NMS_NB_W * G_NB_PX, PH = NMS_NB_H * G_NB_PX;
        int32_t sx0 = ((gcx * gz) >> 5) - gw * gz / 2 + PW / 2, sy0 = ((gcy * gz) >> 5) - gh * gz / 2 + PH / 3;
        int32_t W = gw * gz, Y0 = y0 * gz, Y1 = (y1 + 1) * gz;
        (void)ox, (void)oy;
        for (x = 0; x < W; x += 2) {                    /* (the column of the picture under each cell, once a call) */
            int32_t u = (sx0 + x) % (2 * PW);
            if (u < 0)
                u += 2 * PW;
            u /= G_NB_PX;
            cu[x >> 1] = (int16_t)(u < NMS_NB_W ? u : 2 * NMS_NB_W - 1 - u);
        }
        for (y = Y0 & ~1; y < Y1; y += 2) {
            int32_t v = (sy0 + y) % (2 * PH), c = (y >> 1) & 3;
            const uint8_t *row;
            if (v < 0)
                v += 2 * PH;
            v /= G_NB_PX;
            v = v < NMS_NB_H ? v : 2 * NMS_NB_H - 1 - v;
            row = NMS_NB + v * (NMS_NB_W / 2);
            for (x = 0; x < W; x += 2) {
                uint32_t u = (uint32_t)cu[x >> 1], t = (row[u >> 1] >> ((u & 1u) ? 0 : 4)) & 15u;
                uint32_t bay = BAYER[c][(x >> 1) & 3];
                if (bay < NMS_NB_A[t] && (g.fade >= 16u || bay < g.fade))
                    cv_rect(x, y, 2, 2, PAL[NMS_NB_COL[t]]);
            }
        }
        return;
    }
#endif
    for (y = y0 & ~1; y <= y1; y += 2)                  /* (2 x 2 blocks: the noise is slow) */
        for (x = 0; x < gw; x += 2) {
            int32_t n = g_vnoise((ox + x) * 4, (oy + y) * 4, 0xC10Du, 7, 0);
            if (n < 188)
                continue;
            if (n > 232)
                gp(x, y, NP), gp(x + 1, y + 1, EN);
            else if (n > 212)
                gp(x, y, EN), gp(x + 1, y + 1, EN);
            else if (((x + y) & 6) == 0)
                gp(x, y, EN);
        }
}

static void g_stars(int32_t y0, int32_t y1)
{
    static const int32_t DEPTH[3] = {40, 90, 180};      /* Q8 of the camera's move: far, middle, near */
    static const uint16_t COL[3][6] = {{NP, NB, EN, NP, NB, NO}, {NB, NG, NP, NO, EG, NB}, {NW, EY, ER, EG, NB, NW}};
    uint32_t l, i;
    int32_t speed = (g.vx > 0 ? g.vx : -g.vx) + (g.vy > 0 ? g.vy : -g.vy);
    for (l = 0; l < 3u; l++)
        for (i = 0; i < 26u; i++) {
            uint32_t h = g_hash((int32_t)i, (int32_t)l, 0xA5u);
            int32_t x = (int32_t)(h % 1024u) - ((gcx * DEPTH[l]) >> 8), y = (int32_t)((h >> 10) % 1024u) - ((gcy * DEPTH[l]) >> 8);
            uint16_t c = COL[l][(h >> 20) % 6u];      /* (each star its colour) */
            if (gsr || gcr != 32767) {                  /* (turned: a field half as big again round the middle, turned) */
                int32_t pw = gw * 3 / 2, ph = gh * 3 / 2;
                x = ((x % pw) + pw) % pw - pw / 2, y = ((y % ph) + ph) % ph - ph / 2;
                g_rotv(&x, &y);
                x += gw / 2, y += gh / 2;
            } else {
                x = ((x % gw) + gw) % gw;
                y = ((y % gh) + gh) % gh;
            }
            if (y < y0 - 8 || y > y1 + 8)
                continue;
            if (l >= 1u && speed > 30 * 256) {          /* fast: the stars streak behind, each a line in its colour */
                int32_t k = speed / ((l == 2u ? 12 : 24) * 256), svx = g.vx >> 8, svy = g.vy >> 8;
                if (k > gw / 2)
                    k = gw / 2;
                g_rotv(&svx, &svy);
                gline(x, y, x - svx * 256 * k / (speed + 1), y - svy * 256 * k / (speed + 1), c);
            } else {
                gp(x, y, c);
            }
        }
}

/* the planets near the camera (3 x 3 sectors), the stations, the core; the planet in reach (to land) back in
 * *near, a station in reach in *st (1) */
/* the planets near but out of sight (within 500 px): a little planet of one colour (its land's) at the edge of the
 * screen, toward each. It grows as you get nearer, and in the last 30 px before the planet comes in it slides out to the
 * planet's own place and size, so that it is the big one coming in */
/* the 3 nearest planets out of sight (sectors round you), always: a small disc of its colour at the edge, in its
   direction, bigger nearer, and its distance in light-years by it (100 px a light-year; g_hud writes it). In sight,
   the marker goes */
static struct { int16_t x, y, d; uint16_t c; } g_mk[3];
#define G_MK_TOP (26 / gz + 2)                          /* (the markers keep off the BPM line and the cargo line) */
#define G_MK_BOT (30 / gz + 2)
static uint8_t g_mk_n;
static struct { int16_t x, y, d; } g_stm, g_sunm;    /* the nearest station's edge mark, the system's sun's (d 0: none) */
/* where the line from the middle to (dx, dy) leaves the view, inset m */
static void g_edge(int32_t dx, int32_t dy, int32_t m, int16_t *x, int16_t *y)
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, hw = gw / 2 - m, hh = gh / 2 - m;
    if (ax * hh >= ay * hw)
        *x = (int16_t)(gw / 2 + (dx < 0 ? -hw : hw)), *y = (int16_t)(gh / 2 + dy * hw / (ax ? ax : 1));
    else
        *x = (int16_t)(gw / 2 + dx * hh / (ay ? ay : 1)), *y = (int16_t)(gh / 2 + (dy < 0 ? -hh : hh));
    *y = (int16_t)clamp(*y, G_MK_TOP + 4, gh - 5 - G_MK_BOT);
}
static uint32_t g_mk_frame;
static void g_markers(void)
{
    int32_t a, b, i, sx = g_sector(gcx), sy = g_sector(gcy);
    if (g_mk_frame != g.frame + 1u) {                   /* (once a frame: the halves share it) */
        g_mk_frame = g.frame + 1u, g_mk_n = 0;
        for (a = sx - 2; a <= sx + 2; a++)
            for (b = sy - 2; b <= sy + 2; b++) {
                planet_t p;
                uint32_t k;
                for (k = 0; k < G_NPL && g_planet(a, b, k, &p); k++) {
                    int32_t vx, vy, dx = p.x - gcx, dy = p.y - gcy, d, in = 4, n;
                    g_rotv(&dx, &dy), vx = dx + gw / 2, vy = dy + gh / 2;
                    if (vx + G_PR(p) >= 0 && vx - G_PR(p) < gw && vy + G_PR(p) >= 0 && vy - G_PR(p) < gh)
                        continue;                       /* (in sight: the planet itself) */
                    d = g_isqrt(dx * dx + dy * dy);
                    if (d > 32000)
                        continue;
                    for (n = g_mk_n < 3u ? g_mk_n : 3; n > 0 && g_mk[n - 1].d > d; n--)
                        if (n < 3)
                            g_mk[n] = g_mk[n - 1];     /* (kept sorted, nearest first) */
                    if (n >= 3)
                        continue;
                    if (g_mk_n < 3u)
                        g_mk_n++;
                    {   /* where the line to it leaves the view (inset) */
                        int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, hw = gw / 2 - in, hh = gh / 2 - in;
                        if (ax * hh >= ay * hw)
                            g_mk[n].x = (int16_t)(gw / 2 + (dx < 0 ? -hw : hw)), g_mk[n].y = (int16_t)(gh / 2 + dy * hw / (ax ? ax : 1));
                        else
                            g_mk[n].x = (int16_t)(gw / 2 + dx * hh / (ay ? ay : 1)), g_mk[n].y = (int16_t)(gh / 2 + (dy < 0 ? -hh : hh));
                    }
                    g_mk[n].y = (int16_t)clamp(g_mk[n].y, G_MK_TOP, gh - 1 - G_MK_BOT);   /* (clear of the head) */
                    g_mk[n].d = (int16_t)d, g_mk[n].c = GCOL[p.land & 7u];
                }
            }
    }
    if (g.mode == GM_SPACE) {                           /* (the user) the nearest station, always: its icon at the edge */
        int32_t bd = 1 << 30, bx = 0, by = 0, qx, qy;
        g_stm.d = 0;
        for (a = sx - 6; a <= sx + 6; a++)
            for (b = sy - 6; b <= sy + 6; b++)
                if (g_station(a, b, &qx, &qy)) {
                    int32_t dd = (qx - gcx) * (qx - gcx) + (qy - gcy) * (qy - gcy);
                    if (dd < bd)
                        bd = dd, bx = qx - gcx, by = qy - gcy, g_rotv(&bx, &by);
                }
        if (bd < (1 << 30) && (bx + gw / 2 < 0 || bx + gw / 2 >= gw || by + gh / 2 < 0 || by + gh / 2 >= gh)) {
            g_edge(bx, by, 6, &g_stm.x, &g_stm.y);
            g_stm.d = (int16_t)clamp(g_isqrt(bd), 1, 32000);
            g_st_icon(g_stm.x, g_stm.y, gp);
        }
        {   /* the sun of the system you are in, out of sight: a little sun of its colour at the edge, rays round it */
            int32_t ux, uy, i2, j2;
            uint16_t sc = g_star_col(sx, sy);
            g_sunm.d = 0;
            g_sun_at(sx, sy, &ux, &uy);
            ux -= gcx, uy -= gcy;
            g_rotv(&ux, &uy);
            if ((ux + gw / 2 < -8 || ux + gw / 2 >= gw + 8 || uy + gh / 2 < -8 || uy + gh / 2 >= gh + 8) && !(sx == G_CX && sy == G_CY)) {
                g_edge(ux, uy, 6, &g_sunm.x, &g_sunm.y);
                g_sunm.d = (int16_t)clamp(g_isqrt(ux * ux + uy * uy), 1, 32000);
                for (j2 = -4; j2 <= 4; j2++)
                    for (i2 = -4; i2 <= 4; i2++) {
                        int32_t q = i2 * i2 + j2 * j2;
                        if (q <= 4)
                            gp(g_sunm.x + i2, g_sunm.y + j2, q ? sc : NW);
                        else if (q == 16 || (q > 8 && q <= 10 && (!i2 || !j2 || i2 == j2 || i2 == -j2)))
                            gp(g_sunm.x + i2, g_sunm.y + j2, sc);
                    }
            }
        }
    }
    for (i = 0; i < g_mk_n; i++) {
        int32_t mr = 1 + (g_mk[i].d < 400) + (g_mk[i].d < 160), dx, dy;
        for (dy = -mr - 1; dy <= mr + 1; dy++)
            for (dx = -mr - 1; dx <= mr + 1; dx++) {
                int32_t q = dx * dx + dy * dy;
                if (q <= mr * mr + mr)
                    gp(g_mk[i].x + dx, g_mk[i].y + dy, g_mk[i].c);
                else if (q <= (mr + 1) * (mr + 1) + mr + 1)
                    gp(g_mk[i].x + dx, g_mk[i].y + dy, NK);   /* (a black ring: it reads on the nebula) */
            }
    }
}
/* g_hud: the markers' distances, by each, white outlined (it reads on the nebula) */
static void g_mk_text(void)
{
    uint32_t i;
    if (g_mk_frame != g.frame + 1u)
        return;
    if (g_sunm.d && g.mode == GM_SPACE) {               /* the sun's: in its colour */
        char t[12];
        uint16_t sc = g_star_col(g_sector(gcx), g_sector(gcy));
        int32_t mx = g_sunm.x * gz + gz / 2, my = g_sunm.y * gz + gz / 2, w;
        fmt_int(t, g_sunm.d / 100), str_cpy(t + str_len(t), ".", 2), fmt_int(t + str_len(t), g_sunm.d / 10 % 10);
        str_cpy(t + str_len(t), " LY", 4);
        w = n_w(t, 1);
        n_texto(clamp(g_sunm.x <= 8 ? mx + 16 : g_sunm.x >= gw - 9 ? mx - 16 - w : mx - w / 2, 2, 238 - w),
                clamp(g_sunm.y <= G_MK_TOP + 4 ? my + 14 : my - 30, 26, 178), t, 1, sc == NP ? NW : sc);
    }
    if (g_stm.d && g.mode == GM_SPACE) {                /* the station's: yellow */
        char t[12];
        int32_t mx = g_stm.x * gz + gz / 2, my = g_stm.y * gz + gz / 2, w;
        fmt_int(t, g_stm.d / 100), str_cpy(t + str_len(t), ".", 2), fmt_int(t + str_len(t), g_stm.d / 10 % 10);
        str_cpy(t + str_len(t), " LY", 4);
        w = n_w(t, 1);
        n_texto(clamp(g_stm.x <= 8 ? mx + 16 : g_stm.x >= gw - 9 ? mx - 16 - w : mx - w / 2, 2, 238 - w),
                clamp(g_stm.y <= G_MK_TOP + 4 ? my + 14 : my - 30, 26, 178), t, 1, EY);
    }
    for (i = 0; i < g_mk_n; i++) {
        char t[12];
        int32_t d = g_mk[i].d, mx = g_mk[i].x * gz + gz / 2, my = g_mk[i].y * gz + gz / 2, w, lx, ly;
        fmt_int(t, d / 100);
        str_cpy(t + str_len(t), ".", 2);
        fmt_int(t + str_len(t), d / 10 % 10);
        str_cpy(t + str_len(t), " LY", 4);
        w = n_w(t, 1);
        lx = g_mk[i].x <= 5 ? mx + 8 : g_mk[i].x >= gw - 6 ? mx - 8 - w : mx - w / 2;
        ly = g_mk[i].y <= G_MK_TOP ? my + 8 : g_mk[i].y >= gh - 1 - G_MK_BOT ? my - 26 : my - 8;
        n_texto(clamp(lx, 2, 238 - w), clamp(ly, 26, 178), t, 1, NW);   /* (above the speed) */
    }
}

static void g_planets(int32_t y0, int32_t y1, planet_t *near, int *st)
{
    int32_t a, b, best = 1 << 30;
    int32_t sx = g_sector(gcx), sy = g_sector(gcy);
    near->r = 0;
    *st = 0;
    for (a = sx - 1; a <= sx + 1; a++)
        for (b = sy - 1; b <= sy + 1; b++) {
            uint32_t k;
            planet_t p;
            int32_t x, y;
            if (a == G_CX && b == G_CY) {
                int32_t qx, qy;
                g_w2s(a * G_SECT + G_SECT / 2, b * G_SECT + G_SECT / 2, &qx, &qy);
                g_core(qx, qy);
            } else {                                    /* its sun: its surface, its glow */
                int32_t ux, uy;
                g_sun_at(a, b, &ux, &uy);
                g_w2s(ux, uy, &ux, &uy);
                if (ux > -40 && ux < gw + 40 && uy > y0 - 40 && uy < y1 + 40)
                    g_sun_draw(ux, uy, 16, g_star_kind(a, b), y0, y1);
            }
            if (g_station(a, b, &x, &y)) {
                if ((x - gcx) * (x - gcx) + (y - gcy) * (y - gcy) < 26 * 26)
                    *st = 1;
                g_w2s(x, y, &x, &y);
                if (x > -40 && x < gw + 40 && y > y0 - 26 && y < y1 + 26)
                    g_station_draw(x, y);
            }
            if (g_gate_at(a, b, &x, &y)) {              /* (the user) its race gate */
                g_w2s(x, y, &x, &y);
                if (x > -20 && x < gw + 20 && y > y0 - 20 && y < y1 + 20)
                    g_gate_draw(x, y);
            }
            for (k = 0; k < G_NPL; k++) {
                int32_t vx, vy, d;
                if (!g_planet(a, b, k, &p))
                    break;
                g_w2s(p.x, p.y, &vx, &vy);
                d = (p.x - gcx) * (p.x - gcx) + (p.y - gcy) * (p.y - gcy);
                if (d < (G_PR(p) + 8) * (G_PR(p) + 8) && d < best)
                    best = d, *near = p;
                if (vx + G_PR(p) * 3 < 0 || vx - G_PR(p) * 3 > gw || vy + G_PR(p) < y0 - 2 || vy - G_PR(p) > y1 + 2)
                    continue;
                g_planet_draw(&p, vx, vy, G_PR(p), y0, y1);
                if (g_npc_here(&p) && !(g.q_done & g_npc_bit(&p)))   /* someone waiting: ! (? when you are on their quest) */
                    g_npc_icon(vx - G_PR(p) / 2 - 8, vy - G_PR(p) - 12, g_npc_quest(a, b, k));
                if (g_race_here(&p)) {                  /* (the user) a race here: a chequered flag over it, on a pole */
                    int32_t fx = vx + G_PR(p) / 2, fy = vy - G_PR(p) - 10, i2, j2;
                    for (j2 = 0; j2 < 12; j2++)
                        gp(fx, fy + j2, NW);
                    for (j2 = 0; j2 < 6; j2++)
                        for (i2 = 1; i2 <= 8; i2++)
                            gp(fx + i2, fy + j2, ((i2 / 2 + j2 / 2) & 1) ? NK : NW);
                }
            }
        }
}

/* the text in the corner: a title and a line under it, letter by letter (each purple, orange, then white), 2 s, then
 * away letter by letter, rising and coming apart in pixels */
#define G_SAY_LET 45u
static void g_say(const char *t, const char *sub)
{
    g.say_t = t, g.say_s = sub, g.say_t0 = G_STAMP;
}
static void g_say_draw(void)
{
    uint32_t nt, ns, n, i, el, out;
    if (!g.say_t0)
        return;
    nt = str_len(g.say_t), ns = str_len(g.say_s), n = nt + ns;
    el = fm1_ms - g.say_t0, out = n * G_SAY_LET + 2000u;
    if (el > out + n * 25u + 450u) {
        g.say_t0 = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        char c[2] = {i < nt ? g.say_t[i] : g.say_s[i - nt], 0};
        int32_t a = (int32_t)el - (int32_t)(i * G_SAY_LET), x = 4 + (int32_t)(i < nt ? i : i - nt) * 8, y = i < nt ? 26 : 44, al = 16;
        uint16_t col = a < 90 ? NP : a < 180 ? NO : NW;
        if (a < 0 || c[0] == ' ')
            continue;
        if (el > out + i * 25u) {                       /* away: up and dithered off */
            int32_t d = (int32_t)(el - out - i * 25u);
            y -= d / 18;
            al = 16 - d / 26;
            if (al <= 0)
                continue;
        }
        n_textd(x + 1, y + 1, c, 1, NK, al);
        n_textd(x, y, c, 1, col, al);
    }
}

/* after a take-off the sentinels that had you in sight come after you as little ships: as many as were near you, as fast
 * as you (up to the pulse jump's speed), circling round you and shooting pixels; a hit costs a unit of cargo. They give
 * up after 30 s, or when the pulse jump or a warp takes you away, or when you land */
static uint8_t sf_on;                                   /* (the fight in space: below) */
static uint8_t foe_look[G_NSEN];                        /* (the user) each of theirs: a hangar ship (1..4) | colour << 3 */
static uint32_t foe_hit_t[G_NSEN];                      /* .. hit (a flash) */
static const uint16_t FOE_COL[8] = {ER, NO, NP, EG, EY, RGB(0xF0, 0x60, 0xC0), RGB(0x60, 0xE0, 0xFF), RGB(0xC0, 0xA0, 0x60)};
static void sf_tick(uint32_t dt);
static void es_tick(uint32_t dt);
static void es_draw(void);
static void g_foes_start(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 8u; i++)
        g.shot[i].t = 0;                                /* (the planet's shots stay there) */
    for (i = 0; i < g.sen_n && n < G_NSEN; i++) {      /* all still flying, if they saw you, or you shot one, or hostile */
        if (g.sen_seen || g.angry || g.alert_t) {
            g.foe[n].x = g.sx + (g_cos(g.ang + 512u + n * 120u - 240u) >> 7) * 30, g.foe[n].y = g.sy + (g_sin(g.ang + 512u + n * 120u - 240u) >> 7) * 30;
            g.foe[n].vx = g.foe[n].vy = 0, g.foe[n].shot = fm1_ms + 1200u + n * 400u;
            n++;
        }
    }
    g.foe_n = (uint8_t)n, g.foe_t = fm1_ms;
    if (n)
        g_say("SENTINELS", "IN PURSUIT");
}
static void g_foes_tick(uint32_t dt)
{
    uint32_t i, far = 0;
    int32_t vs = g_isqrt((g.vx >> 8) * (g.vx >> 8) + (g.vy >> 8) * (g.vy >> 8));   /* px / s */
    int32_t cap = 10 * g_spd() * 3, sp = vs * 11 / 10;   /* (the boost's top: the pulse jump outruns them) */
    if (sp < 55)                                        /* (the user: they come to you, to be seen) */
        sp = 55;
    if (sp > cap)
        sp = cap;
    for (i = 0; i < g.foe_n; i++) {
        int32_t dx = (g.sx - g.foe[i].x) >> 8, dy = (g.sy - g.foe[i].y) >> 8, d = g_isqrt(dx * dx + dy * dy) + 1, tvx, tvy;
        uint32_t orb = fm1_ms / 4u + i * 341u;          /* (they circle round you, 22 px out) */
        int32_t ox = dx + ((g_cos(orb) * 22) >> 15), oy = dy + ((g_sin(orb) * 22) >> 15), od = g_isqrt(ox * ox + oy * oy) + 1;
        if (d > 260)
            far++;
        tvx = ox * sp * 256 / od + g.vx * (d < 40), tvy = oy * sp * 256 / od + g.vy * (d < 40);
        if (d < 40)
            tvx = tvx / 2, tvy = tvy / 2;
        g.foe[i].vx += (tvx - g.foe[i].vx) * (int32_t)dt / 200;
        g.foe[i].vy += (tvy - g.foe[i].vy) * (int32_t)dt / 200;
        g.foe[i].x += g.foe[i].vx * (int32_t)dt / 1000;
        g.foe[i].y += g.foe[i].vy * (int32_t)dt / 1000;
        if (d < 80 && (int32_t)(fm1_ms - g.foe[i].shot) >= 0) {   /* a shot, aimed a little ahead of you */
            uint32_t k;
            for (k = 0; k < 8u && g.shot[k].t; k++)
                ;
            if (k < 8u) {
                int32_t ax = dx + (g.vx >> 8) / 4, ay = dy + (g.vy >> 8) / 4, ad = g_isqrt(ax * ax + ay * ay) + 1;
                g.shot[k].x = g.foe[i].x, g.shot[k].y = g.foe[i].y, g.shot[k].t = G_STAMP;
                g.shot[k].vx = ax * 110 * 256 / ad + g.foe[i].vx, g.shot[k].vy = ay * 110 * 256 / ad + g.foe[i].vy;
            }
            g.foe[i].shot = fm1_ms + 800u + (g_hash((int32_t)i, (int32_t)(fm1_ms >> 8), 7u) % 600u);
        }
    }
    for (i = 0; i < 8u; i++) {                          /* the shots fly 1.4 s; one on you: a unit of cargo lost */
        int32_t dx, dy;
        if (!g.shot[i].t)
            continue;
        if (fm1_ms - g.shot[i].t > 1400u) {
            g.shot[i].t = 0;
            continue;
        }
        g.shot[i].x += g.shot[i].vx * (int32_t)dt / 1000, g.shot[i].y += g.shot[i].vy * (int32_t)dt / 1000;
        dx = (g.shot[i].x - g.sx) >> 8, dy = (g.shot[i].y - g.sy) >> 8;
        if (dx * dx + dy * dy <= 9) {
            uint32_t k, m = 0;
            g.shot[i].t = 0;
            if (fm1_ms - g.hit_t < 900u)
                continue;
            g.hit_t = fm1_ms;
            for (k = 1; k < 4u; k++)
                if (g.res[k] > g.res[m])
                    m = k;
            if (g.res[m]) {
                g.res[m]--;
                g_say("HIT", "CARGO LOST");
            } else {
                g_say("HIT", "NOTHING TO LOSE");
            }
        }
    }
    if (g.foe_n && (far == g.foe_n || (!sf_on && fm1_ms - g.foe_t > 30000u))) {
        g.foe_n = 0;
        g_say("ESCAPED", "THEY GAVE UP");
    }
}
static void g_foes_draw(void)
{
    uint32_t i;
    for (i = 0; i < g.foe_n; i++) {                    /* (the user) a hangar ship in its colour, its nose to you; white a
                                                         * moment when hit; off the view: a mark of its colour at the edge */
        int32_t x, y;
        int32_t ax = (g.sx - g.foe[i].x) >> 8, ay = (g.sy - g.foe[i].y) >> 8, d, cx, cy;
        uint32_t lk = foe_look[i], cls = (lk & 7u) ? (lk & 7u) : 1u;
        uint16_t col = fm1_ms - foe_hit_t[i] < 100u ? NW : FOE_COL[(lk >> 3) & 7u];
        g_w2s(g.foe[i].x >> 8, g.foe[i].y >> 8, &x, &y), g_rotv(&ax, &ay);
        d = g_isqrt(ax * ax + ay * ay) + 1, cx = ax * 32767 / d, cy = ay * 32767 / d;
        if (x < -6 || x > gw + 6 || y < -6 || y > gh + 6) {
            int16_t ex, ey;
            g_edge(-ax, -ay, 4, &ex, &ey);
            gp(ex, ey, col), gp(ex + 1, ey, col), gp(ex, ey + 1, col), gp(ex + 1, ey + 1, col), gp(ex - 1, ey, NK), gp(ex + 2, ey + 1, NK);
            continue;
        }
        g_hull_at(x, y, cls, cx, cy, col);
        if ((fm1_ms / 80u + i) & 1u)
            gp(x - (cx * 7 >> 15), y - (cy * 7 >> 15), EY);
    }
    for (i = 0; i < 8u; i++)                            /* the shots: red streaks */
        if (g.shot[i].t) {
            int32_t x, y, svx = g.shot[i].vx >> 8, svy = g.shot[i].vy >> 8, v;
            g_w2s(g.shot[i].x >> 8, g.shot[i].y >> 8, &x, &y), g_rotv(&svx, &svy);
            v = g_isqrt(svx * svx + svy * svy) + 1;
            gp(x, y, (fm1_ms / 60u + i) & 1u ? NW : ER);
            gp(x - svx * 2 / v, y - svy * 2 / v, ER);
            gp(x - svx * 3 / v, y - svy * 3 / v, ER);
        }
}


/* OCT- (A) / OCT+ (B) on HOME: a click is the octave (as on SLOOP), held G_HOLD_MS it is the game's A / B, both
   together the warp's map (g_oct_poll) */
#define G_HOLD_MS 250u
static struct { uint32_t t0[2], dbl; uint8_t fired[2], both, called; } go;   /* called: A held on foot called the ship */   /* dbl: an A click waiting for a second (space) */
#define G_DBL_A_MS 350u
static uint32_t g_help_t;                               /* the first-time help on screen since (0: not) */
static int g_help_eat(uint32_t pressed)                 /* ui_input.c: a press while it shows is its own */
{
    if (!g_help_t || !pressed)
        return 0;
    if (fm1_ms - g_help_t >= 3000u) {
        g_help_t = 0;
        settings.zoom |= 1u << 29;                      /* (seen: kept with the settings) */
        settings_later = 1;
        ui.force = 1;
    }
    return 1;
}
static int g_down(uint32_t b) { return (int)((fm1_in.buttons >> panel.btn[b ? B_OCTUP : B_OCTDN]) & 1u); }
static int g_held(uint32_t b)                           /* A / B held past a click (boost, run, the flame) */
{
    return g_down(b) && go.t0[b] && !go.both && fm1_ms - go.t0[b] >= G_HOLD_MS;
}

static void g_space_tick(uint32_t dt)
{
    int32_t adown = g_held(0) || g.cruise, boost = adown && g.fuel && g.thr, pulse;
    int32_t sp;
    if (!adown)
        g.a_t0 = 0;
    else if (!g.a_t0)
        g.a_t0 = G_STAMP;
    pulse = boost && fm1_ms - g.a_t0 > 1200u;           /* A held 1.2 s: the pulse jump (to the next planet) */
    if (boost && !g.boost_on)
        g_say("BOOST", "AFTERBURNER");
    if (pulse && !g.pulse_on)
        g_say("PULSE JUMP", "IN-SYSTEM JUMP");
    g.boost_on = (uint8_t)boost, g.pulse_on = (uint8_t)pulse;
    sp = g.thr * g_spd() * 256 * (pulse ? 9 : boost ? 3 : 1);   /* px / s, Q8 */
    /* (the pulse jump outruns them: they fall behind, and give up past 260 px, g_foes_tick) */
    if (boost && (g.boost_ms += dt) >= 2000u) {          /* the boost burns a unit of fuel every 2 s */
        g.boost_ms -= 2000u;
        g.fuel--;
        if (!g.fuel)
            ui_message("NO FUEL: COLLECT ORANGE");
    }
    int32_t tvx = (g_cos(g.ang) >> 7) * sp >> 8, tvy = (g_sin(g.ang) >> 7) * sp >> 8;
    g.vx += (tvx - g.vx) * (int32_t)dt / (pulse ? 250 : 500);
    g.vy += (tvy - g.vy) * (int32_t)dt / (pulse ? 250 : 500);
    if (g.foe_n)
        g_foes_tick(dt);
    sf_tick(dt);
    es_tick(dt);
    /* (no gravity: near a planet nothing slows or pulls the ship, the user's choice; B lands it in reach) */
    g.sx += g.vx * (int32_t)dt / 1000;
    g.sy += g.vy * (int32_t)dt / 1000;
    if (g.thr && (g.frame & 1u))                        /* the exhaust: left where it was made */
        g_puff(g.sx - (g_cos(g.ang) >> 6) * 2, g.sy - (g_sin(g.ang) >> 6) * 2);
}

/* -------------------------------------------------------------- surface --- */
static int32_t g_height(int32_t x, int32_t y)
{
    return (g_vnoise(x, y, g.pl.seed, 5, 0) * 3 + g_vnoise(x, y, g.pl.seed ^ 0x77u, 3, 0)) >> 2;
}
static int32_t g_sea(void) { return 92 + (int32_t)(g.pl.seed >> 24) % 24; }
static int g_got(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < G_NGOT && i < g.got_n; i++)
        if (g.got[i] == id)
            return 1;
    return 0;
}
/* the resource of cell (cx, cy) (16 x 16 px): 0 = none, else its kind + 1; *x, *y its place */
static uint32_t g_res(int32_t cx, int32_t cy, int32_t *x, int32_t *y)
{
    uint32_t h = g_hash(cx, cy, g.pl.seed ^ 0xBEEFu);
    int32_t hh;
    if (h % 5u)
        return 0;
    *x = cx * 16 + 2 + (int32_t)((h >> 8) % 12u), *y = cy * 16 + 2 + (int32_t)((h >> 12) % 12u);
    hh = g_height(*x, *y);
    if (hh < g_sea() + 4 || hh > 205 || g_got(h))
        return 0;
    return 1u + ((h >> 16) & 3u);
}

static void g_surface(int32_t y0, int32_t y1)
{
    static int16_t rows[3][124];
    int32_t sea = g_sea(), x, y, ox = gcx - gw / 2, oy = gcy - gh / 2, k;
    uint16_t wc = GCOL[g.pl.water & 7u], lc = GCOL[g.pl.land & 7u];
    uint32_t shim = fm1_ms / 350u;
    if (y0 < 0)
        y0 = 0;
    if (y1 > gh - 1)
        y1 = gh - 1;
    for (k = 0; k < 2; k++)                             /* the rows above and at y0 */
        for (x = 0; x <= gw + 1; x++)
            rows[k][x] = (int16_t)g_height(ox + x - 1, oy + y0 - 1 + k);
    for (y = y0; y <= y1; y++) {
        int16_t *up = rows[(y - y0) % 3], *mid = rows[(y - y0 + 1) % 3], *dn = rows[(y - y0 + 2) % 3];
        int32_t wy = oy + y;
        for (x = 0; x <= gw + 1; x++)
            dn[x] = (int16_t)g_height(ox + x - 1, wy + 1);
        for (x = 0; x < gw; x++) {
            int32_t h = mid[x + 1], wx = ox + x;
            uint16_t c = NK;
            if (h < sea) {                              /* water: shallow solid, then checks over the deep blue, deep:
                                                         * deep blue with waves passing (the sea's colour), glints */
                if (h > sea - 8)
                    c = wc;
                else if (h > sea - 24)
                    c = ((wx + wy + (int32_t)shim) & 1) ? wc : EN;
                else if (((wx + 2 * wy + (int32_t)shim) % 9) == 0)
                    c = h > sea - 50 ? wc : NP;         /* a wave: the sea's colour, purple far down */
                else if (((wx * 7 + wy * 3 + (int32_t)shim) % 61) == 0)
                    c = NW;                             /* a glint */
                else
                    c = EN;
            } else if (mid[x] < sea || mid[x + 2] < sea || up[x + 1] < sea || dn[x + 1] < sea) {
                c = NW;                                 /* the coast */
            } else if (h < sea + 12) {
                c = ((wx & 1) && (wy & 1)) ? NW : NO;   /* sand: white dots on orange */
            } else if (h < 165) {
                c = ((wx & 3) == 0 && (wy & 3) == 0) ? NK : lc;   /* grass: solid, a grid of holes */
            } else if (h < 205) {
                c = ((wx - wy) & 3) == 0 ? NW : GCOL[g.pl.land2 & 7u];   /* hills: their colour, white lines */
            } else {
                c = ((wx + wy) & 3) == 0 ? lc : NW;     /* peaks */
            }
            if (c != NK)
                gp(x, y, c);
        }
    }
}

static void g_res_draw(void)
{
    int32_t cx, cy, ox = gcx - gw / 2, oy = gcy - gh / 2;
    for (cy = (oy >> 4) - 1; cy <= ((oy + gh) >> 4); cy++)
        for (cx = (ox >> 4) - 1; cx <= ((ox + gw) >> 4); cx++) {
            int32_t x, y;
            uint32_t k = g_res(cx, cy, &x, &y);
            if (!k)
                continue;
            x -= ox, y -= oy;
            gp(x - 1, y - 1, NK), gp(x + 1, y - 1, NK), gp(x - 1, y + 1, NK), gp(x + 1, y + 1, NK);
            gp(x, y - 2, NK), gp(x - 2, y, NK), gp(x + 2, y, NK), gp(x, y + 2, NK);
            gp(x, y - 1, NCOL[k - 1u]), gp(x - 1, y, NCOL[k - 1u]), gp(x + 1, y, NCOL[k - 1u]), gp(x, y + 1, NCOL[k - 1u]);
            gp(x, y, (fm1_ms / 300u) & 1u ? NW : NCOL[k - 1u]);
        }
}

/* creatures: one per 64 x 64 block (some), wandering round its home, never stored */
static void g_creatures(void)
{
    int32_t bx, by, ox = gcx - gw / 2, oy = gcy - gh / 2;
    for (by = (oy >> 6) - 1; by <= ((oy + gh) >> 6) + 1; by++)
        for (bx = (ox >> 6) - 1; bx <= ((ox + gw) >> 6) + 1; bx++) {
            uint32_t h = g_hash(bx, by, g.pl.seed ^ 0xC0DEu), sp = (h >> 3) & 3u, ph = h >> 8;
            int32_t x, y, step;
            uint16_t c;
            if (h & 1u)
                continue;
            x = bx * 64 + 32 + ((g_sin(fm1_ms / (14u + sp * 4u) + ph) * 18) >> 15);
            y = by * 64 + 32 + ((g_cos(fm1_ms / (21u + sp * 3u) + ph * 3u) * 14) >> 15);
            if (g_height(x, y) < g_sea() + 2)            /* not in the water */
                continue;
            x -= ox, y -= oy;
            step = (int32_t)((fm1_ms / 180u) & 1u);
            gp(x - 1, y + 2, NK), gp(x, y + 2, NK), gp(x + 1, y + 2, NK), gp(x, y + 3, NK);   /* its shadow */
            c = NCOL[(sp + 2u) & 3u];
            switch (sp) {
            case 0:                                     /* a bug */
                gp(x, y, c), gp(x + 1, y, c), gp(x - 1, y + 1 - step, NW), gp(x + 2, y + step, NW);
                break;
            case 1:                                     /* a walker */
                gp(x, y - 1, NW), gp(x, y, c), gp(x - 1, y + 1, c), gp(x + 1, y + 1, c), gp(x - step, y + 2, c);
                break;
            case 2:                                     /* a hopper */
                gp(x, y - step, c), gp(x + 1, y - step, c), gp(x, y + 1 - step, c), gp(x + 1, y + 1 - step, NW);
                break;
            default:                                    /* a big one */
                gp(x - 1, y, c), gp(x, y, c), gp(x + 1, y, c), gp(x, y - 1, c), gp(x + 2, y - 1, NW),
                gp(x - 1, y + 1 + step, c), gp(x + 1, y + 2 - step, c);
                break;
            }
        }
}

static void g_wake(void)                               /* the surface's particles: a wake in the water, fading */
{
    uint32_t i;
    for (i = 0; i < G_NPART; i++) {
        uint32_t age = fm1_ms - g.part[i].t;
        int32_t x, y, o;
        uint16_t c;
        if (!g.part[i].t || age > 1500u)
            continue;
        x = (g.part[i].x >> 8) - gcx + gw / 2, y = (g.part[i].y >> 8) - gcy + gh / 2, o = (int32_t)age / 350;
        c = age < 900u ? NW : NB;                       /* foam, then the last of it blue */
        if (age > 600u && ((x + y + (int32_t)(age / 120u)) & 1))   /* (older: dithered away) */
            continue;
        gp(x - o, y, c), gp(x + o, y, c);               /* (it opens out as it ages: a V behind) */
        if (age < 400u)
            gp(x, y + 1, NW);
    }
}

#include "nms_anao.h"
#define G_ZAP_MS 450u                                   /* the mining beam: how long it shines */
#if NMS_ANAO
/* a frame of the dwarf (gen_anao.py: 10 x 11 with its outline, nibble 15 transparent), its feet on y + 1, mirrored
   when facing left; only the top `rows` (in the water, up to the waist) */
static void g_dwarf(int32_t x, int32_t y, uint32_t f, int32_t rows)
{
    static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
    const uint8_t *d = NMS_AN + f * NMS_AN_FB;
    int32_t u, v;
    for (v = 0; v < rows; v++)
        for (u = 0; u < NMS_AN_W; u++) {
            uint32_t i = (uint32_t)(v * NMS_AN_W + u), n = (d[i >> 1] >> ((i & 1u) ? 0 : 4)) & 15u;
            if (n < 10u)
                gp(x - 5 + (g.facing ? u : NMS_AN_W - 1 - u), y - 8 + v, PAL[n]);
        }
}
#endif
/* the mining beam: from the hand to what it hits, in screen pixels (8 wide at any zoom): black edges, blue, a white
   core running along it; it swells and flickers, sparks and a ring where it lands */
static void g_zap(int32_t x, int32_t y)
{
    static const uint16_t W[8] = {NK, NB, NB, NW, NW, NB, NB, NK};
    uint32_t age = fm1_ms - g.zap_t;
    int32_t hx = (x + (g.facing ? 3 : -4)) * gz + gz / 2, hy = (y - 3) * gz + gz / 2;
    int32_t tx, ty, ax, ay, n, k, sw;
    if (!g.zap_t || age >= G_ZAP_MS || age < 80u)       /* (it fires on the swing's second frame) */
        return;
    if (g.zap_sen && g.zap_sen <= g.sen_n)              /* (on a sentinel: it follows it) */
        g.zx = g.sen[g.zap_sen - 1u].x >> 8, g.zy = (g.sen[g.zap_sen - 1u].y >> 8) - 6;
    tx = (g.zx - gcx + gw / 2) * gz + gz / 2, ty = (g.zy - gcy + gh / 2) * gz + gz / 2;
    ax = tx > hx ? tx - hx : hx - tx, ay = ty > hy ? ty - hy : hy - ty, n = ax > ay ? ax : ay;
    if (n < 1)
        n = 1;
    sw = (int32_t)((fm1_ms / 60u) & 1u);                /* (it swells a pixel every other beat) */
    for (k = 0; k <= n; k++) {
        int32_t bx = hx + (tx - hx) * k / n, by = hy + (ty - hy) * k / n, j;
        uint32_t run = ((uint32_t)k + fm1_ms / 12u) % 14u;   /* the core: bright stretches running out from the hand */
        for (j = -sw; j < 8 + sw; j++) {
            uint16_t c = j < 0 || j > 7 ? NB : (j == 3 || j == 4) && run < 3u ? EN : W[j];
            if (ax > ay)
                cv_rect(bx, by - 4 + j, 1, 1, c);
            else
                cv_rect(bx - 4 + j, by, 1, 1, c);
        }
    }
    cv_rect(hx - 3, hy - 3, 6, 6, NW);                  /* the muzzle flash */
    for (k = 0; k < 8; k++) {                           /* sparks and a ring where it hits */
        uint32_t h = g_hash((int32_t)(fm1_ms / 50u), k, 0x2A9u);
        int32_t r = 6 + (int32_t)(age / 40u) % 6;
        cv_rect(tx + (int32_t)(h % 17u) - 8, ty + (int32_t)((h >> 5) % 17u) - 8, 2, 2, k & 1 ? NB : NW);
        cv_rect(tx + ((g_cos((uint32_t)k * 128u) * r) >> 15) - 1, ty + ((g_sin((uint32_t)k * 128u) * r) >> 15) - 1, 3, 3, NB);
    }
    cv_rect(tx - 4, ty - 4, 8, 8, NK), cv_rect(tx - 3, ty - 3, 6, 6, NB), cv_rect(tx - 2, ty - 2, 4, 4, NW);
}
static void g_player(int32_t x, int32_t y)
{
    uint32_t now = fm1_ms, swing = g.zap_t && now - g.zap_t < G_ZAP_MS;
    int32_t wet = g_height(g.px >> 8, g.py >> 8) < g_sea();
#if NMS_ANAO
    uint32_t f = swing ? NMS_AN_ATK + ((now - g.zap_t) / 90u > NMS_AN_ATK_N - 1u ? NMS_AN_ATK_N - 1u : (now - g.zap_t) / 90u)
               : g.walk ? NMS_AN_WALK + (now / 90u) % NMS_AN_WALK_N : NMS_AN_IDLE + (now / 150u) % NMS_AN_IDLE_N;
    if (wet) {                                          /* in the water: up to the waist, a ring of foam */
        g_dwarf(x, y, f, 7);
        gp(x - 3, y - 1, NW), gp(x + 3, y - 1, NW), gp(x - 2, y - 1, (now / 200u) & 1u ? NW : NB), gp(x + 2, y - 1, (now / 200u) & 1u ? NB : NW);
        gp(x - 1, y - 1, NB), gp(x, y - 1, NW), gp(x + 1, y - 1, NB);
    } else {
        gp(x - 2, y + 2, NK), gp(x - 1, y + 2, NK), gp(x, y + 2, NK), gp(x + 1, y + 2, NK), gp(x + 2, y + 2, NK);   /* the shadow */
        g_dwarf(x, y, f, NMS_AN_H);
    }
#else
    (void)swing;
    if (wet) {
        gp(x, y - 3, NW), gp(x + (g.facing ? 1 : -1), y - 2, NB), gp(x, y - 2, NW);
        gp(x, y - 1, NO), gp(x - 1, y - 1, NO), gp(x + 1, y - 1, NO);
        gp(x - 2, y, NW), gp(x + 2, y, NW), gp(x - 1, y, (now / 200u) & 1u ? NW : NB), gp(x + 1, y, (now / 200u) & 1u ? NB : NW);
    } else {
        int32_t st = g.walk ? (int32_t)((now / 140u) & 1u) : 0;
        gp(x - 1, y + 2, NK), gp(x, y + 2, NK), gp(x + 1, y + 2, NK);
        gp(x, y - 3, NW), gp(x + (g.facing ? 1 : -1), y - 2, NB), gp(x, y - 2, NW);
        gp(x, y - 1, NO), gp(x - 1, y - 1, NO), gp(x + 1, y - 1, NO);
        gp(x, y, NO);
        gp(x - 1, y + 1 - st, NW), gp(x + 1, y + st, NW);
    }
#endif
    g_zap(x, y);
}

static void g_parked(int32_t x, int32_t y)              /* the ship on the ground */
{
    gp(x, y - 2, NW), gp(x - 1, y - 1, NW), gp(x, y - 1, NB), gp(x + 1, y - 1, NW);
    gp(x - 2, y, NW), gp(x - 1, y, NW), gp(x, y, NW), gp(x + 1, y, NW), gp(x + 2, y, NW);
    gp(x - 2, y + 1, NO), gp(x + 2, y + 1, NO);
}

static void g_surf_tick(uint32_t dt)
{
    int32_t dx = g.tx - g.px, dy = g.ty - g.py, h = g_height(g.px >> 8, g.py >> 8), sea = g_sea();
    int32_t run = g_held(0), m = (int32_t)dt * (run ? 18 : 11);   /* Q8 px a ms: ~43 px/s, running ~70 */
    if (h < sea)                                        /* in the water: the shallows 70 %, deep water 40 % */
        m = m * (h > sea - 24 ? 7 : 4) / 10;
    if (h < sea && (dx * dx + dy * dy > 64) && (g.frame & 1u) == 0u)   /* walking in the water: a wake behind */
        g_puff(g.px + (int32_t)(g_hash((int32_t)g.frame, 1, 3u) % 3u) * 256 - 256, g.py + (1 << 8));
    g.walk = (uint8_t)(dx * dx + dy * dy > 64);
    if (dx > m) g.px += m; else if (dx < -m) g.px -= m; else g.px = g.tx;
    if (dy > m) g.py += m; else if (dy < -m) g.py -= m; else g.py = g.ty;
    if (dx > 0) g.facing = 1; else if (dx < 0) g.facing = 0;
}

/* ------------------------------------------------------------ the scanner --- */
static void g_scan(void)
{
    uint32_t age = fm1_ms - g.t_scan;
    if (!g.t_scan || age > 5000u)
        return;
    if (age < 1400u) {                                  /* the pulse: a blue ring of light opening from you to past the corners,
                                                         * like the beam (black edges, blue, a white core), in screen px */
        static const uint16_t W[7] = {NK, NB, NB, NW, NB, NB, NK};
        int32_t q = (int32_t)age * 256 / 1400, r = (256 * 256 - (256 - q) * (256 - q)) * 175 / (256 * 256), j, a, st;
        int32_t ox = gw / 2 * gz + gz / 2, oy = gh / 2 * gz + gz / 2;
        st = r > 8 ? 1024 / (r * 6) + 1 : 16;          /* (a point every ~1 px round) */
        for (j = 0; j < 7; j++) {
            int32_t rr = r - 3 + j;
            if (rr < 1)
                continue;
            for (a = 0; a < 1024; a += st)
                cv_rect(ox + ((g_cos((uint32_t)a) * rr) >> 15), oy + ((g_sin((uint32_t)a) * rr) >> 15), 1, 1,
                        j == 3 && ((uint32_t)a / 32u + age / 40u) % 6u == 0u ? NB : W[j]);
        }
    }
    if (g.mode == GM_SURF) {                            /* the resources near: brackets, arrows at the edge */
        int32_t cx, cy, ox = gcx - gw / 2, oy = gcy - gh / 2;
        for (cy = (gcy >> 4) - 6; cy <= (gcy >> 4) + 6; cy++)
            for (cx = (gcx >> 4) - 6; cx <= (gcx >> 4) + 6; cx++) {
                int32_t x, y;
                uint32_t k = g_res(cx, cy, &x, &y);
                if (!k)
                    continue;
                x -= ox, y -= oy;
                if (x < 2 || y < 2 || x > gw - 3 || y > gh - 3) {
                    int32_t ex = x < 1 ? 1 : x > gw - 2 ? gw - 2 : x, ey = y < 1 ? 1 : y > gh - 2 ? gh - 2 : y;
                    gp(ex, ey, NCOL[k - 1u]);
                } else if ((age / 250u) & 1u) {
                    gp(x - 2, y - 2, NW), gp(x + 2, y - 2, NW), gp(x - 2, y + 2, NW), gp(x + 2, y + 2, NW);
                }
            }
    }
}

/* ------------------------------------------------------------ sentinels --- */
/* a few hover over some planets (0..3, from its seed; a hostile planet: 5): within 120 px they follow you, circling at
   16 px, and look. The beam hurts them (3 hits): each hit shows n/3 over it, it blinks red and backs off; at 0 it blows up
   and the rest are alert for 3 s (a "!" over them, faster). On a hostile planet they are alert as soon as they are near
   you and shoot pixels back (they never hurt you) */
#define G_ALERT_MS 3000u
static void g_sen_init(void)
{
    uint32_t i;
    g.angry = (uint8_t)((g.pl.seed >> 20) % 3u == 0u);
    g.sen_n = (uint8_t)(g.angry ? 5u : (g.pl.seed >> 12) % 4u);
    g.sen_seen = 0, g.alert_t = 0, g.boom_t = 0, g.zap_sen = 0, g.zres = 0;
    for (i = 0; i < 8u; i++)
        g.shot[i].t = 0;
    for (i = 0; i < g.sen_n; i++) {
        g.sen[i].x = (g.lx + ((g_cos(i * 205u + 100u) * 70) >> 15)) << 8;
        g.sen[i].y = (g.ly + ((g_sin(i * 205u + 100u) * 70) >> 15)) << 8;
        g.sen[i].hp = 3, g.sen[i].hit = 0;
    }
}
static int g_sen_alert(uint32_t i)                      /* the "!" over it */
{
    int32_t ex = (g.px - g.sen[i].x) >> 8, ey = (g.py - g.sen[i].y) >> 8;
    return (g.alert_t && (int32_t)(g.alert_t - fm1_ms) > 0) || (g.angry && ex * ex + ey * ey < 48 * 48);
}
static void g_sen_hurt(uint32_t i)                      /* the beam hit sentinel i */
{
    int32_t dx = (g.sen[i].x - g.px) >> 8, dy = (g.sen[i].y - g.py) >> 8, d = g_isqrt(dx * dx + dy * dy) + 1;
    g.sen[i].hit = G_STAMP;
    g.sen[i].x += dx * 256 / d * 6, g.sen[i].y += dy * 256 / d * 6;   /* (knocked back 6 px) */
    if (--g.sen[i].hp)
        return;
    g.boom_x = g.sen[i].x >> 8, g.boom_y = (g.sen[i].y >> 8) - 6, g.boom_t = G_STAMP;   /* 0/3: it blows up */
    g.sen[i] = g.sen[--g.sen_n];
    g.zap_sen = 0;
    g.alert_t = fm1_ms + G_ALERT_MS;
    g_say(g.sen_n ? "SENTINEL DOWN" : "ALL SENTINELS DOWN", g.sen_n ? "THE REST ARE ALERT" : "ALL CLEAR");
}
static void g_sen_tick(uint32_t dt)
{
    uint32_t i, now = fm1_ms;
    for (i = 0; i < g.sen_n; i++) {
        uint32_t a = now / 12u + i * 205u;
        int32_t back = g.sen[i].hit && now - g.sen[i].hit < 1200u, al = g_sen_alert(i), r = back ? 34 : 16;   /* hit: backs off */
        int32_t tx = g.px + (g_cos(a) >> 7) * r, ty = g.py + (g_sin(a) >> 7) * r;   /* (Q8) */
        int32_t dx = tx - g.sen[i].x, dy = ty - g.sen[i].y, ex = (g.px - g.sen[i].x) >> 8, ey = (g.py - g.sen[i].y) >> 8;
        int32_t m = (int32_t)dt * (al ? 12 : 6), d = g_isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)) + 1;   /* ~23 px / s, alert 46 */
        if (ex * ex + ey * ey > (g.angry ? 160 * 160 : 120 * 120))
            continue;                                   /* too far: it waits */
        if (ex * ex + ey * ey < 40 * 40 && !g.sen_seen) {
            g.sen_seen = 1;
            ui_message(g.angry ? "HOSTILE SENTINELS" : "SENTINELS WATCHING");
        }
        if (d * 256 <= m) {
            g.sen[i].x = tx, g.sen[i].y = ty;
        } else {
            g.sen[i].x += dx / d * m >> 8;
            g.sen[i].y += dy / d * m >> 8;
        }
        if (g.angry && al && !back && ex * ex + ey * ey < 70 * 70 &&   /* hostile, near: a shot now and then */
            (now / 50u + i * 7u) % (24u + i * 3u) == 0u) {
            uint32_t k;
            int32_t ad = g_isqrt(ex * ex + (ey + 6) * (ey + 6)) + 1;
            for (k = 0; k < 8u && g.shot[k].t; k++)
                ;
            if (k < 8u) {
                g.shot[k].x = g.sen[i].x, g.shot[k].y = g.sen[i].y - (6 << 8), g.shot[k].t = G_STAMP;
                g.shot[k].vx = ex * 256 / ad * 80, g.shot[k].vy = (ey + 6) * 256 / ad * 80;   /* 80 px / s */
            }
        }
    }
    for (i = 0; i < 8u; i++) {                          /* their shots: they burst on you (no harm) */
        int32_t ex, ey;
        if (!g.shot[i].t)
            continue;
        if (now - g.shot[i].t > 1600u) {
            g.shot[i].t = 0;
            continue;
        }
        g.shot[i].x += g.shot[i].vx * (int32_t)dt / 1000, g.shot[i].y += g.shot[i].vy * (int32_t)dt / 1000;
        ex = (g.shot[i].x - g.px) >> 8, ey = ((g.shot[i].y - g.py) >> 8) + 3;
        if (ex * ex + ey * ey < 9) {
            g_puff(g.shot[i].x, g.shot[i].y);
            g.shot[i].t = 0;
        }
    }
}
static void g_sen_draw(void)
{
    uint32_t i;
    for (i = 0; i < g.sen_n; i++) {
        int32_t x = (g.sen[i].x >> 8) - gcx + gw / 2, y = (g.sen[i].y >> 8) - gcy + gh / 2;
        int32_t ex = gw / 2 - x, ey = gh / 2 - y, near = ex * ex + ey * ey < 40 * 40;
        int32_t by = y - 6 + (int32_t)(((fm1_ms / 220u) + i) & 1u);   /* it hovers, bobbing */
        uint16_t eye = near ? NO : NB;
        int32_t red = g.sen[i].hit && fm1_ms - g.sen[i].hit < 500u && ((fm1_ms / 70u) & 1u);   /* hit: blinks red */
        gp(x - 2, y + 1, NK), gp(x - 1, y + 1, NK), gp(x, y + 1, NK), gp(x + 1, y + 1, NK), gp(x + 2, y + 1, NK);   /* its shadow */
        if (near && ((fm1_ms / 120u + i * 7u) % 24u) < 4u) {   /* now and then a look: a dotted beam to you */
            int32_t k;
            for (k = 1; k < 10; k += 2)
                gp(x + ex * k / 10, by + (ey + 5) * k / 10, NB);
        }
        {   /* a drone, solid white, 7 x 5: the eye in a black socket, two thrusters blinking under it */
            static const char *const D[5] = {"  ###  ", " ##### ", "##KEK##", " ##### ", "  T T  "};
            int32_t u, v;
            for (v = 0; v < 5; v++)
                for (u = 0; u < 7; u++) {
                    char ch = D[v][u];
                    if (ch == ' ' || (ch == 'T' && ((fm1_ms / 150u) & 1u)))
                        continue;
                    gp(x - 3 + u, by - 3 + v, ch == '#' ? (red ? ER : NW) : ch == 'K' ? NK : ch == 'T' ? NB :
                                                (fm1_ms / 900u + i) % 5u ? eye : NK);   /* (the eye blinks) */
                }
        }
        if (g_sen_alert(i) && ((fm1_ms / 200u) & 3u)) {  /* alert: a "!" over it, yellow in black */
            int32_t v;
            for (v = -1; v <= 5; v++)
                gp(x - 1, by - 11 + v, NK), gp(x + 1, by - 11 + v, NK);
            gp(x, by - 12, NK), gp(x, by - 8, NK), gp(x, by - 6, NK);
            gp(x, by - 11, EY), gp(x, by - 10, EY), gp(x, by - 9, EY), gp(x, by - 7, EY);
        }
    }
    if (g.boom_t && fm1_ms - g.boom_t < 800u) {         /* one blew up: a flash, a ring, bits flying out */
        uint32_t age = fm1_ms - g.boom_t, k;
        int32_t x = g.boom_x - gcx + gw / 2, y = g.boom_y - gcy + gh / 2, r = (int32_t)age / 45;
        uint16_t c = age < 150u ? NW : age < 350u ? EY : age < 550u ? ER : NP;
        if (age < 120u)
            for (k = 0; k < 25u; k++)
                gp(x - 2 + (int32_t)(k % 5u), y - 2 + (int32_t)(k / 5u), NW);
        for (k = 0; k < 1024u; k += 32u)
            if (((k >> 5) + age / 60u) & 1u)
                gp(x + ((g_cos(k) * r) >> 15), y + ((g_sin(k) * r) >> 15), c);
        for (k = 0; k < 12u; k++) {
            uint32_t h = g_hash((int32_t)k, (int32_t)g.boom_t, 0xB00u), an = h & 1023u;
            int32_t dd = (int32_t)age * (int32_t)(20u + (h >> 10) % 30u) / 1000;
            gp(x + ((g_cos(an) * dd) >> 15), y + ((g_sin(an) * dd) >> 15) + (int32_t)(age * age / 60000u), k & 1u ? c : NO);
        }
    }
    for (i = 0; i < 8u; i++)                            /* their shots: a pixel, red and yellow by turns */
        if (g.shot[i].t)
            gp((g.shot[i].x >> 8) - gcx + gw / 2, (g.shot[i].y >> 8) - gcy + gh / 2, (fm1_ms / 60u + i) & 1u ? ER : EY);
}
/* g_hud: a sentinel just hit shows its life over it (2/3, 1/3) for 2 s */
static void g_sen_text(void)
{
    uint32_t i;
    for (i = 0; i < g.sen_n; i++) {
        char t[4] = {(char)('0' + g.sen[i].hp), '/', '3', 0};
        int32_t x = ((g.sen[i].x >> 8) - gcx + gw / 2) * gz + gz / 2, y = ((g.sen[i].y >> 8) - 6 - gcy + gh / 2) * gz;
        if (!g.sen[i].hit || fm1_ms - g.sen[i].hit > 2000u)
            continue;
        n_texto(x - n_w(t, 1) / 2, y - 4 * gz - 18 - (g_sen_alert(i) ? 8 * gz : 0), t, 1, g.sen[i].hp == 1 ? ER : NW);
    }
}

/* ------------------------------------------------------------ fuel, warp --- */
static int32_t g_jump(int32_t dx, int32_t dy)          /* a warp's length in sectors, rounded up */
{
    int32_t d2 = dx * dx + dy * dy, r = g_isqrt(d2);
    return r * r < d2 ? r + 1 : r;
}
static void g_refuel(void)                              /* the orange carried into the tank (as you board) */
{
    int32_t before = g.fuel;
    while (g.res[3] && g.fuel + G_FUEL_ORANGE <= G_CLASS[g.cls].tank)
        g.res[3]--, g.fuel = (uint8_t)(g.fuel + G_FUEL_ORANGE);
    if (g.fuel > before) {
        char b[16];
        str_cpy(b, "FUEL +", sizeof b);
        fmt_int(b + 6, g.fuel - before);
        ui_message(b);
        g.dirty = 1;
    }
}
static int32_t g_fuel_can(void)                         /* the fuel there would be after a refuel */
{
    int32_t f = g.fuel + g.res[3] * G_FUEL_ORANGE;
    return f > G_CLASS[g.cls].tank ? G_CLASS[g.cls].tank : f;
}

/* the map, after Low Mem Sky's: the night, each system a star where its sector puts it (its colour, bigger with planets),
 * the one picked under a reticle with its name over it; the ship, the warp's reach (a dotted ring), the jump (a line), the
 * centre of the galaxy (a turning cross, or a mark at the edge toward it). 11 x 7 sectors of 20 px round the cursor. */
static int32_t gm_cell = 20, gm_cols = 11, gm_rows = 7;  /* the map's zoom (KNOB 3): 14, 20 or 28 px a sector */
#define GM_CELL gm_cell
#define GM_COLS gm_cols
#define GM_ROWS gm_rows
#define GM_X0 (120 - GM_COLS * GM_CELL / 2)
#define GM_Y0 (112 - GM_ROWS * GM_CELL / 2)
static void gpm(int32_t x, int32_t y, uint16_t c)       /* a pixel, inside the map */
{
    if (x >= GM_X0 && y >= GM_Y0 && x < GM_X0 + GM_COLS * GM_CELL && y < GM_Y0 + GM_ROWS * GM_CELL)
        gp(x, y, c);
}
/* a system's place in the view (its sector's cell, moved a little by its hash) and its star's colour */
static void g_star_at(int32_t ca, int32_t cb, int32_t *x, int32_t *y, uint16_t *c)
{
    uint32_t h = g_hash(ca, cb, 0x57A5u);
    *x = GM_X0 + (ca - g.mx + GM_COLS / 2) * GM_CELL + 4 + (int32_t)(h % 12u);
    *y = GM_Y0 + (cb - g.my + GM_ROWS / 2) * GM_CELL + 4 + (int32_t)((h >> 4) % 12u);
    *c = g_star_col(ca, cb);
}
/* a system's name: two or three syllables from its sector (CANINAY, KORVA...) */
static void g_sys_name(int32_t ca, int32_t cb, char *b)
{
    static const char *const SY[16] = {"CA", "NI", "NAY", "KOR", "VA", "ZE", "LO", "RIM", "TA", "SU", "ON", "DRA", "XI", "BEL", "MO", "QUA"};
    uint32_t h = g_hash(ca, cb, 0x4A4Du), n = 2u + (h & 1u), k;
    b[0] = 0;
    for (k = 0; k < n; k++)
        str_cpy(b + str_len(b), SY[(h >> (4u + k * 4u)) & 15u], 4);
}
static void g_map(void)
{
    int32_t sx, sy, i, j, k;
    static const uint8_t CELL[3] = {14, 20, 28};
    gm_cell = CELL[g.mz % 3u], gm_cols = (220 / gm_cell) | 1, gm_rows = (140 / gm_cell) | 1;
    sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8);
    int32_t jump = g_jump(g.mx - sx, g.my - sy), ok = jump > 0 && jump <= G_CLASS[g.cls].range && jump <= g_fuel_can();
    int32_t shx = GM_X0 + (sx - g.mx + GM_COLS / 2) * GM_CELL + GM_CELL / 2, shy = GM_Y0 + (sy - g.my + GM_ROWS / 2) * GM_CELL + GM_CELL / 2;
    int32_t px = 0, py = 0;
    uint32_t a;
    for (i = 0; i < 70; i++) {                          /* far stars (no systems): the depth, drifting with the cursor */
        uint32_t h = g_hash(i, 77, 0xD5u);
        int32_t x = (int32_t)(h % 240u) - g.mx * 3, y = (int32_t)((h >> 8) % 240u) - g.my * 3;
        x = ((x % 240) + 240) % 240, y = ((y % 240) + 240) % 240;
        gpm(x, y, (h >> 20) % 3u ? EN : NP);
    }
    for (j = -GM_ROWS / 2 - 1; j <= GM_ROWS / 2 + 1; j++)
        for (i = -GM_COLS / 2 - 1; i <= GM_COLS / 2 + 1; i++) {
            int32_t ca = g.mx + i, cb = g.my + j, x, y, n = 0, qx, qy;
            planet_t p;
            uint16_t c;
            for (k = 0; k < (int32_t)G_NPL && g_planet(ca, cb, (uint32_t)k, &p); k++)
                n++;
            g_star_at(ca, cb, &x, &y, &c);
            if (i == 0 && j == 0)
                px = x, py = y;
            if (n) {                                    /* a system: 1 px, 2 x 2 with two planets or more */
                gpm(x, y, c);
                if (n > 1)
                    gpm(x + 1, y, c), gpm(x, y + 1, c), gpm(x + 1, y + 1, c);
            } else if (!(g_hash(ca, cb, 0x11u) & 3u)) {
                gpm(x, y, NP);                          /* (an empty one, faint) */
            }
            if (g_station(ca, cb, &qx, &qy))            /* a station: its icon beside its star */
                g_st_icon(x + 6, y - 5, gpm);
            if (ca == G_CX && cb == G_CY) {             /* the centre: a turning cross */
                uint32_t r = fm1_ms / 30u;
                for (k = 0; k < 4; k++)
                    gline(x, y, x + ((g_cos(r + (uint32_t)k * 256u) * 7) >> 15), y + ((g_sin(r + (uint32_t)k * 256u) * 7) >> 15), EY),
                    gline(x, y, x + ((g_cos(r + 128u + (uint32_t)k * 256u) * 4) >> 15), y + ((g_sin(r + 128u + (uint32_t)k * 256u) * 4) >> 15), ER);
            }
        }
    if (g.t_scan && fm1_ms - g.t_scan < 2500u) {       /* the radar (KNOB 4): a ring out from the ship, the systems it
                                                         * has passed lit white, stations framed */
        uint32_t age = fm1_ms - g.t_scan;
        int32_t rr = (int32_t)age * 200 / 1400;
        for (a = 0; a < 1024u; a += 6u)
            gpm(shx + ((g_cos(a) * rr) >> 15), shy + ((g_sin(a) * rr) >> 15), EG);
        for (j = -GM_ROWS / 2 - 1; j <= GM_ROWS / 2 + 1; j++)
            for (i = -GM_COLS / 2 - 1; i <= GM_COLS / 2 + 1; i++) {
                int32_t x, y, qx, qy;
                planet_t p;
                uint16_t c;
                g_star_at(g.mx + i, g.my + j, &x, &y, &c);
                if ((x - shx) * (x - shx) + (y - shy) * (y - shy) > rr * rr || !g_planet(g.mx + i, g.my + j, 0, &p))
                    continue;
                gpm(x - 1, y, NW), gpm(x + 1, y, NW), gpm(x, y - 1, NW), gpm(x, y + 1, NW);
                if (g_station(g.mx + i, g.my + j, &qx, &qy))
                    g_st_icon(x + 6, y - 5, gpm);
            }
    }
    for (a = 0; a < 1024u; a += 8u)                     /* the warp's reach */
        if ((a / 8u + fm1_ms / 200u) & 1u)
            gpm(shx + ((g_cos(a) * G_CLASS[g.cls].range * GM_CELL) >> 15), shy + ((g_sin(a) * G_CLASS[g.cls].range * GM_CELL) >> 15), NB);
    if (jump) {                                         /* the jump: dotted, white when it can be made */
        int32_t n = (jump * GM_CELL) / 3 + 1;
        for (k = 1; k < n; k++)
            if ((k + (int32_t)(fm1_ms / 120u)) % 3 == 0)
                gpm(shx + (px - shx) * k / n, shy + (py - shy) * k / n, ok ? NW : ER);
    }
    if (shx > GM_X0 && shy > GM_Y0 && shx < GM_X0 + GM_COLS * GM_CELL && shy < GM_Y0 + GM_ROWS * GM_CELL && jump)
        g_ship(shx, shy, g.ang, 0, 1);
    {   /* the reticle: a white cross in a red ring, the name over it */
        char b[12];
        for (a = 0; a < 1024u; a += 64u)
            if ((a / 64u) % 4u != 0u)
                gp(px + ((g_cos(a + fm1_ms / 8u) * 6) >> 15), py + ((g_sin(a + fm1_ms / 8u) * 6) >> 15), ER);
        gline(px - 3, py, px + 3, py, NW), gline(px, py - 3, px, py + 3, NW);
        g_sys_name(g.mx, g.my, b);
        n_texto(clamp(px - n_w(b, 1) / 2, 2, 238 - n_w(b, 1)), py - 26, b, 1, NW);
    }
    {   /* the centre, far off: a mark at the edge, toward it */
        int32_t dx = G_CX - g.mx, dy = G_CY - g.my, m = dx < 0 ? -dx : dx, my = dy < 0 ? -dy : dy;
        if (my > m)
            m = my;
        if (m > GM_ROWS / 2) {
            int32_t cx = 120 + dx * 104 / m, cy = GM_Y0 + GM_ROWS * GM_CELL / 2 + dy * (GM_ROWS * GM_CELL / 2 - 4) / m;
            int32_t blink = (int32_t)((fm1_ms / 250u) & 1u);
            gpm(cx, cy, NW), gpm(cx - 1, cy, EY), gpm(cx + 1, cy, EY), gpm(cx, cy - 1, EY), gpm(cx, cy + 1, EY);
            gpm(cx - 2 - blink, cy, ER), gpm(cx + 2 + blink, cy, ER), gpm(cx, cy - 2 - blink, ER), gpm(cx, cy + 2 + blink, ER);
        }
    }
}

/* the warp, 6 s. The ship in screen pixels at any size (L: its nose, in px): medium while it charges, then the jump (it
 * shrinks to a point in the distance), back growing larger, then hovering along a figure 8 lying down, turned the way
 * it goes, bobbing; a flash and it arrives. Behind: a tunnel of rings and the stars streaming, as fast as the phase. With
 * assets/nave.png (tools/gen_nave.py) the ship is that picture. */
#define G_WARP_MS 6000u
#include "nms_nave.h"
#include "nms_nave_decola.h"
static void g_ship_px(int32_t cx, int32_t cy, uint32_t a, int32_t L, int flame);
/* (the user) the take-off from the 3D model: frame f of NMS_ND (its nose rising), L its half width, centred on (cx, cy) */
static void g_ship_decola(int32_t cx, int32_t cy, int32_t L, int32_t f)
{
#if NMS_NAVE_DECOLA
    static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
    int32_t w = L * 2, h = w * NMS_ND_H / NMS_ND_W, i, j;
    const uint8_t *fr = NMS_ND + clamp(f, 0, NMS_ND_N - 1) * NMS_ND_FB;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint32_t sx = (uint32_t)(i * NMS_ND_W / w), sy = (uint32_t)(j * NMS_ND_H / h), k = sy * NMS_ND_W + sx;
            uint32_t v = (fr[k >> 1] >> ((k & 1u) ? 0u : 4u)) & 15u;
            if (v < 10u)
                cv_pset(cx - w / 2 + i, cy - h / 2 + j, PAL[v]);
        }
#else
    (void)f;
    g_ship_px(cx, cy, 768u, L, 0);
#endif
}
static void g_ship_tint(int32_t cx, int32_t cy, int32_t L, uint16_t c0, uint16_t c1);
static void g_ship_tint(int32_t cx, int32_t cy, int32_t L, uint16_t c0, uint16_t c1)   /* (the race) the ship from behind, recoloured */
{
#if NMS_NAVE
    uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
    int32_t w = L * 2, h = w * NMS_NV_H / NMS_NV_W, i, j;
    PAL[7] = c0, PAL[1] = c1, PAL[5] = c0;
    if (w < 2)
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint32_t sx = (uint32_t)(i * NMS_NV_W / w), sy = (uint32_t)(j * NMS_NV_H / h), k = sy * NMS_NV_W + sx;
            uint32_t v = (NMS_NV[k >> 1] >> ((k & 1u) ? 0u : 4u)) & 15u;
            if (v < 10u)
                cv_pset(cx - w / 2 + i, cy - h / 2 + j, PAL[v]);
        }
#else
    cv_rect(cx - L, cy - L / 2, 2 * L, L, c0);
#endif
}
static void g_ship_px(int32_t cx, int32_t cy, uint32_t a, int32_t L, int flame)
{
    int32_t i, j, r = L + 2;
#if NMS_NAVE
    static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
    int32_t w = L * 2, h = w * NMS_NV_H / NMS_NV_W;    /* (the picture: L is half its width; it does not turn) */
    (void)a;
    if (w < 2)
        w = 2, h = 2;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint32_t sx = (uint32_t)(i * NMS_NV_W / w), sy = (uint32_t)(j * NMS_NV_H / h), k = sy * NMS_NV_W + sx;
            uint32_t v = (NMS_NV[k >> 1] >> ((k & 1u) ? 0u : 4u)) & 15u;
            if (v < 10u)
                cv_pset(cx - w / 2 + i, cy - h / 2 + j, PAL[v]);
        }
    if (flame && L > 6)                                 /* seen from behind: sparks trail from it, down the tunnel */
        for (i = 0; i < 6; i++) {
            uint32_t hh = g_hash(i, (int32_t)(fm1_ms / 70u), 0x5Eu);
            cv_rect(cx - w / 3 + (int32_t)(hh % (uint32_t)(w * 2 / 3 + 1)), cy + h / 2 + (int32_t)((hh >> 8) % 6u), 1, 1, (hh >> 16) & 1u ? NB : EY);
        }
    return;
#endif
    {
        int32_t nx = cx + ((g_cos(a) * L) >> 15), ny = cy + ((g_sin(a) * L) >> 15);
        int32_t lx = cx + ((g_cos(a + 370u) * L * 3 / 4) >> 15), ly = cy + ((g_sin(a + 370u) * L * 3 / 4) >> 15);
        int32_t rx = cx + ((g_cos(a - 370u) * L * 3 / 4) >> 15), ry = cy + ((g_sin(a - 370u) * L * 3 / 4) >> 15);
        int32_t bx = cx + ((g_cos(a + 512u) * L / 4) >> 15), by = cy + ((g_sin(a + 512u) * L / 4) >> 15);
        if (L < 2) {
            cv_rect(cx, cy, 2, 2, NW);
            return;
        }
        if (flame) {                                    /* the flame: yellow, then red, flickering */
            int32_t fl = L / 2 + (int32_t)((fm1_ms / 50u) % 3u) * L / 8;
            cv_line(bx, by, bx + ((g_cos(a + 512u) * fl) >> 15), by + ((g_sin(a + 512u) * fl) >> 15), EY);
            cv_line(bx + 1, by, bx + 1 + ((g_cos(a + 512u) * fl * 2 / 3) >> 15), by + ((g_sin(a + 512u) * fl * 2 / 3) >> 15), ER);
        }
        for (j = cy - r; j <= cy + r; j++)              /* the hull: two triangles, nose-wing-tail */
            for (i = cx - r; i <= cx + r; i++) {
                int32_t e1 = (lx - nx) * (j - ny) - (ly - ny) * (i - nx), e2 = (bx - lx) * (j - ly) - (by - ly) * (i - lx);
                int32_t e3 = (nx - bx) * (j - by) - (ny - by) * (i - bx);
                int32_t f1 = (bx - nx) * (j - ny) - (by - ny) * (i - nx), f2 = (rx - bx) * (j - by) - (ry - by) * (i - bx);
                int32_t f3 = (nx - rx) * (j - ry) - (ny - ry) * (i - rx);
                if (((e1 >= 0 && e2 >= 0 && e3 >= 0) || (e1 <= 0 && e2 <= 0 && e3 <= 0)) ||
                    ((f1 >= 0 && f2 >= 0 && f3 >= 0) || (f1 <= 0 && f2 <= 0 && f3 <= 0)))
                    cv_pset(i, j, ((i + j) & 1) && L > 10 && (j - cy) * (int32_t)(g_sin(a) >> 12) > 0 ? NP : NW);   /* (a shade) */
            }
        n_disc(cx + ((g_cos(a) * L / 4) >> 15), cy + ((g_sin(a) * L / 4) >> 15), L > 12 ? 2 : 1, NB);   /* the cockpit */
    }
}

/* ease: q 0..256 -> expo-ish out / in */
static int32_t g_eout(int32_t q) { q = 256 - q; return 256 - (q * q / 256) * q / 256; }
static int32_t g_ein(int32_t q) { return (q * q / 256) * q / 256; }

#define G_WM (NMS_NAVE ? 32 : 16)                       /* the ship's size (L) in the warp: medium (the picture 1:1), */
#define G_WB (NMS_NAVE ? 64 : 30)                       /* .. big (the picture 2:1, clean pixels) */
#define G_W8X (NMS_NAVE ? 44 : 70)                      /* .. the figure 8's width and height (half) */
#define G_W8Y (NMS_NAVE ? 26 : 34)
static void g_warp_draw(uint32_t el)
{
    static const uint16_t WC[6] = {NW, EY, ER, EG, NB, NW};
    int32_t t = (int32_t)el, L, cx = 120, cy = 120, sp, ring;
    uint32_t i, a = 768u, flame = 1;
    /* the phases */
    if (t < 900) {                                      /* charging: medium, a tremble */
        L = G_WM;
        cx += (int32_t)((fm1_ms / 40u) % 3u) - 1;
        sp = 30 + t * 120 / 900;
    } else if (t < 1500) {                              /* the jump: into the distance */
        int32_t q = (t - 900) * 256 / 600;
        L = G_WM - (G_WM - 1) * g_ein(q) / 256;
        cy -= 30 * g_ein(q) / 256;
        sp = 400;
        flame = 2;
    } else if (t < 2600) {                              /* back, growing larger */
        int32_t q = (t - 1500) * 256 / 1100;
        L = 1 + (G_WB - 1) * g_eout(q) / 256;
        cy = 90 + 30 * g_eout(q) / 256;
        sp = 260 - q / 2;
    } else if (t < 5500) {                              /* hovering: a figure 8 lying down (x = sin u, y = sin 2u / 2) */
        uint32_t u = (uint32_t)((t - 2600) * 1024 / 2600);   /* (a turn in 2.6 s) */
        int32_t ax = G_W8X * (t < 3000 ? (t - 2600) : 400) / 400, ay = G_W8Y * (t < 3000 ? (t - 2600) : 400) / 400;
        int32_t dx = (g_cos(u) * ax) >> 15, dy = (g_cos(2u * u) * ay) >> 15, k, best = -0x7FFFFFFF;
        cx = 120 + ((g_sin(u) * ax) >> 15);
        cy = 120 + ((g_sin(2u * u) * ay) >> 16) + ((g_sin((uint32_t)t / 2u) * 3) >> 15);   /* (+ a bob) */
        for (k = 0; k < 1024; k += 16) {               /* turned the way it goes */
            int32_t d = (g_cos((uint32_t)k) >> 4) * dx + (g_sin((uint32_t)k) >> 4) * dy;
            if (d > best)
                best = d, a = (uint32_t)k;
        }
        if (dx == 0 && dy == 0)
            a = 768u;
        L = G_WB + (NMS_NAVE ? 0 : ((g_sin((uint32_t)t / 3u) * 2) >> 15));
        sp = 130;
    } else {                                            /* arriving: back to the middle, medium, a flash */
        int32_t q = (t - 5500) * 256 / 500;
        L = G_WB - (G_WB - G_WM) * g_eout(q) / 256;
        sp = 130 - q / 3;
    }
    /* behind: the tunnel and the streaks (logical px), as fast as sp */
    for (ring = 0; ring < 6; ring++) {
        int32_t r = (int32_t)(((uint32_t)ring * 40u + (uint32_t)t * (uint32_t)sp / 1200u) % 240u) * gw / 240;
        uint32_t b;
        for (b = 0; b < 1024u; b += 12u)
            if (((b / 12u) & 1u) == 0u)
                gp(gw / 2 + ((g_cos(b) * r) >> 15), gh / 2 + ((g_sin(b) * r) >> 15), r < gw / 4 ? EN : NP);
    }
    for (i = 0; i < 72u; i++) {
        uint32_t h = g_hash((int32_t)i, 9, 0xAAu), b = h & 1023u;
        int32_t r0 = (int32_t)(((h >> 10) % (uint32_t)gw + (uint32_t)t * (uint32_t)sp / 900u) % (uint32_t)gw);
        int32_t len = 1 + sp * gw / 4000, c0 = g_cos(b), s0 = g_sin(b);
        gline(gw / 2 + ((c0 * r0) >> 15), gh / 2 + ((s0 * r0) >> 15), gw / 2 + ((c0 * (r0 + len)) >> 15), gh / 2 + ((s0 * (r0 + len)) >> 15),
              WC[i % 6u]);
    }
    if (t >= 1380 && t < 1620)                          /* the jump's flash: a small bright point */
        n_disc(120, 90, 2 + (t - 1380) / 30, NW);
    g_ship_px(cx, cy, a, L, (int)flame);
    if (t > 5600) {                                     /* the arrival: a ring of light opening from the middle */
        int32_t r = (t - 5600) * 170 / 400;
        n_ring(120, 120, r, 6, NW);
        if (r > 10)
            n_ring(120, 120, r - 7, 3, EY);
    }
}

/* ------------------------------------------------------------ the take-off --- */
/* B by the ship: a little film, 6.6 s. The ship from the side (assets/decolagem.png, the camera panning across it between
 * black bars, grass going by in front faster: depth); a cut: the ship from behind on the ground, under this planet's sky,
 * its engines waking; it lifts, the ground falls away, clouds rush past in perspective, the sky darkens to night; out of
 * the air the stars streak past; it slows, and the game goes on from above, by the planet. */
#include "nms_decolagem.h"
#define G_TO_SIDE (NMS_DECOL ? 2200 : 0)                /* (no picture: it starts behind the ship) */
#include "nms_nave_lado.h"
#define G_TO_REAR 3600                                  /* behind the ship: the ground, the clouds, the stars */
#define G_TO_FLY (NMS_NAVE_LADO ? 2800 : 1000)          /* then from the side, leaving the planet */
#define G_TO_MS ((uint32_t)G_TO_SIDE + (uint32_t)G_TO_REAR + (uint32_t)G_TO_FLY)
static void g_side_draw(int32_t t)
{
#if NMS_DECOL
    uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};   /* (its orange ground: this planet's land colour) */
    int32_t q = t * 256 / G_TO_SIDE, e = q < 128 ? 2 * q * q / 256 : 256 - 2 * (256 - q) * (256 - q) / 256;   /* ease in-out */
    int32_t ox = -e * (NMS_DC_W * 2 - 240) / 256, oy = 30, k, n = 0, i;
    PAL[5] = GCOL[g.pl.land & 7u];
    for (k = 0; k < NMS_DC_LEN; k++) {                   /* the picture, 2 x, its runs row by row */
        int32_t run = (NMS_DC[k] >> 4) + 1;
        uint16_t c = PAL[NMS_DC[k] & 15u];
        while (run > 0) {
            int32_t y = n / NMS_DC_W, x = n % NMS_DC_W, len = NMS_DC_W - x < run ? NMS_DC_W - x : run;
            if (c != NK)
                cv_rect(ox + x * 2, oy + y * 2, len * 2, 2, c);
            n += len, run -= len;
        }
    }
    for (i = 0; i < 46; i++) {                           /* grass in front, going by twice as fast */
        uint32_t h = g_hash(i, 5, 0x6A55u);
        int32_t x = (int32_t)(h % 300u) + ox * 2, hh = 10 + (int32_t)((h >> 9) % 26u);
        x = ((x % 300) + 300) % 300 - 30;
        cv_rect(x, 210 - hh, 2, hh, (h >> 16) % 3u == 0u ? EY : (h >> 16) % 3u == 1u ? EN : NK);
        cv_rect(x + 2, 212 - hh / 2, 1, hh / 2, NK);
    }
    cv_rect(0, 0, 240, 30, NK);                          /* the bars */
    cv_rect(0, 210, 240, 30, NK);
#else
    (void)t;
#endif
}
/* the last shot: the ship from the side (assets/nave-lado.png), crossing to the left; behind it the planet falls away,
   smaller and smaller, the nebula and three layers of stars going by at their depths; at the end it shoots off */
static void g_fly_draw(int32_t t)
{
    static const uint16_t SC[3][4] = {{NP, EN, NB, NP}, {NB, NW, NP, EY}, {NW, EY, NW, ER}};
    int32_t q = t * 256 / G_TO_FLY, e = 256 - (256 - q) * (256 - q) / 256, i, l;
    int32_t oz = gz, ow = gw, oh = gh, ocx = gcx, y0 = (-cv_oy) / 2 - 1, y1 = y0 + 62;
    gz = 2, gw = gh = 120, gcx = ocx - t / 6;           /* (the nebula: drifting by, slowly) */
    g_nebula(y0, y1);
    gcx = ocx;
    {   /* the planet: big at the lower right, falling away */
        int32_t r = 62 - e * 36 / 256, cx = 96 + e * 10 / 256, cy = 86 + e * 6 / 256;
        g_planet_draw(&g.pl, cx, cy, r, y0, y1);
    }
    gz = oz, gw = ow, gh = oh;
    for (l = 0; l < 3; l++)                             /* the stars: to the right, faster the nearer */
        for (i = 0; i < 22; i++) {
            uint32_t h = g_hash(i, l, 0xF1Bu);
            int32_t sp = (l + 1) * (l + 1) * 22 + (q > 200 ? (q - 200) * (l + 1) * 6 : 0);   /* px / s (the dash at the end) */
            int32_t x = ((int32_t)(h % 260u) + t * sp / 1000) % 260 - 10, y = (int32_t)((h >> 9) % 240u), len = 1 + sp / 40;
            cv_rect(x - len, y, len, 1, SC[l][(h >> 20) & 3u]);
        }
#if NMS_NAVE_LADO
    {   /* the ship, bobbing; its engine's light streaming back; at the end it shoots off to the left */
        static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
        int32_t dash = q > 200 ? (q - 200) * (q - 200) / 6 : 0, ox = 70 - NMS_NL_W / 2 - dash - e * 12 / 256;
        int32_t oy = 96 - NMS_NL_H / 2 + ((g_sin((uint32_t)t / 2u) * 3) >> 15), u, v, k, n = 14 + (int32_t)((fm1_ms / 40u) % 5u) + dash / 2;
        for (k = 0; k < 5; k++) {                       /* the engine's light (the blue nozzle, two thirds along) */
            int32_t ey = oy + NMS_NL_H * 55 / 100 - 2 + k;
            cv_rect(ox + NMS_NL_W * 72 / 100, ey, n - (k == 0 || k == 4 ? n / 2 : 0), 1, k == 2 ? NW : k == 1 || k == 3 ? NB : EN);
        }
        for (v = 0; v < NMS_NL_H; v++)
            for (u = 0; u < NMS_NL_W; u++) {
                uint32_t idx = (uint32_t)(v * NMS_NL_W + u), c = (NMS_NL[idx >> 1] >> ((idx & 1u) ? 0u : 4u)) & 15u;
                if (c < 10u)
                    cv_rect(ox + u, oy + v, 1, 1, PAL[c]);
            }
    }
#else
    g_ship_px(70 - (q > 200 ? (q - 200) * (q - 200) / 6 : 0), 96, 512u, 40, 2);
#endif
    if (q > 236)                                        /* gone: a flash */
        cv_rect(0, 0, 240, 240, (q & 2) ? NW : NB);
}
static void g_takeoff_draw(uint32_t el)
{
    int32_t t = (int32_t)el - G_TO_SIDE, i, hy, sx = 120, sy, L;
    uint16_t sky = GCOL[g.pl.water & 7u], land = GCOL[g.pl.land & 7u];
    if (t < 0) {
        g_side_draw((int32_t)el);
        return;
    }
    if (t >= G_TO_REAR) {
        g_fly_draw(t - G_TO_REAR);
        return;
    }
    /* the sky: this planet's, deep blue at the top, hazy toward the horizon; darker and darker as it climbs */
    {
        int32_t dark = t < 700 ? 0 : t < 2300 ? (t - 700) * 16 / 1600 : 16, y, x;
        hy = t < 700 ? 150 : 150 + g_ein((t - 700) * 256 / 1600 > 256 ? 256 : (t - 700) * 256 / 1600) * 220 / 256;
        for (y = 0; y < (hy < 240 ? hy : 240); y += 2) {
            int32_t up = (hy - y) * 16 / (hy + 1);           /* 0 at the horizon .. 16 at the top */
            for (x = 0; x < 240; x += 2) {
                int32_t lv = up + dark, b = BAYER[(y >> 1) & 3][(x >> 1) & 3];
                uint16_t c;
                if (lv < 3)
                    c = b < 6 ? NW : sky;                    /* haze */
                else if (lv < 11)
                    c = sky;
                else if (lv < 16)
                    c = b < (lv - 10) * 3 ? EN : sky;        /* into deep blue */
                else if (lv < 24)
                    c = b < (lv - 15) * 2 ? NK : EN;         /* into the night */
                else
                    c = NK;
                if (c != NK)
                    cv_rect(x, y, 2, 2, c);
            }
        }
        if (t > 1200)                                        /* the first stars, as it gets dark */
            for (x = 0; x < 30; x++) {
                uint32_t h = g_hash(x, 4, 0x5Au);
                int32_t yy = (int32_t)((h >> 8) % 240u);
                if (yy < hy - 10)
                    cv_pset((int32_t)(h % 240u), yy, NW);
            }
    }
    if (hy < 240) {                                      /* far hills on the horizon, then the ground: grass tufts that grow
                                                         * toward us and run down past */
        int32_t y, x;
        for (x = 0; x < 240; x += 2) {
            int32_t hh = 4 + ((g_vnoise(x * 3, 7, g.pl.seed, 5, 0) - 64) * 14 >> 8);
            if (hh > 0)
                cv_rect(x, hy - hh, 2, hh, EN);
        }
        for (y = hy; y < 240; y++) {
            int32_t d = y - hy + 1;
            cv_rect(0, y, 240, 1, (y & 1) && d < 6 ? EN : land);
        }
        for (i = 0; i < 90; i++) {                       /* tufts: z from far to near, sliding down as the camera rises */
            uint32_t h = g_hash(i, 6, g.pl.seed);
            int32_t z = 40 + (int32_t)((h % 1000u + (uint32_t)(t > 700 ? (t - 700) / 3 : 0)) % 1000u), ty, tx, sz;
            ty = hy + 6000 / (1040 - z) - 5;
            if (ty >= 240 || ty <= hy)
                continue;
            sz = 1 + (ty - hy) / 14;
            tx = (int32_t)((h >> 10) % 280u) - 20;
            tx = 120 + (tx - 120) * (ty - hy + 20) / 60;
            cv_rect(tx, ty - sz * 2, sz, sz * 2, (h >> 20) % 3u == 0u ? EY : (h >> 20) % 3u == 1u ? EN : NK);   /* (on this */
            cv_rect(tx + sz, ty - sz, sz, sz, (h >> 20) % 2u ? NK : EY);                                       /* planet's ground) */
        }
    }
    /* the ship: on the ground, waking; lifting; flying up and away */
    L = 48;
    if (t < 700)
        sy = 180 + (int32_t)((fm1_ms / 50u) & 1u);
    else if (t < 2300)
        sy = 180 - g_ein((t - 700) * 256 / 1600) * 70 / 256, L = 48 - (t - 700) * 12 / 1600;
    else
        sy = 110, L = 36;
    /* the clouds: puffs rushing out from the middle as it climbs (perspective: 1/z) */
    if (t > 900 && t < 3200) {
        for (i = 0; i < 14; i++) {
            uint32_t h = g_hash(i, 2, 0xC1Du);
            int32_t z = 1000 - (int32_t)(((uint32_t)t * 2u + (h % 1000u)) % 1000u), a = (int32_t)(h >> 10) & 1023, r, cx, cy, rr;
            if (z < 60)
                continue;
            r = 9000 / z, cx = 120 + ((g_cos((uint32_t)a) >> 7) * r >> 8) * 2, cy = 120 + ((g_sin((uint32_t)a) >> 7) * r >> 8) * 2;
            rr = 4 + 2400 / z;
            {
                int32_t yy, xx;
                for (yy = -rr / 2; yy <= rr / 2; yy += 2)
                    for (xx = -rr; xx <= rr; xx += 2)
                        if (xx * xx / 4 + yy * yy <= rr * rr / 4 && ((xx + yy + i) & 2) == 0)
                            cv_rect(cx + xx, cy + yy, 2, 2, z < 300 ? NW : EN);
            }
        }
    }
    /* out of the air: the stars streaking from the middle (fast), then slowing to dots */
    if (t > 2000) {
        int32_t sp = t < 3400 ? 400 : t < 4300 ? 400 - (t - 3400) * 390 / 900 : 10;
        for (i = 0; i < 70; i++) {
            uint32_t h = g_hash(i, 8, 0x57A1u), a = h & 1023u;
            int32_t r0 = (int32_t)(((h >> 10) % 170u + (uint32_t)t * (uint32_t)sp / 600u) % 170u), len = 1 + sp * r0 / 1800;
            uint16_t c = (h >> 20) % 5u == 0u ? EY : (h >> 20) % 5u == 1u ? NB : NW;
            cv_line(120 + ((g_cos(a) * r0) >> 15), 120 + ((g_sin(a) * r0) >> 15),
                    120 + ((g_cos(a) * (r0 + len)) >> 15), 120 + ((g_sin(a) * (r0 + len)) >> 15), c);
        }
    }
    if (t < 400)                                         /* the engines waking: dust */
        for (i = 0; i < 20; i++) {
            uint32_t h = g_hash(i, (int32_t)(fm1_ms / 60u), 0xD057u);
            cv_rect(sx - 60 + (int32_t)(h % 120u), sy + 6 + (int32_t)((h >> 8) % 14u), 2, 2, (h >> 16) & 1u ? NO : EY);
        }
    {   /* (the user) the ship from the 3D model: its nose rising as it lifts (six frames), then back down to the view from
         * behind (3/4); the engines' sparks under it */
        int32_t f = t < 700 ? 0 : t < 1600 ? (t - 700) * NMS_ND_N / 900 : t < 2500 ? NMS_ND_N - 1 - (t - 1600) * NMS_ND_N / 900 : 0, bx = sx + (t > 700 && t < 2300 ? (int32_t)((fm1_ms / 40u) % 3u) - 1 : 0);
        g_ship_decola(bx, sy, L, f);
        if (t > 300)
            for (i = 0; i < 8; i++) {
                uint32_t hh = g_hash(i, (int32_t)(fm1_ms / 60u), 0x5Eu);
                cv_rect(bx - L / 2 + (int32_t)(hh % (uint32_t)(L + 1)), sy + L / 3 + (int32_t)((hh >> 8) % 8u), 1 + (int32_t)((hh >> 20) & 1u), 1, (hh >> 16) & 1u ? NB : EY);
            }
    }
}

/* ------------------------------------------------------------ the station --- */
/* docked: 6 rows (sell blue, green, purple, orange; the next ship; leave), the one picked in a texture of its colour */
static const uint16_t G_ENG_PR[3] = {120, 300, 700}, G_WPN_PR[4] = {80, 220, 500, 900};
/* (the user) the station as a hangar: every ship side by side on its pad (all five, or three at a time: KNOB 3), the
 * SELECT knob turning them round in a loop with a swing; the one in the middle has the upgrades round it, boxes over the
 * hangar with a line to the part (the engine to its tail, the weapon to its nose) and a bar like the FX lab's; selling
 * along the bottom. KNOB 1 walks the boxes (DK_ORD), B does the one picked (g_dock_do) */
static const uint8_t DK_ORD[8] = {5, 6, 4, 0, 1, 2, 3, 7};   /* (KNOB 1's order: engine, weapon, the ship, sell x 4, leave) */
static int32_t dk_from, dk_to;                          /* the carousel (Q8 ships, unwound) */
static uint32_t dk_t;
static int32_t g_eback(int32_t q)                       /* the swing: past the end a little and back (back-out) */
{
    int32_t t = q - 256, t2 = t * t / 256, t3 = t2 * t / 256;
    return q >= 256 ? 256 : 256 + t3 * 27 / 10 + t2 * 17 / 10;
}
static int32_t dk_pos(void)                             /* (once a frame: every band of it the same) */
{
    static uint32_t fr = 0xFFFFFFFFu;
    static int32_t at;
    uint32_t e = fm1_ms - dk_t;
    if (fr == g.frame && e < 600u)
        return at;
    fr = g.frame;
    at = e >= 520u ? dk_to : dk_from + (dk_to - dk_from) * g_eback((int32_t)e * 256 / 520) / 256;
    return at;
}
static void g_dock_turn(int32_t s)                      /* the ships one along (SELECT, or KNOB 2 on them) */
{
    dk_from = dk_pos(), dk_to += s > 0 ? 256 : -256, dk_t = fm1_ms;
    g.pick = (uint8_t)((((dk_to >> 8) % 5) + 5) % 5);
}
static void dk_box(int32_t x, int32_t y, int32_t w, int32_t h, int on, uint16_t c)   /* a box over the hangar: a mesh */
{
    n_tex(x, y, w, h, PT_CHECK, NK);
    cv_rect(x + 1, y + 1, w - 2, 11, NK);
    n_frame(x, y, w, h, 1, on ? c : EN);
    if (on)
        n_frame(x - 2, y - 2, w + 4, h + 4, 1, NW);
}
static void dk_wire(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* a line from a box to a part: */
{
    int32_t xm = x0 < x1 ? x0 + 10 : x0 - 10;          /* out level, then across, a dot on the part */
    cv_line(x0, y0, xm, y0, c), cv_line(xm, y0, x1, y1, c);
    cv_rect(x1 - 1, y1 - 1, 3, 3, NW), cv_rect(x0 - 1, y0 - 1, 3, 3, c);
}
static void g_dock(void)
{
    static const uint16_t RC[4] = {NB, NG, NP, NO};
    char b[20];
    int32_t i, x, y, z, pos, sp, kc, yc, now = (int32_t)fm1_ms;
    if ((((dk_to >> 8) % 5) + 5) % 5 != g.pick)         /* (just docked: the ship you fly in the middle) */
        dk_to = dk_from = g.pick * 256;
    pos = dk_pos(), z = 0;                              /* (the user) three at a time, no zoom */
    /* the hangar: the back wall in panels, lamps and their light, the floor in perspective */
    cv_rect(0, 20, 240, 112, EN);
    for (x = 0; x < 240; x += 40)
        cv_rect(x, 20, 2, 112, NK), cv_rect(x + 18, 60, 4, 72, RGB(0x10, 0x1C, 0x40));
    cv_rect(0, 46, 240, 3, NK);
    for (x = 20; x < 240; x += 80) {
        cv_rect(x - 6, 22, 12, 3, NW);
        for (y = 26; y < 70; y += 2)
            for (i = -(y - 22) / 3; i <= (y - 22) / 3; i += 4)
                cv_pset(x + i + (y & 2), y, RGB(0x30, 0x44, 0x80));
    }
    cv_rect(0, 132, 240, 108, NK);
    for (i = -6; i <= 6; i++)
        cv_line(120 + i * 14, 132, 120 + i * 70, 240, RGB(0x14, 0x20, 0x44));
    for (i = 1; i < 7; i++)
        cv_rect(0, 132 + i * i * 2, 240, 1, RGB(0x14, 0x20, 0x44));
    /* the ships on their pads, the middle one the picked */
    sp = 84 - (84 - 46) * z / 256, kc = z < 128 ? 5 : 3, yc = z < 128 ? 114 : 120;
    for (i = 0; i < 5; i++) {
        int32_t d = ((i * 256 - pos) % 1280 + 1280 + 640) % 1280 - 640, mid = d > -128 && d < 128, k = mid ? kc : 3;
        uint32_t own = (g.own >> i) & 1u || i == 0;
        uint16_t c = (uint32_t)i == g.cls ? EG : own ? NB : RGB(0x50, 0x58, 0x70);
        x = 120 + d * sp / 256;
        if (x < -30 || x > 270)
            continue;
        y = yc + (mid ? 0 : 6) + (mid ? (int32_t)((fm1_ms / 400u) & 1u) : 0);
        {   /* its pad: a flat ring, lit under the picked one */
            int32_t a, rw = k * 8, rh = k * 2;
            for (a = 0; a < 1024; a += 16)
                cv_pset(x + ((g_cos((uint32_t)a) * rw) >> 15), y + k * 7 + ((g_sin((uint32_t)a) * rh) >> 15), mid ? NB : RGB(0x20, 0x30, 0x60));
            if (mid)
                n_tex(x - rw + 4, y + k * 7 - rh / 2, rw * 2 - 8, rh, PT_DOTS, NB);
        }
        g_hull(x, y, (uint32_t)i, k, c);
        if (z >= 128 || !mid) {                         /* (its number under it) */
            b[0] = (char)('1' + i), b[1] = 0;
            n_texto(x - 3, y + k * 7 + 6, b, 1, (uint32_t)i == g.pick ? NW : NP);
        }
    }
    {   /* the picked one: its name, flying / use / price */
        uint32_t k = g.pick % 5u;
        str_cpy(b, "< SHIP ", 8), b[7] = (char)('1' + k), b[8] = 0, str_cpy(b + 8, " >", 3);
        if (g.row == 4)
            n_tex(70, 152, 100, 30, PT_GRID, NB), n_frame(70, 152, 100, 30, 1, NW);
        n_texto(120 - n_w(b, 1) / 2, 153, b, 1, NW);
        if (k == g.cls)
            str_cpy(b, "FLYING", 8);
        else if (((g.own >> k) & 1u) || !k)
            str_cpy(b, "USE", 8);
        else
            fmt_int(b, G_CLASS[k].price);
        n_texto(120 - n_w(b, 1) / 2, 166, b, 1, k == g.cls ? EG : (((g.own >> k) & 1u) || !k || g.cred >= G_CLASS[k].price) ? NW : NO);
    }
    /* the upgrades round it: engine (left, to the tail), weapon (right, to the nose) */
    for (i = 0; i < 2; i++) {
        uint32_t lv = i ? g.wpn : g.eng, mx = i ? 4u : 3u, pr = i ? G_WPN_PR[lv < 4u ? lv : 3u] : G_ENG_PR[lv < 3u ? lv : 2u];
        int on = g.row == 5 + i;
        uint16_t c = i ? NG : NO;
        int32_t bx = i ? 168 : 4, by = 40, wv = (now / 60) % 4;
        dk_box(bx, by, 68, 40, on, c);
        n_texto(bx + 4, by + 2, i ? "WEAPON" : "ENGINE", 1, NW);
        n_knob(bx + 4, by + 16, 60, 10, (int32_t)(lv * 1000u / mx), BK_GROW, NW, c);
        if (lv >= mx)
            str_cpy(b, "MAX", 8);
        else
            fmt_int(b, pr);
        n_texto(bx + 64 - n_w(b, 1), by + 26, b, 1, lv >= mx ? NP : g.cred >= pr ? NW : NO);
        b[0] = 'L', b[1] = (char)('0' + lv), b[2] = 0;
        n_texto(bx + 4, by + 26, b, 1, NP);
        if (i)
            dk_wire(bx, by + 20, 120, yc - kc * 5 - 1, on ? c : NW);
        else
            dk_wire(bx + 68, by + 20, 120 + (wv & 1) - 1 + kc, yc + kc * 4, on ? c : NW);
    }
    /* LEAVE: top, between the two */
    dk_box(92, 40, 56, 18, g.row == 7, NB);
    n_texto(120 - n_w("LEAVE", 1) / 2, 42, "LEAVE", 1, NW);
    /* selling, along the bottom, under its title */
    cv_rect(4, 189, 232, 1, EN);
    cv_rect(120 - n_w("SELL CARGO", 1) / 2 - 4, 183, n_w("SELL CARGO", 1) + 8, 12, NK);
    n_texto(120 - n_w("SELL CARGO", 1) / 2, 182, "SELL CARGO", 1, NP);
    for (i = 0; i < 4; i++) {
        int32_t pr = g_price(g.stx, g.sty, (uint32_t)i), cx = 4 + i * 59;
        int on = g.row == i;
        dk_box(cx, 197, 55, 19, on, RC[i]);
        cv_rect(cx + 4, 201, 10, 10, RC[i]), cv_rect(cx + 4, 201, 10, 2, NW);
        b[0] = '+', fmt_int(b + 1, g.res[i] * pr);
        n_texto(cx + 51 - n_w(b, 1), 199, b, 1, g.res[i] ? NW : NP);
    }
    n_texto(8, 24, "HANGAR", 1, NW);
    x = n_w("CR ", 1);
    fmt_int(b, g.cred);
    n_texto(232 - n_w(b, 1) - x, 24, "CR", 1, NO);
    n_texto(232 - n_w(b, 1), 24, b, 1, NW);
}
/* B on a row */
static void g_dock_do(void)
{
    if (g.row < 4) {
        int32_t k = g.row, n = g.res[k];
        if (!n)
            return;
        g.cred = (uint16_t)clamp(g.cred + n * g_price(g.stx, g.sty, (uint32_t)k), 0, 60000);
        g.res[k] = 0;
        g.dirty = 1;
    } else if (g.row == 4) {                            /* a ship: fly the one owned, or buy it */
        uint32_t k = g.pick % 5u;
        if (k == g.cls)
            return;
        if (k && !((g.own >> k) & 1u)) {
            if (g.cred < G_CLASS[k].price) {
                ui_message("NOT ENOUGH CREDITS");
                return;
            }
            g.cred = (uint16_t)(g.cred - G_CLASS[k].price), g.own |= (uint8_t)(1u << k);
            ui_message("NEW SHIP");
        } else {
            ui_message("SHIP CHANGED");
        }
        g.cls = (uint8_t)k;
        if (g.fuel > G_CLASS[k].tank)
            g.fuel = G_CLASS[k].tank;
        else if (g.fuel < G_CLASS[k].tank / 2)
            g.fuel = G_CLASS[k].tank / 2;
        g.dirty = 1;
    } else if (g.row == 5 || g.row == 6) {              /* an engine / a weapon level */
        uint8_t *lv = g.row == 5 ? &g.eng : &g.wpn;
        uint32_t mx = g.row == 5 ? 3u : 4u, pr;
        if (*lv >= mx)
            return;
        pr = g.row == 5 ? G_ENG_PR[*lv] : G_WPN_PR[*lv];
        if (g.cred < pr) {
            ui_message("NOT ENOUGH CREDITS");
            return;
        }
        g.cred = (uint16_t)(g.cred - pr), (*lv)++;
        ui_message(g.row == 5 ? "ENGINE UP" : "WEAPON UP");
        g.dirty = 1;
    } else {
        int32_t x, y;
        g_station(g.stx, g.sty, &x, &y);
        g.sx = (x << 8) + (g_cos(g.ang) >> 7) * 16, g.sy = (y << 8) + (g_sin(g.ang) >> 7) * 16;
        g.vx = g.vy = 0, g.thr = 0;
        g.mode = GM_SPACE, g.t_mode = g.t_fade = fm1_ms;
    }
}

/* ------------------------------------------------------------- the save --- */
/* 44 bytes appended to the free flash sector 0xFB000 (fm1_flash.h FL_GAME): 64 records of 64 B, the valid one with
 * the highest sequence wins; the sector is erased only when full, and only while nothing plays (an erase stops the
 * audio ~50 ms). Written at most every 8 s, after a landing, a take-off, a warp, a trade, or 30 s of flight */
#define G_SAVE_AT 0xFB000u
#define G_SAVE_N 64u
#define G_SAVE_MAGIC 0x47534D4Eu                        /* "NMSG" */
typedef struct {
    uint32_t magic, seq;
    int32_t sx, sy;                                     /* the ship (world px) */
    int16_t psx, psy, px, py, lx, ly;                   /* on foot: the planet's sector; you, the ship (surface px) */
    uint16_t cred, warps;
    uint8_t res[4];
    uint8_t ang, cls, fuel, flags;                      /* ang / 4; flags: 1 on foot, 2 the centre reached, pk << 4 */
    uint32_t crc;
} g_save_t;
_Static_assert(sizeof(g_save_t) == 44u, "the game's save is 44 B");
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);
static uint32_t st_crc32(const void *p, uint32_t n);
static int audio_quiet(void);

static void g_load(void)                                /* the newest valid record, if any; g.slot: the next one */
{
    g_save_t r, best;
    uint32_t i, bi = G_SAVE_N, w[4];
    if (!flash_ok)
        return;
    for (i = 0; i < G_SAVE_N; i++)
        if (!st_read(G_SAVE_AT + i * 64u, &r, sizeof r) && r.magic == G_SAVE_MAGIC &&
            r.crc == st_crc32(&r, sizeof r - 4u) && (bi == G_SAVE_N || r.seq > best.seq))
            best = r, bi = i;
    g.slot = (uint8_t)(bi == G_SAVE_N ? 0u : bi + 1u);
    if (g.slot < G_SAVE_N && (st_read(G_SAVE_AT + g.slot * 64u, w, sizeof w) || (w[0] & w[1] & w[2] & w[3]) != 0xFFFFFFFFu))
        g.slot = G_SAVE_N;                              /* not blank: erase before the next save */
    if (bi == G_SAVE_N || (best.cls & 7u) > 4u)
        return;
    g.seq = best.seq;
    g.sx = best.sx << 8, g.sy = best.sy << 8, g.svx = best.sx, g.svy = best.sy;
    g.ang = (uint32_t)best.ang << 2;
    g.cls = best.cls & 7u, g.eng = (best.cls >> 3) & 3u, g.wpn = best.cls >> 5;
    g.own = (uint8_t)(1u | ((best.flags >> 1) & 6u) | ((best.flags >> 3) & 24u));
    {   /* (a save from before: every class up to the one flown) */
        uint32_t q;
        for (q = 0; q <= g.cls; q++)
            g.own |= (uint8_t)(1u << q);
    }
    g.fuel = best.fuel > G_CLASS[g.cls].tank ? G_CLASS[g.cls].tank : best.fuel;
    g.flags = best.flags & 2u;
    g.cred = best.cred, g.warps = best.warps;
    for (i = 0; i < 4u; i++)
        g.res[i] = best.res[i];
    if ((best.flags & 1u) && g_planet(best.psx, best.psy, (uint32_t)(best.flags >> 4) & 3u, &g.pl)) {
        g.psx = best.psx, g.psy = best.psy, g.pk = (uint8_t)((best.flags >> 4) & 3u);
        g.px = g.tx = best.px << 8, g.py = g.ty = best.py << 8;
        g.lx = best.lx, g.ly = best.ly;
        g_sen_init();
        g.sx = (g.pl.x + G_PR(g.pl) + 24) << 8, g.sy = g.pl.y << 8;   /* (the user: it always starts in space, by it) */
    }
}

static void g_save_tick(void)
{
    g_save_t r;
    uint32_t now = fm1_ms, foot = g.mode == GM_SURF;
    if (g.mode == GM_SPACE && now - g.t_save > 30000u &&
        ((g.sx >> 8) - g.svx) * ((g.sx >> 8) - g.svx) + ((g.sy >> 8) - g.svy) * ((g.sy >> 8) - g.svy) > 64 * 64)
        g.dirty = 1;                                    /* 30 s of flight */
    if (!g.dirty || !flash_ok || now - g.t_save < 8000u || !(foot || g.mode == GM_SPACE || g.mode == GM_DOCK))
        return;
    if (g.slot >= G_SAVE_N) {                           /* full: an erase, only while nothing plays */
        if (song.playing || transport_req || !audio_quiet())
            return;
        g.t_save = now;                                 /* (a failed erase: again in 8 s) */
        if (st_erase(G_SAVE_AT))
            return;
        g.slot = 0;
    }
    memset(&r, 0, sizeof r);
    r.magic = G_SAVE_MAGIC;
    r.seq = ++g.seq;
    r.sx = g.sx >> 8, r.sy = g.sy >> 8;
    r.psx = g.psx, r.psy = g.psy;
    r.px = (int16_t)(g.px >> 8), r.py = (int16_t)(g.py >> 8), r.lx = (int16_t)g.lx, r.ly = (int16_t)g.ly;
    r.cred = g.cred, r.warps = g.warps;
    r.res[0] = (uint8_t)g.res[0], r.res[1] = (uint8_t)g.res[1], r.res[2] = (uint8_t)g.res[2], r.res[3] = (uint8_t)g.res[3];
    r.ang = (uint8_t)(g.ang >> 2), r.cls = (uint8_t)(g.cls | g.eng << 3 | g.wpn << 5), r.fuel = g.fuel;
    r.flags = (uint8_t)((foot ? 1u : 0u) | (g.flags & 2u) | (uint32_t)g.pk << 4 | (uint32_t)(g.own & 6u) << 1 | (uint32_t)(g.own & 24u) << 3);
    r.crc = st_crc32(&r, sizeof r - 4u);
    g.t_save = now;
    if (st_prog(G_SAVE_AT + g.slot * 64u, &r, sizeof r)) {
        g.slot = G_SAVE_N;                              /* (erase and try again) */
        return;
    }
    g.slot++;
    g.dirty = 0;
    g.svx = r.sx, g.svy = r.sy;
}

/* ------------------------------------------------------------- the HUD --- */
static int32_t g_hud_frac(int32_t x, int32_t a, int32_t b)   /* "a/b" at the bottom; the end x */
{
    char t[12];
    fmt_int(t, a);
    x = n_texto(x, 218, t, 1, NW);
    x = n_texto(x, 218, "/", 1, NP);
    fmt_int(t, b);
    return n_texto(x, 218, t, 1, NW);
}
/* the music's head on top: tempo, play / rec, the track and its sound; the resources at the bottom (on the map:
 * the fuel and the jump; docked: the cargo and the fuel) */
static void g_hud(void)
{
    char b[8];
    uint32_t k;
    int32_t x;
    if (g.mode == GM_SPACE)
        g_mk_text();
    if (g.mode == GM_SURF)
        g_sen_text();
    if (ui.msg_t) {
        n_msg(0, 26);
    } else {
        fmt_int(b, song.g[G_BPM]);
        x = n_texto(4, 4, b, 1, NW);
        if (song.rec)
            n_disc(x + 10, 12, 5, NO);
        else if (song.playing) {
            int32_t i;
            for (i = 0; i < 10; i++)
                cv_rect(x + 6 + i, 7 + i / 2, 1, 11 - i, NW);
        }
        if (song.playing)                               /* the beat */
            for (k = 0; k < 4u; k++)
                cv_rect(x + 22 + (int32_t)k * 6, 16, 4, 4, clk_beat % 4u == k ? NW : NP);
        cv_rect(206, 4, 18, 18, NCOL[song.sel & 3u]);   /* the track, its colour; its sound's name (the drums: the kit) */
        b[0] = (char)('1' + song.sel), b[1] = 0;
        n_text(211, 5, b, 1, NW);
        {
            const char *role, *name = rw_name(song.sel, &role);
            n_texto(200 - n_w(name, 1), 4, name, 1, NW);
        }
    }
    if (g.mode == GM_RACE && rc_space)                  /* (the space race: its dashboard says it all) */
        return;
    if (g.mode == GM_MAP) {                             /* the map (Low Mem Sky's): how far the centre is on top; the
                                                         * system picked at the bottom; here, or the jump and the fuel */
        int32_t sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8), j = g_jump(g.mx - sx, g.my - sy), np = 0, qx, qy;
        int32_t dx = G_CX - g.mx, dy = G_CY - g.my;
        char t[24];
        planet_t p;
        str_cpy(t, "CORE ", sizeof t);                  /* light years: ~1000 a sector */
        fmt_int(t + 5, g_isqrt(dx * dx + dy * dy) * 1000 + (int32_t)(g_hash(g.mx, g.my, 3u) % 1000u));
        str_cpy(t + str_len(t), " LY", 4);
        n_texto(120 - n_w("GALACTIC MAP", 1) / 2, 24, "GALACTIC MAP", 1, EY);   /* where you are: the map */
        n_texto(4, 44, t, 1, NP);
        n_texto(120 - n_w("PRESS HOME TO EXIT WARP", 1) / 2, 166, "PRESS HOME TO EXIT WARP", 1, (fm1_ms / 600u) & 1u ? NW : NP);
        g_sys_name(g.mx, g.my, t);
        str_cpy(t + str_len(t), " SYSTEM", 8);
        n_texto(4, 186, t, 1, NW);
        while (np < (int32_t)G_NPL && g_planet(g.mx, g.my, (uint32_t)np, &p))
            np++;
        fmt_int(t, np);
        str_cpy(t + str_len(t), np == 1 ? " PLANET" : " PLANETS", 9);
        if (g_station(g.mx, g.my, &qx, &qy))
            str_cpy(t + str_len(t), " +STATION", 10);
        n_texto(4, 204, t, 1, NW);
        if (!j) {
            n_texto(4, 222, "YOU ARE HERE", 1, ER);
        } else {
            str_cpy(t, "JUMP ", sizeof t);
            fmt_int(t + 5, j);
            n_texto(4, 222, t, 1, j <= G_CLASS[g.cls].range && j <= g_fuel_can() ? NW : ER);
            cv_rect(130, 226, 9, 9, NO);
            g_hud_frac(143, g.fuel, G_CLASS[g.cls].tank);
        }
        return;
    }
    if (g.mode == GM_DOCK) {                            /* docked: the picked ship's figures */
        {
            static const char *const L[3] = {"SPD", "CRG", "WRP"};
            const uint8_t v[3] = {(uint8_t)(G_CLASS[g.pick % 5u].speed + g.eng * 2), G_CLASS[g.pick % 5u].cargo, G_CLASS[g.pick % 5u].range};
            for (k = 0; k < 3u; k++) {
                x = n_texto(4 + (int32_t)k * 80, 218, L[k], 1, NP) + 4;
                fmt_int(b, v[k]);
                n_texto(x, 218, b, 1, NW);
            }
            return;
        }
        x = n_texto(4, 218, "CARGO", 1, NP) + 4;
        g_hud_frac(x, g_cargo(), G_CLASS[g.cls].cargo);
        cv_rect(140, 222, 9, 9, NO);
        g_hud_frac(153, g.fuel, G_CLASS[g.cls].tank);
        return;
    }
    if (g.mode == GM_SPACE) {                           /* the speed, bottom right: u/s (px / s x 10), its colour the mode's */
        char s[12];
        int32_t v = g_isqrt((g.vx >> 8) * (g.vx >> 8) + (g.vy >> 8) * (g.vy >> 8)) * 10, w;
        uint16_t c = g.pulse_on ? NO : g.boost_on ? EY : NW;
        fmt_int(s, v);
        w = n_w(" U/S", 1);
        n_texto(236 - w, 198, " U/S", 1, NP);
        n_texto(236 - w - n_w(s, 1), 198, s, 1, c);
    }
    for (k = 0; k < 4u; k++) {                          /* what you carry */
        x = 4 + (int32_t)k * 58;
        cv_rect(x, 222, 9, 9, NCOL[k]);
        fmt_int(b, g.res[k]);
        n_texto(x + 13, 218, b, 1, NW);
    }
    {   /* the fuel: a bar that grows, orange over a purple track, along the bottom edge */
        int32_t w = 232 * g.fuel / G_CLASS[g.cls].tank;
        cv_rect(4, 236, 232, 1, NP);
        cv_rect(4, 235, w, 3, NO);
    }
}

/* ------------------------------------------------------------- the logo --- */
/* the title in a thin line font of our own (strokes in a 6 x 10 box, x-height from 4; y descends to 13) */
static const char G_GLYPHS[] = "NoMankSyF";
static const int8_t G_STROKE[][12] = {
    {0, 10, 0, 0, 0, 0, 5, 10, 5, 10, 5, 0},            /* N */
    {0, 5, 0, 9, 1, 4, 4, 4, 5, 5, 5, 9},               /* o (+ the bottom below) */
    {0, 10, 1, 0, 1, 0, 3, 6, 3, 6, 5, 0},              /* M (+ the last leg) */
    {1, 4, 4, 4, 5, 5, 5, 10, 1, 7, 4, 7},              /* a (+ bowl) */
    {0, 4, 0, 10, 1, 4, 4, 4, 5, 5, 5, 10},             /* n */
    {0, 0, 0, 10, 0, 7, 4, 4, 1, 7, 4, 10},             /* k */
    {1, 0, 5, 0, 0, 1, 0, 4, 1, 5, 4, 5},               /* S (+ the lower half) */
    {0, 4, 0, 8, 1, 9, 4, 9, 5, 4, 5, 12},              /* y (+ the tail) */
    {0, 0, 0, 10, 0, 0, 5, 0, 0, 5, 4, 5},              /* F */
};
static const int8_t G_MORE[][4] = {                      /* the fourth stroke of some glyphs */
    {-1, 0, 0, 0}, {1, 10, 4, 10}, {5, 0, 6, 10}, {0, 8, 0, 9}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {5, 6, 5, 9}, {1, 13, 4, 13},
    {-1, 0, 0, 0},
};
/* text in it at (x, y), each unit u px (Q8), strokes 2 px; returns the end x */
static int32_t g_title(int32_t x, int32_t y, const char *s, int32_t u, uint16_t c)
{
    for (; *s; s++) {
        uint32_t gi;
        int32_t k;
        if (*s == ' ') {
            x += 5 * u >> 8;
            continue;
        }
        for (gi = 0; G_GLYPHS[gi] && G_GLYPHS[gi] != *s; gi++)
            ;
        if (!G_GLYPHS[gi])
            continue;
        for (k = 0; k < 4; k++) {
            const int8_t *q = k < 3 ? &G_STROKE[gi][k * 4] : G_MORE[gi];
            int32_t ax, ay, bx, by;
            if (q[0] < 0)
                continue;
            ax = x + (q[0] * u >> 8), ay = y + (q[1] * u >> 8), bx = x + (q[2] * u >> 8), by = y + (q[3] * u >> 8);
            cv_line(ax, ay, bx, by, c), cv_line(ax + 1, ay, bx + 1, by, c), cv_line(ax, ay + 1, bx, by + 1, c);
        }
        x += (*s == 'M' ? 9 : 8) * u >> 8;
    }
    return x;
}

/* the logo screen into the canvas (one half: cv_oy): stars in the four colours, the title, two lines under it;
 * tw: the stars twinkle */
/* the opening, 4 s: the ship from the side (assets/nave-lado.png, turned to face right) flying through space, a lying
   8 seen from the side (a little forward and back, up and down), the stars rushing past in three depths, the nebula,
   a planet going by far behind; the title and the credits. The last half second it eases back, then shoots off to
   the right. In screen px */
#include "nms_nave_lado.h"
static void g_open_draw(int32_t t)
{
    static const uint16_t SC[3][4] = {{NP, EN, NB, NP}, {NB, NW, NP, EY}, {NW, EY, NW, ER}};
    int32_t i, l, oz = gz, ow = gw, oh = gh, ocx = gcx, y0 = (-cv_oy) / 2 - 1, y1 = y0 + 62, sx, sy, dash = 0, slow = 256;
    if (t > 3500)                                       /* the last half second: easing back, then away */
        slow = t < 3700 ? 256 - (t - 3500) * 96 / 200 : 160 + (t - 3700) * 6;
    uint8_t ofade = g.fade;
    g.fade = 16;                                        /* (the opening's own: not the game's fade-in) */
    gz = 2, gw = gh = 120, gcx = ocx + t / 5;           /* the nebula drifting by */
    g_nebula(y0, y1);
    gcx = ocx;
    {   /* a planet far behind, crossing slowly, with its ring */
        planet_t p;
        p.r = 20, p.seed = 0x5EEDu | 1u, p.water = 0, p.land = 4, p.land2 = 1, p.ring = 1 + 3;   /* (an orange ring) */
        g_planet_draw(&p, 132 - t / 50, 66, 20, y0, y1);   /* (clear of the credits) */
    }
    gz = oz, gw = ow, gh = oh;
    g.fade = ofade;
    for (l = 0; l < 3; l++)                             /* the stars: to the left, fast, faster the nearer */
        for (i = 0; i < 24; i++) {
            uint32_t h = g_hash(i, l, 0x0BE7u);
            int32_t sp = ((l + 1) * (l + 1) * 70 + 40) * slow / 256;   /* px / s */
            int32_t x = 250 - (((int32_t)(h % 270u) + t * sp / 1000) % 270), y = (int32_t)((h >> 9) % 240u), len = 2 + sp / 30;
            cv_rect(x, y, len, 1, SC[l][(h >> 20) & 3u]);
        }
    /* the ship: an 8 on its side (seen from the side: forward and back, up and down) */
    sx = 120 + ((g_sin((uint32_t)t / 3u) * 10) >> 15);
    sy = 116 + ((g_sin((uint32_t)t * 2u / 3u) * 7) >> 15);
    if (t > 3500)
        dash = t < 3700 ? -(t - 3500) * 14 / 200 : -14 + (t - 3700) * (t - 3700) / 300;
    if (t < 1100) {                                     /* it comes in from the left: fast, then softly (expo out) */
        int32_t q = 256 - t * 256 / 1100, e = q * q / 256 * q / 256 * q / 256;
        dash -= e * 200 / 256;
    }
    sx += dash;
#if NMS_NAVE_LADO
    {
        static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
        int32_t ox = sx - NMS_NL_W / 2, oy = sy - NMS_NL_H / 2, u, v, k;
        int32_t n = 12 + (int32_t)((fm1_ms / 40u) % 6u) + (dash > 0 ? dash / 2 : 0), ex = ox + NMS_NL_W * 28 / 100;
        for (k = 0; k < 5; k++)                         /* the engine's light, streaming back (to the left) */
            cv_rect(ex - n + (k == 0 || k == 4 ? n / 2 : 0), oy + NMS_NL_H * 55 / 100 - 2 + k, n - (k == 0 || k == 4 ? n / 2 : 0), 1,
                    k == 2 ? NW : k == 1 || k == 3 ? NB : EN);
        for (v = 0; v < NMS_NL_H; v++)
            for (u = 0; u < NMS_NL_W; u++) {
                uint32_t idx = (uint32_t)(v * NMS_NL_W + (NMS_NL_W - 1 - u)), c = (NMS_NL[idx >> 1] >> ((idx & 1u) ? 0u : 4u)) & 15u;
                if (c < 10u)
                    cv_rect(ox + u, oy + v, 1, 1, PAL[c]);
            }
    }
#else
    g_ship_px(sx, sy, 0u, 40, 2);
#endif
    {   /* the title, the line under it, the credits */
        int32_t u = 2 * 256, w = 0, x, a = t < 1200 ? 0 : t < 1800 ? (t - 1200) * 16 / 600 : 16;   /* (after the ship) */
        const char *tt = "No Man Sky";
        for (x = 0; tt[x]; x++)
            w += tt[x] == ' ' ? 5 : tt[x] == 'M' ? 9 : 8;
        if (a >= 16 || (a > 0 && ((t / 40) & 1))) {
            g_title(110 - (w * u >> 8) / 2, 14, tt, u, NW);
            g_title(110 + (w * u >> 8) / 2 + 6, 26, "FM", 256 + 128, NO);
        }
        if (t > 1700) {
            const char *s = "A SPACE ADVENTURE";
            int32_t nch = (t - 1700) / 45, m;
            char b[20];
            for (m = 0; s[m] && m < nch && m < 19; m++)
                b[m] = s[m];
            b[m] = 0;
            n_texto(120 - n_w(s, 1) / 2, 49, b, 1, NB);  /* (outlined: it reads on the nebula) */
        }
        {
            static const char *const L[3] = {"BASED ON", "PICO-8 GAME LOW MEM SKY", "AND SLOOP"};
            uint32_t k;
            for (k = 0; k < 3u && t > 2200 + (int32_t)k * 150; k++) {
                int32_t y = 176 + (int32_t)k * 20;
                n_texto(120 - n_w(L[k], 1) / 2, y, L[k], 1, k ? EY : NW);
            }
        }
    }
}
static void g_logo(int tw)
{
    uint32_t i;
    for (i = 0; i < 30u; i++) {
        uint32_t h = g_hash((int32_t)i, 3, 0x5A5Au);
        int32_t x = (int32_t)(h % 238u), y = (int32_t)((h >> 9) % 238u);
        uint16_t c = (h >> 20) % 5u == 4u ? NW : NCOL[(h >> 20) % 5u];
        if (tw && ((fm1_ms / 300u + h) % 7u) == 0u)
            continue;
        cv_rect(x, y, 2, 2, c);
    }
    {
        int32_t u = 3 * 256, w = 0, x;
        const char *t = "No Man Sky";
        for (x = 0; t[x]; x++)
            w += t[x] == ' ' ? 5 : t[x] == 'M' ? 9 : 8;
        g_title(120 - (w * u >> 8) / 2, 62, t, u, NW);
        g_title(176, 108, "FM", 2 * 256, NO);
    }
    {   /* white: only "BASED ON"; what it is based on in yellow (each with a purple shadow) */
        static const char *const L[3] = {"BASED ON", "PICO-8 GAME LOW MEM SKY", "AND SLOOP"};
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            int32_t y = 158 + (int32_t)k * 22;
            n_text(120 - n_w(L[k], 1) / 2 + 2, y + 2, L[k], 1, NP);
            n_text(120 - n_w(L[k], 1) / 2, y, L[k], 1, k ? EY : NW);
        }
    }
}

/* the opening picture (one half: cv_oy): 120 x 120 in the ten colours, each pixel 2 x 2; the title over it, crisp */
static void g_picture(void)
{
#if NMS_ABERTURA
    static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
    int32_t x, y, y0 = -cv_oy / 2, y1 = y0 + 60;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < NMS_AB_N; y++)
        for (x = 0; x < NMS_AB_N; x++) {
            uint32_t i = (uint32_t)(y * NMS_AB_N + x), v = (NMS_AB[i >> 1] >> ((i & 1u) ? 0u : 4u)) & 15u;
            if (v && v < 10u)
                cv_rect(x * 2, y * 2, 2, 2, PAL[v]);
        }
    {
        const char *t = "NO MAN SKY FM";
        int32_t tx = 120 - n_w(t, 2) / 2, d;
        for (d = 0; d < 4; d++)                         /* a deep blue outline, then white */
            n_text(tx + (d == 0 ? -2 : d == 1 ? 2 : 0), 122 + (d == 2 ? -2 : d == 3 ? 2 : 0), t, 2, EN);
        n_text(tx, 122, t, 2, NW);
    }
#endif
}

static void sloop_splash(void)                          /* the boot screen (main.c), while felucca_init runs */
{
    uint32_t half;
    lcd_fill(0, 0, 240, 240, NK);
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, NK);
        cv_oy = -(int32_t)(half * 120u);
        {   /* only the stars (the user: no logo here): the opening starts from them, the ship coming in */
            uint32_t i;
            for (i = 0; i < 30u; i++) {
                uint32_t h = g_hash((int32_t)i, 3, 0x5A5Au);
                cv_rect((int32_t)(h % 238u), (int32_t)((h >> 9) % 238u), 2, 2, (h >> 20) % 5u == 4u ? NW : NCOL[(h >> 20) % 5u]);
            }
        }
        cv_oy = 0;
        cv_blit(0, half * 120u);
    }
    lcd_sync();
}

/* ------------------------------------------------------------- the loop --- */
static void g_init(void)
{
    memset(&g, 0, sizeof g);
    g.z = 3, g.mz = 1;
    g.mode = GM_INTRO;
    g.t_mode = g.t_last = fm1_ms;
    g.sx = (G_HOME - 10) << 8, g.sy = (G_HOME + 12) << 8;
    g.ang = 896u;                                       /* up and to the right: toward the first planet */
    g.fuel = G_CLASS[0].tank;
    g.own = 1;
    g.after = GM_SPACE;
    g.svx = G_HOME - 10, g.svy = G_HOME + 12;
    g.inited = 1;
    g_load();                                           /* the game as it was left */
}

static void g_set_mode(uint32_t m)
{
    g.mode = (uint8_t)m;
    if (m != GM_SPACE)
        g.cruise = 0;
    g.t_mode = fm1_ms;
    g.t_fade = fm1_ms;
}

/* land on g.pl: a place on land near its middle, the ship parked there */

/* -------------------------------------------------------------- the NPCs --- */
/* (the user) some planets have someone waiting (a yellow box with ! over the planet, from space; ? once you took what
 * they ask). Land there: they stand by your ship; walk up to them and they speak, a little box at the bottom: bring me
 * N of this (its icon). Come back with it and they take it: credits. In space now and then another ship flies by and
 * you talk over the radio, a balloon from each */
static int g_npc_here(const planet_t *p) { return (p->seed >> 17) % 3u == 0u && !g_race_here(p); }
static uint32_t g_npc_bit(const planet_t *p) { return 1u << ((p->seed >> 7) & 31u); }
static int g_npc_quest(int32_t sx, int32_t sy, uint32_t k) { return g.q_on && g.q_sx == sx && g.q_sy == sy && g.q_pk == k; }
/* someone: 7 x 12, after the little pixel figures: a head, a body, legs; colours of their own (the planet's seed) */
static void g_npc_draw(int32_t x, int32_t y, uint32_t seed)
{
    static const uint16_t SKIN[4] = {RGB(0xF0, 0xC8, 0x98), RGB(0xC0, 0x88, 0x58), RGB(0x80, 0xD0, 0x80), RGB(0xD0, 0xD0, 0xE0)};
    static const uint16_t CLOTH[6] = {NO, NB, NP, ER, RGB(0xC0, 0xB0, 0x80), NW};
    static const uint8_t FIG[12] = {0x1C, 0x3E, 0x2A, 0x3E, 0x1C, 0x7F, 0x7F, 0x5D, 0x5D, 0x1C, 0x14, 0x36};
    uint16_t sk = SKIN[seed & 3u], cl = CLOTH[(seed >> 2) % 6u], hr = (seed >> 5) & 1u ? NK : RGB(0x60, 0x38, 0x20);
    int32_t i, j, bob = (int32_t)((fm1_ms / 500u) & 1u);
    for (j = 0; j < 12; j++)
        for (i = 0; i < 7; i++)
            if ((FIG[j] >> (6 - i)) & 1u)
                gp(x - 3 + i, y - 12 + j - (j < 5 ? bob : 0), j < 2 ? hr : j < 5 ? (j == 2 && (i == 2 || i == 4) ? NK : sk) : j < 9 ? cl : j < 11 ? EN : NK);
}
static void g_npc_icon(int32_t x, int32_t y, int ask)   /* over a planet: a yellow box, ! (or ? when the quest is on) */
{
    int32_t i;
    for (i = 0; i < 7; i++)
        gp(x + i, y, EY), gp(x + i, y + 8, EY), gp(x, y + 1 + i, EY), gp(x + 6, y + 1 + i, EY);
    if (ask)
        gp(x + 2, y + 2, NW), gp(x + 3, y + 2, NW), gp(x + 4, y + 3, NW), gp(x + 3, y + 4, NW), gp(x + 3, y + 6, NW);
    else
        for (i = 2; i < 6; i++)
            gp(x + 3, y + i - (i == 5 ? 0 : 0), i == 5 ? EY : NW);
    if (!ask)
        gp(x + 3, y + 6, NW);
}
static void g_npc_tick(void)                            /* on foot: near them, they speak; with what they asked, it is done */
{
    planet_t *p = &g.pl;
    int32_t dx = (g.px >> 8) - g.nx, dy = (g.py >> 8) - g.ny;
    if (g.mode != GM_SURF || !g_npc_here(p) || (g.q_done & g_npc_bit(p)))
        return;
    if (dx * dx + dy * dy > 14 * 14) {
        g.talk_t = 0;
        return;
    }
    if (!g.talk_t)
        g.talk_t = G_STAMP;
    if (!g.q_on) {                                      /* a quest given */
        g.q_on = 1, g.q_k = (uint8_t)((p->seed >> 9) & 3u), g.q_n = (uint8_t)(3u + (p->seed >> 11) % 4u);
        g.q_sx = g.psx, g.q_sy = g.psy, g.q_pk = g.pk;
    } else if (g_npc_quest(g.psx, g.psy, g.pk) && g.res[g.q_k] >= g.q_n && fm1_ms - g.talk_t > 1500u) {   /* done */
        g.res[g.q_k] = (uint16_t)(g.res[g.q_k] - g.q_n);
        g.cred = (uint16_t)(g.cred + g.q_n * 40u);
        g.q_on = 0, g.q_done |= g_npc_bit(p), g.dirty = 1;
        g_say("THANK YOU!", "QUEST DONE: CREDITS");
    }
}
static void g_npc_talk_draw(void)                       /* the box at the bottom: who, what they ask, its icon */
{
    static const uint16_t RC[4] = {NB, NG, NP, NO};
    char t[24];
    int32_t n;
    if (g.mode != GM_SURF || !g.talk_t || !g_npc_here(&g.pl) || (g.q_done & g_npc_bit(&g.pl)))
        return;
    n = (int32_t)(fm1_ms - g.talk_t) / 40;              /* (its words come in, a letter at a time) */
    cv_rect(10, 168, 220, 44, NK), cv_rect(10, 168, 220, 2, EY), cv_rect(10, 210, 220, 2, EY);
    cv_rect(10, 168, 2, 44, EY), cv_rect(228, 168, 2, 44, EY);
    if (g_npc_quest(g.psx, g.psy, g.pk) && g.res[g.q_k] >= g.q_n)
        str_cpy(t, "YOU BROUGHT IT!", sizeof t);
    else
        str_cpy(t, "PILOT, BRING ME ", sizeof t), fmt_int(t + str_len(t), g.q_n);
    if (n < (int32_t)str_len(t))
        t[n < 0 ? 0 : n] = 0;
    n_texto(18, 172, t, 1, NW);
    cv_rect(18, 192, 12, 12, RC[g.q_k & 3u]), cv_rect(18, 192, 12, 2, NW);   /* the thing's icon */
    fmt_int(t, g.res[g.q_k]), str_cpy(t + str_len(t), " / ", 4), fmt_int(t + str_len(t), g.q_n);
    n_texto(38, 190, t, 1, g.res[g.q_k] >= g.q_n ? EG : NW);
}
/* a ship passing in space, talking over the radio: two balloons in turn */
static void g_radio_at(int32_t el, int32_t *x, int32_t *y)   /* (the user) its way: straight across, slowing to talk (2 s),
                                                         * nearly still while you talk, then off, faster and faster */
{
    int32_t d1 = gw * 45 / 100, sd;
    if (el < 2000)
        sd = d1 * g_eout(el * 256 / 2000) / 256;
    else if (el < 6500)
        sd = d1 + (el - 2000) * 4 / 1000;
    else {
        int32_t q = (el - 6500) * 256 / 2500;
        sd = d1 + 18 + (gw + 40) * q / 256 * q / 256;
    }
    *x = gw + 14 - sd, *y = gh / 3;
}
static void g_radio_box(int32_t bx, int32_t by, const char *t, uint16_t c, int32_t tx, int32_t ty)   /* a balloon fixed in
                                                         * its place (screen px), a line from it to (tx, ty) */
{
    int32_t w = n_w(t, 1);
    cv_line(bx + w / 2, by + (ty > by ? 18 : -2), tx, ty, c);
    cv_rect(bx - 4, by - 2, w + 8, 20, c), cv_rect(bx - 3, by - 1, w + 6, 18, NK);
    cv_rect(tx - 1, ty - 1, 3, 3, c);
    n_text(bx, by, t, 1, c);
}
static void g_radio_draw(void)
{
    static const char *const L[8] = {"HELLO PILOT!", "HI THERE!", "NICE SHIP.", "THANKS, NEW ONE.", "SUN STORM AHEAD", "GOT IT, OVER.",
                                     "SAFE TRAVELS", "YOU TOO!"};
    uint32_t el = g.fr_ev ? g.fr_e0 - g.fr_t : fm1_ms - g.fr_t, pair = (g.fr_t / 997u) % 4u, cls = 1u + (g.fr_t >> 3) % 4u;
    int32_t x, y, px, py, s = gz, k, ang, c, sn;
    if (!g.fr_t || el > 9000u || g.mode != GM_SPACE || g.z > 4)
        return;
    g_radio_at((int32_t)el, &x, &y);
    for (k = 1; k < 14; k++) {                          /* its trail: a streak of light where it was (long when it goes) */
        int32_t ox, oy, qx, qy;
        if ((int32_t)el - k * 80 < 0)
            break;
        g_radio_at((int32_t)el - (k - 1) * 80, &ox, &oy), g_radio_at((int32_t)el - k * 80, &qx, &qy);
        if (ox == qx)
            break;
        gline(ox + 6, oy, qx + 6, qy, k < 3 ? NW : k < 6 ? EY : k < 10 ? RGB(0x60, 0xE0, 0xFF) : NB);
        if (k < 6)
            gline(ox + 6, oy + 1, qx + 6, qy + 1, k < 3 ? NW : EY);
    }
    g_radio_at((int32_t)el - 60, &px, &py);             /* its heading: from a moment ago */
    {   /* the ship: one of the hangar's, a cell a pixel, turned along its way (nose first) */
        int32_t i, j;
        c = -32767, sn = 0, ang = 0;                    /* (straight across: nose to the left) */
        (void)px, (void)py;
        (void)ang;
        for (j = 0; j < 11; j++)
            for (i = 0; i < 11; i++)
                if ((G_HULL[(cls - 1u) & 3u][j] >> (10 - i)) & 1u) {
                    int32_t q;
                    for (q = 0; q < 4; q++) {           /* (four points a cell: no holes, turned) */
                        int32_t u = 10 - 2 * j + (q & 1), v = 2 * i - 10 + (q >> 1);   /* (half cells: along, across) */
                        gp(x + ((u * c - v * sn) >> 16), y + ((u * sn + v * c) >> 16), j < 2 ? NW : j > 8 ? NB : RGB(0xB0, 0xB0, 0xC0));
                    }
                }
        if ((fm1_ms / 80u) & 1u)                        /* its engine, behind */
            gp(x - ((6 * c) >> 15), y - ((6 * sn) >> 15), EY);
    }
    if (g.fr_ev) {                                      /* hailed: what they say, what it did */
        static const char *const E[4][2] = {{"FUEL FOR YOU, PILOT", "+2 FUEL"}, {"I'LL BUY 3 OF THOSE", "SOLD, x2 PRICE"},
                                            {"PIRATES! HELP US!", "FIGHT!"}, {"GOT ANYTHING TO SELL?", "NO? SAFE TRAVELS"}};
        const char *t1 = E[(g.fr_ev - 1u) & 3u][0], *t2 = E[(g.fr_ev - 1u) & 3u][1];
        int32_t n = (int32_t)(fm1_ms - g.fr_e0) / 40;
        char t[24];
        str_cpy(t, t1, sizeof t);
        if (n < (int32_t)str_len(t))
            t[n] = 0;
        cv_line(120, 168, x * s, y * s, EY);
        cv_rect(10, 168, 220, 44, NK), n_frame(10, 168, 220, 44, 2, EY);
        n_texto(18, 172, t, 1, NW);
        if (n > (int32_t)str_len(t1) + 6)
            n_texto(18, 190, t2, 1, g.fr_ev == 3u ? ER : EG);
        return;
    }
    if (el > 2000u && el < 4300u)                       /* their words (stopped), in the middle, fixed; a line to them */
        g_radio_box(120 - n_w(L[pair * 2u], 1) / 2, 72, L[pair * 2u], NW, x * s, y * s);   /* (the user: in the middle) */
    else if (el >= 4300u && el < 6500u)                 /* yours, under you, fixed; a line to you */
        g_radio_box(120 - n_w(L[pair * 2u + 1u], 1) / 2, 142, L[pair * 2u + 1u], EY, 120, 128);   /* (clear of the distances) */
}
static void g_land_place(void)
{
    int32_t r, a;
    for (r = 0; r < 400; r += 6)
        for (a = 0; a < 1024; a += 64) {
            int32_t x = (g_cos((uint32_t)a) * r) >> 15, y = (g_sin((uint32_t)a) * r) >> 15, h = g_height(x, y);
            if (h > g_sea() + 14 && h < 165) {
                g.lx = x, g.ly = y;
                g.px = g.tx = (x + 10) << 8, g.py = g.ty = (y + 4) << 8;   /* (out of the take-off reach) */
                g.nx = (int16_t)(x + 36), g.ny = (int16_t)(y - 6), g.talk_t = 0;   /* (someone, a few steps away) */
                g_sen_init();
                return;
            }
        }
    g.lx = g.ly = 0, g.px = g.tx = 10 << 8, g.py = g.ty = 4 << 8;
    g_sen_init();
}

/* B (OCT+): land near a planet / collect / take off */
#define G_CALL_MS 1800u
static void g_call_ship(void)                           /* on a planet: the ship comes down beside you */
{
    if (g.mode != GM_SURF || g.call_t)
        return;
    g.cx_ = (g.px >> 8) + 7, g.cy_ = (g.py >> 8) - 3;
    g.lx = g.ly = -100000;                              /* (away until it lands) */
    g.call_t = G_STAMP;
    ui_message("CALLING SHIP...");
}
/* the radar (HOME tapped twice, in space or on foot; KNOB 4 too): the scanner's pulse and its marks */
static void g_radar(void)
{
    if (g.mode != GM_SPACE && g.mode != GM_SURF)
        return;
    g.t_scan = G_STAMP;
    g_say("RADAR", "SCANNING");
}
#define G_DBL_MS 400u
static void g_home_tap(void)                            /* ui_input.c: HOME tapped on HOME (the game) */
{
    if (!g.inited)
        return;
    if (g.mode == GM_MAP) {                             /* the warp's map: HOME leaves it */
        g_set_mode(GM_SPACE);
        return;
    }
    if (g.home_t && (int32_t)(fm1_ms - g.home_t) < (int32_t)G_DBL_MS) {   /* twice: the radar */
        g.home_t = 0;
        g_radar();
        return;
    }
    g.home_t = G_STAMP;                             /* (once: waiting 0.4 s for a second tap) */
}
#define G_TRN 24u
static int32_t g_tr[G_TRN][2];                         /* the trail: the last places at full speed (world px) */
static uint32_t g_tr_i, g_tr_t;
/* ------------------------------------------------------------ the battle --- */
/* (the user) taking off with the sentinels after you: a fight in space, cinematic, seen from above: your little ship at
 * the bottom flying up, the planet you left behind it, asteroids drifting, the sun, all under a dark mesh so the fight
 * stands out. Three waves of little dark ships (4, 6, 8; three hits each) fire on the beat; then the boss: a colossal
 * ship wider than the screen, about a minute, its hull burning red where you hit it until those pixels blow and are
 * gone; from its half on, the view from behind your ship over the planet. Each key fires a pair of bolts straight
 * ahead; with the loop playing a plasma ball goes every other beat; your ship dodges by itself. A hit costs a unit of
 * cargo, as in flight. Then back to space */
#define BT_NE 8u
#define BT_NM 64u
#define BT_NP 36u
#define BT_Y 206                                        /* your ship's row */
#define BT_BOSS 3u
#define BT_BW 120                                       /* the boss: cells across (2 px each) */
#define BT_BH 46                                        /* .. and down */
#define BT_BY 26                                        /* its top (screen px) */
#define BR_HZ 92                                        /* (from behind) the horizon */
static struct {
    uint8_t on, wave, n, view;
    int32_t px, lane;                                   /* your ship: its column (Q8); from behind its lane (Q8) */
    uint32_t t0, hit_t, notes, beat, beat_t, won_t, clear_t, bt0, beats;
    struct { int8_t hp; uint32_t die, flash; } e[BT_NE];
    struct { int32_t x, y, vx, vy; uint8_t on, kind; } m[BT_NM];   /* kind 0: theirs, 1: your bolt, 2: a plasma ball,
                                                         * 3: a burning piece of the boss. From behind: x the lane, y the
                                                         * depth (1024 at the boss .. 0 at you) */
    struct { int16_t x, y, vx, vy; uint8_t t, c; } p[BT_NP];   /* sparks, fire, smoke (t: frames left) */
    uint8_t hull[BT_BH][BT_BW];                         /* the boss, a cell each: 0 none, 1..3 its greys, 4 a light,
                                                         * 5..12 burning (12 just hit .. 5 about to blow) */
    int32_t hp, hpmax, bx;                              /* its life, where it is across (screen px of its middle) */
} bt;
/* a little ship seen from above, nose down, 9 x 7 (drawn 2 x): its hull, two red lights, its engine at the back */
static const uint16_t BT_INV[5][8] = {                 /* (the user) the little ones as invaders: 11 x 8 each */
    {0x104, 0x088, 0x1FC, 0x376, 0x7FF, 0x5FD, 0x505, 0x0D8},
    {0x070, 0x3FE, 0x7FF, 0x739, 0x7FF, 0x0D8, 0x1AC, 0x606},
    {0x020, 0x070, 0x1FC, 0x3AE, 0x7FF, 0x252, 0x401, 0x202},
    {0x271, 0x1FC, 0x3DE, 0x7FF, 0x6DB, 0x7FF, 0x124, 0x306},
    {0x0F8, 0x1FC, 0x3FE, 0x6DB, 0x7FF, 0x5AD, 0x104, 0x000}};
static const uint16_t BT_INVC[5] = {NW, EG, RGB(0x60, 0xD0, 0xF0), RGB(0xF0, 0x90, 0x30), RGB(0xD0, 0x70, 0xF0)};

static void bt_spark(int32_t x, int32_t y, int32_t n, uint8_t c)   /* a burst: n sparks, fire or smoke */
{
    uint32_t i, k = 0;
    for (i = 0; i < BT_NP && k < (uint32_t)n; i++)
        if (!bt.p[i].t) {
            uint32_t h = g_hash((int32_t)i, (int32_t)(fm1_ms + k * 7u), 0x5B4u), a = h & 1023u;
            int32_t sp = 20 + (int32_t)((h >> 10) % 70u);
            bt.p[i].x = (int16_t)(x << 4), bt.p[i].y = (int16_t)(y << 4);
            bt.p[i].vx = (int16_t)((g_cos(a) >> 11) * sp / 16), bt.p[i].vy = (int16_t)((g_sin(a) >> 11) * sp / 16);
            bt.p[i].t = (uint8_t)(14 + (h >> 20) % 14u), bt.p[i].c = c;
            k++;
        }
}
/* the boss: a colossal ship made anew each time from a seed: a long spine, towers and blocks over and under it, antennas
 * with lights on their tips, rows of lit windows, its engines glowing at one end; its life about a minute */
static void bt_boss_make(uint32_t seed)
{
    uint32_t i, s = g_hash((int32_t)seed, 1, 0xB055u);
    int32_t x, y;
    for (y = 0; y < BT_BH; y++)
        for (x = 0; x < BT_BW; x++)
            bt.hull[y][x] = 0;
    for (x = 4; x < BT_BW - 4; x++)                     /* the spine */
        for (y = 22; y < 30; y++)
            bt.hull[y][x] = (uint8_t)(y == 22 ? 3 : y < 26 ? 2 : 1);
    for (i = 0; i < 16u; i++) {                         /* blocks over and under it */
        uint32_t h = g_hash((int32_t)i, 3, s);
        int32_t w = 5 + (int32_t)(h % 14u), hh = 4 + (int32_t)((h >> 8) % 14u), x0 = 4 + (int32_t)((h >> 16) % (uint32_t)(BT_BW - 8 - w));
        int up = (h >> 28) & 1u, y0 = up ? 22 - hh : 30;
        for (y = y0; y < y0 + hh && y < BT_BH; y++)
            for (x = x0; x < x0 + w; x++)
                bt.hull[y][x] = (uint8_t)(y == y0 ? 3 : (x == x0 ? 2 : (y - y0) % 4 == 2 && (x - x0) % 3 == 1 ? 4 : 1 + ((x + y) & 1)));
    }
    for (i = 0; i < 9u; i++) {                          /* antennas, a light on each */
        uint32_t h = g_hash((int32_t)i, 9, s);
        int32_t ax = 6 + (int32_t)(h % (uint32_t)(BT_BW - 12)), top = 2 + (int32_t)((h >> 8) % 10u);
        for (y = top; y < 22 && !bt.hull[y][ax]; y++)
            bt.hull[y][ax] = 2;
        bt.hull[top][ax] = 4;
    }
    for (y = 23; y < 29; y++)                           /* the engines, at the left end */
        bt.hull[y][2] = bt.hull[y][3] = 4;
    bt.hpmax = bt.hp = 460 + (int32_t)(s % 60u);        /* (the user: about 30 s, holding keys fires on) */        /* (the user: about a minute, keys on the eighths) */
    bt.view = 0, bt.bt0 = fm1_ms, bt.beats = 0, bt.bx = 120;
}
static void bt_boss_hit(int32_t x, int32_t y, int32_t r, int32_t dmg)   /* a hit at screen (x, y): the hull there catches fire */
{
    int32_t cx = (x - (bt.bx - BT_BW)) / 2, cy = (y - BT_BY) / 2, i, j;
    for (j = -r; j <= r; j++)
        for (i = -r; i <= r; i++)
            if ((uint32_t)(cx + i) < (uint32_t)BT_BW && (uint32_t)(cy + j) < (uint32_t)BT_BH && i * i + j * j <= r * r) {
                uint8_t *c = &bt.hull[cy + j][cx + i];
                if (*c >= 5u) {                         /* (the user) burning already: hit again, it blows away */
                    *c = 0;
                    if (((cx + i + cy + j) & 3) == 0)
                        bt_spark(bt.bx - BT_BW + (cx + i) * 2, BT_BY + (cy + j) * 2, 2, 1);
                } else if (*c) {
                    *c = (uint8_t)(12 - (g_hash(cx + i, cy + j, 0xF12u) & 3u));
                }
            }
    (void)dmg;
    bt_spark(x, y, 3, 0);
}
static int bt_boss_at(int32_t x, int32_t y)            /* a point on its hull (from above)? */
{
    int32_t cx = (x - (bt.bx - BT_BW)) / 2, cy = (y - BT_BY) / 2;
    return (uint32_t)cx < (uint32_t)BT_BW && (uint32_t)cy < (uint32_t)BT_BH && bt.hull[cy][cx];
}
static void bt_epos(uint32_t i, int32_t *x, int32_t *y)   /* an enemy's place: its slot, swaying in from the top */
{
    uint32_t el = fm1_ms - bt.t0, n = bt.n;
    int32_t in = el < 1500u ? (int32_t)(1500u - el) * 90 / 1500 : 0;
    *x = 26 + (int32_t)i * 188 / (int32_t)(n - 1u) + ((g_sin((fm1_ms / 6u) + i * 96u) * 12) >> 15);
    *y = 52 + (int32_t)(i & 1u) * 26 + ((g_sin((fm1_ms / 9u) + i * 200u) * 6) >> 15) - in;
}
static int32_t bt_aim_x(int32_t px)                    /* the enemy to line up under: the nearest one still there */
{
    uint32_t i;
    int32_t best = 120, bd = 1 << 30;
    for (i = 0; i < bt.n; i++)
        if (bt.e[i].hp > 0) {
            int32_t x, y;
            bt_epos(i, &x, &y);
            if ((x - px) * (x - px) < bd)
                bd = (x - px) * (x - px), best = x;
        }
    return best;
}
static int32_t bt_kick;                                /* the recoil, px (back to 0) */
static struct { int16_t x0, y0, x1, y1; uint32_t t; } bt_zl[8];   /* the wingmen's laser lines, a moment each */
static void bt_epos(uint32_t i, int32_t *x, int32_t *y);
static void bt_boss_hit(int32_t x, int32_t y, int32_t r, int32_t dmg);
static void bt_spark(int32_t x, int32_t y, int32_t n, uint8_t c);
static void bt_zap(int32_t x, int32_t y)                /* a wingman's laser: at the nearest one (the boss: its hull), a hit */
{
    uint32_t i, best = 0xFFu, q;
    int32_t bd = 1 << 30, tx = x, ty = 0;
    if (bt.wave == BT_BOSS) {
        uint32_t h = g_hash(x, (int32_t)fm1_ms, 0x2A9u);
        tx = bt.bx - 80 + (int32_t)(h % 160u), ty = BT_BY + 40 + (int32_t)((h >> 8) % 20u);
        bt_boss_hit(tx, ty, 2, 1);
    } else {
        for (i = 0; i < bt.n; i++)
            if (bt.e[i].hp > 0) {
                int32_t ex, ey;
                bt_epos(i, &ex, &ey);
                if ((ex - x) * (ex - x) + (ey - y) * (ey - y) < bd)
                    bd = (ex - x) * (ex - x) + (ey - y) * (ey - y), best = i, tx = ex, ty = ey;
            }
        if (best == 0xFFu)
            return;
        bt.e[best].hp--, bt.e[best].flash = fm1_ms;
        if (bt.e[best].hp <= 0)
            bt.e[best].die = fm1_ms, bt_spark(tx, ty, 10, 0);
    }
    for (q = 0; q < 7u && bt_zl[q].t && fm1_ms - bt_zl[q].t < 160u; q++)
        ;
    bt_zl[q].x0 = (int16_t)x, bt_zl[q].y0 = (int16_t)y, bt_zl[q].x1 = (int16_t)tx, bt_zl[q].y1 = (int16_t)ty, bt_zl[q].t = fm1_ms;
}
static int32_t bt_wings(void)                           /* wingmen: one a note held past the first, up to 4 */
{
    uint32_t n = fm1_in.notes, c = 0;
    while (n)
        c += n & 1u, n >>= 1;
    return c > 1u ? (int32_t)(c - 1u > 4u ? 4u : c - 1u) : 0;
}
static void bt_launch(int32_t x, int32_t y, int32_t vx, int32_t vy, uint8_t kind)
{
    uint32_t i;
    for (i = 0; i < BT_NM && bt.m[i].on; i++)
        ;
    if (i < BT_NM)
        bt.m[i].on = 1, bt.m[i].kind = kind, bt.m[i].x = x << 8, bt.m[i].y = y << 8, bt.m[i].vx = vx, bt.m[i].vy = vy;
}
static void bt_hurt(void)                               /* you are hit: a unit of cargo */
{
    uint32_t m = 0, q;
    if (fm1_ms - bt.hit_t < 900u)
        return;
    bt.hit_t = fm1_ms;
    for (q = 1; q < 4u; q++)
        if (g.res[q] > g.res[m])
            m = q;
    if (g.res[m])
        g.res[m]--;
    g_say("HIT", g.res[m] ? "CARGO LOST" : "HULL SCRATCHED");
}
static void bt_wave(uint32_t w)
{
    uint32_t i;
    bt.wave = (uint8_t)w, bt.n = (uint8_t)(4u + 2u * w), bt.t0 = fm1_ms, bt.clear_t = 0;
    for (i = 0; i < BT_NE; i++)
        bt.e[i].hp = (int8_t)(i < bt.n ? 3 : 0), bt.e[i].die = 0, bt.e[i].flash = 0;
    if (!w)
        g_say("BATTLE", "KEYS FIRE");
}
static void g_battle_start(void)
{
    uint32_t i;
    for (i = 0; i < BT_NM; i++)
        bt.m[i].on = 0;
    for (i = 0; i < BT_NP; i++)
        bt.p[i].t = 0;
    bt.on = 1, bt.px = 120 << 8, bt.lane = 0, bt.won_t = 0, bt.notes = fm1_in.notes, bt.beat = clk_beat, bt.beat_t = fm1_ms;
    g_set_mode(GM_BATTLE);
    bt_wave(0);
}
static void bt_tick(uint32_t dt)
{
    uint32_t i, k, alive = 0, nw = fm1_in.notes & ~bt.notes, beat = 0;
    int32_t px = bt.px >> 8, best = px, bd = 1 << 30, c, cx = 0;
    bt.notes = fm1_in.notes;
    if (dt > 100u)
        dt = 100u;
    if ((uint32_t)clk_beat != bt.beat || (!song.playing && fm1_ms - bt.beat_t > 700u))
        bt.beat = clk_beat, bt.beat_t = fm1_ms, bt.beats++, beat = 1;
    for (i = 0; i < bt.n; i++)
        if (bt.e[i].hp > 0) {
            int32_t x, y;
            bt_epos(i, &x, &y);
            cx += x, alive++;
        }
    if (bt.wave == BT_BOSS) {
        alive = bt.hp > 0;
        bt.bx = 120 + ((g_sin(fm1_ms / 17u) * 40) >> 15);
        bt.hp = bt.hpmax - (int32_t)(fm1_ms - bt.bt0) * bt.hpmax / 30000;   /* (the user) always 30 s */
        if (bt.hp > 0 && bt.hp * 2 < bt.hpmax && !bt.view)   /* its half: a word */
            bt.view = 2, g_say("IT BREAKS", "KEEP FIRING");
        for (k = 0; k < 40u; k++) {                     /* the hull burning: some cells burn down, the last blow */
            uint32_t h = g_hash((int32_t)k, (int32_t)fm1_ms, 0xB1Eu), y = h % BT_BH, x = (h >> 8) % BT_BW;
            uint8_t *c2 = &bt.hull[y][x];
            if (*c2 >= 5u && (*c2 = (uint8_t)(*c2 - 1u)) < 5u) {
                *c2 = 0;
                if ((h >> 20) % 3u == 0u) {
                    bt_spark(bt.bx - BT_BW + (int32_t)x * 2, BT_BY + (int32_t)y * 2, 4, 1);
                    if (!bt.view && (h >> 24) % 4u == 0u)   /* a burning piece falls */
                        bt_launch(bt.bx - BT_BW + (int32_t)x * 2, BT_BY + (int32_t)y * 2, ((int32_t)(h >> 26) % 40 - 20) << 8, 70 << 8, 3);
                }
            }
        }
    }
    {   /* (the user) the keys: each held key fires, and again every eighth while held; the track's DELAY sends echoes
         * of each shot after it (an eighth apart, up to 3), its REVERB spreads them sideways */
        static uint32_t held_t[27];
        static struct { uint32_t t; int8_t dx; } echo[12];
        int32_t dly = TSEL->p[P_DLY], rev = TSEL->p[P_REV], eighth = (int32_t)(30000u / (uint32_t)clamp(song.g[G_BPM], 40, 240));
        uint32_t held = fm1_in.notes;
        for (k = 0; k < 27u; k++) {
            int fire = 0;
            if ((nw >> k) & 1u)
                fire = 1, held_t[k] = fm1_ms;
            else if (((held >> k) & 1u) && fm1_ms - held_t[k] >= (uint32_t)eighth)
                fire = 1, held_t[k] = fm1_ms;
            if (!fire)
                continue;
            {   /* (the user) each more note held: a wingman beside you, firing too; each shot kicks them back */
                int32_t w, nw2 = bt_wings();
                for (w = 0; w <= nw2; w++) {
                    int32_t wx = px + (w == 0 ? 0 : (w & 1 ? -1 : 1) * 22 * ((w + 1) / 2));
                    if (w == 0)
                        bt_launch(wx - 4, BT_Y - 10, 0, -320 << 8, 1), bt_launch(wx + 4, BT_Y - 10, 0, -320 << 8, 1);
                    else                                /* (the user) the wingmen: a homing laser, a white line to its mark */
                        bt_zap(wx, BT_Y + 6);
                }
                bt_kick = 5;
            }
            for (i = 0; i < (uint32_t)(dly / 40) && i < 3u; i++) {
                uint32_t q;
                for (q = 0; q < 12u && echo[q].t; q++)
                    ;
                if (q < 12u)
                    echo[q].t = fm1_ms + (uint32_t)eighth * (i + 1u), echo[q].dx = (int8_t)(rev > 30 ? ((i & 1u) ? 1 : -1) * (int32_t)(i + 1u) * rev / 16 : 0);
            }
        }
        for (i = 0; i < 12u; i++)
            if (echo[i].t && (int32_t)(fm1_ms - echo[i].t) >= 0) {
                bt_launch(px + echo[i].dx, BT_Y - 10, (int32_t)echo[i].dx * 4 << 8, -300 << 8, 1);
                echo[i].t = 0;
            }
    }
    if (beat) {
        if (song.playing && (bt.beats & 1u)) {          /* the loop: every other beat a fan of three big ones */
            if (bt.view)
                bt_launch(bt.lane >> 8, 0, 0, 0, 2);
            else
                for (k = 0; k < 3u; k++)
                    bt_launch(px, BT_Y - 14, ((int32_t)k - 1) * 70 << 8, -280 << 8, 2);
        }
        if (bt.wave == BT_BOSS && bt.hp > 0) {         /* the boss: its turrets fire on the beat */
            for (k = 0; k < 2u + (bt.hpmax - bt.hp) * 3u / (uint32_t)bt.hpmax; k++) {
                uint32_t h = g_hash((int32_t)fm1_ms, (int32_t)k, 0xF1Bu);
                if (bt.view)
                    bt_launch(k ? (int32_t)(h % 220u) - 110 : bt.lane >> 8, 1024, 0, 0, 0);
                else {
                    int32_t tx = bt.bx - 100 + (int32_t)(h % 200u), ty = BT_BY + 70;
                    bt_launch(tx, ty, (px - tx) * 90 / (BT_Y - ty) << 8, 90 << 8, 0);
                }
            }
        } else if (bt.wave != BT_BOSS && alive && fm1_ms - bt.t0 > 1500u) {   /* the little ships fire on it */
            for (k = 0; k < 1u + bt.wave; k++) {
                uint32_t e = g_hash((int32_t)fm1_ms, (int32_t)k, 0xB7u) % bt.n;
                int32_t x, y, sp = 80 + 12 * (int32_t)bt.wave;
                if (bt.e[e].hp <= 0)
                    continue;
                bt_epos(e, &x, &y);
                bt_launch(x, y + 8, (px - x) * sp / (BT_Y - y + 1) << 8, sp << 8, 0);
            }
        }
    }
    for (i = 0; i < BT_NM; i++) {                       /* everything in flight */
        int32_t x, y;
        if (!bt.m[i].on)
            continue;
        if (bt.view) {                                  /* from behind: depth */
            if (bt.m[i].kind == 1u || bt.m[i].kind == 2u) {
                bt.m[i].y += (int32_t)dt * (bt.m[i].kind == 1u ? 1500 : 800) / 1000 << 8;
                if (bt.m[i].y >= 1000 << 8)
                    bt.hp -= bt.m[i].kind == 2u ? 3 : 1, bt.m[i].on = 0;
            } else {
                bt.m[i].y -= (int32_t)dt * 640 / 1000 << 8;
                if (bt.m[i].y <= 30 << 8) {
                    int32_t d = (bt.m[i].x >> 8) - (bt.lane >> 8);
                    if (d * d < 18 * 18)
                        bt_hurt();
                    bt.m[i].on = 0;
                }
            }
            continue;
        }
        bt.m[i].x += bt.m[i].vx / 1000 * (int32_t)dt, bt.m[i].y += bt.m[i].vy / 1000 * (int32_t)dt;
        x = bt.m[i].x >> 8, y = bt.m[i].y >> 8;
        if (x < -10 || x > 250 || y < 14 || y > 236) {
            bt.m[i].on = 0;
            continue;
        }
        if (bt.m[i].kind == 0u || bt.m[i].kind == 3u) { /* theirs, a piece: on you */
            if ((x - px) * (x - px) < 9 * 9 && (y - BT_Y) * (y - BT_Y) < 7 * 7)
                bt.m[i].on = 0, bt_hurt();
            continue;
        }
        if (bt.wave == BT_BOSS) {                       /* yours, on the boss's hull */
            if (bt.hp > 0 && bt_boss_at(x, y))
                bt_boss_hit(x, y, bt.m[i].kind == 2u ? 4 : 2, bt.m[i].kind == 2u ? 3 : 1), bt.m[i].on = 0;
            continue;
        }
        for (k = 0; k < bt.n; k++) {                    /* yours, on a little ship: three hits */
            int32_t ex, ey;
            if (bt.e[k].hp <= 0)
                continue;
            bt_epos(k, &ex, &ey);
            if ((x - ex) * (x - ex) < 10 * 10 && (y - ey) * (y - ey) < 9 * 9) {
                bt.e[k].hp -= (bt.m[i].kind == 2u ? 3 : 1) + (int8_t)(g.wpn / 2u), bt.e[k].flash = fm1_ms;   /* (the weapon's level) */
                if (bt.e[k].hp <= 0)
                    bt.e[k].die = fm1_ms, bt_spark(ex, ey, 10, 0), bt_spark(ex, ey, 5, 2);
                bt.m[i].on = 0;
                break;
            }
        }
    }
    if (bt_wings() > 0 && !bt.view) {                   /* the weaving laser: it burns what is over you */
        static uint32_t zt;
        if (fm1_ms - zt > 150u) {
            zt = fm1_ms;
            if (bt.wave == BT_BOSS)
                bt_boss_hit(px, BT_BY + 70, 2, 1);
            else
                for (k = 0; k < bt.n; k++) {
                    int32_t ex, ey;
                    if (bt.e[k].hp <= 0)
                        continue;
                    bt_epos(k, &ex, &ey);
                    if ((ex - px) * (ex - px) < 12 * 12) {
                        bt.e[k].hp--, bt.e[k].flash = fm1_ms;
                        if (bt.e[k].hp <= 0)
                            bt.e[k].die = fm1_ms, bt_spark(ex, ey, 10, 0);
                    }
                }
        }
    }
    for (i = 0; i < BT_NP; i++)                         /* the sparks */
        if (bt.p[i].t)
            bt.p[i].x = (int16_t)(bt.p[i].x + bt.p[i].vx), bt.p[i].y = (int16_t)(bt.p[i].y + bt.p[i].vy), bt.p[i].t--;
    if (bt.view) {                                      /* the dodge from behind: the lane least in the way */
        int32_t lx = bt.lane >> 8;
        best = lx;
        for (c = -100; c <= 100; c += 10) {
            int32_t dg = (c - lx) * (c - lx) / 40;
            for (i = 0; i < BT_NM; i++)
                if (bt.m[i].on && bt.m[i].kind == 0u && bt.m[i].y < 500 << 8) {
                    int32_t d = (bt.m[i].x >> 8) - c;
                    if (d * d < 30 * 30)
                        dg += 6000 - (bt.m[i].y >> 8) * 8;
                }
            if (dg < bd)
                bd = dg, best = c;
        }
        bt.lane += clamp((best << 8) - bt.lane, -(int32_t)(160u * dt * 256u / 1000u), (int32_t)(160u * dt * 256u / 1000u));
    } else {                                            /* from above: the column least in the way, under them */
        for (c = 16; c <= 224; c += 8) {
            int32_t aim = bt.wave == BT_BOSS ? bt.bx : alive ? bt_aim_x(px) : 120;
            int32_t dg = (c - aim) * (c - aim) / 8 + (c - px) * (c - px) / 64;
            for (i = 0; i < BT_NM; i++)
                if (bt.m[i].on && (bt.m[i].kind == 0u || bt.m[i].kind == 3u)) {
                    int32_t x = bt.m[i].x >> 8, y = bt.m[i].y >> 8;
                    if (y > 110 && y < BT_Y + 6 && (x - c) * (x - c) < 14 * 14)
                        dg += 4000 - (BT_Y - y) * 20;
                }
            if (dg < bd)
                bd = dg, best = c;
        }
        bt.px += clamp((best << 8) - bt.px, -(int32_t)(140u * dt * 256u / 1000u), (int32_t)(140u * dt * 256u / 1000u));
    }
    if (bt.wave == BT_BOSS) {
        if (bt.hp <= 0 && !bt.won_t)
            bt.won_t = fm1_ms, g.cred = (uint16_t)(g.cred + 250u), g_say("BOSS DOWN", "+250 CREDITS");
        if (bt.won_t && ((fm1_ms / 90u) & 1u))          /* dying: blowing up all over */
            bt_spark(bt.bx - BT_BW + (int32_t)(g_hash((int32_t)fm1_ms, 1, 9u) % 240u), BT_BY + (int32_t)(g_hash((int32_t)fm1_ms, 2, 9u) % 90u), 6, 0);
    } else if (!alive && !bt.won_t) {                   /* a wave down: the next, or the boss */
        if (!bt.clear_t)
            bt.clear_t = fm1_ms;
        else if (fm1_ms - bt.clear_t > 1500u) {
            if (bt.wave < 2u) {
                bt_wave(bt.wave + 1u);
            } else {
                bt.wave = BT_BOSS, bt.n = 0, bt.clear_t = 0;
                bt_boss_make(fm1_ms);
                g_say("WARNING", "SOMETHING COLOSSAL");
            }
        }
    }
    if (bt.won_t && fm1_ms - bt.won_t > 3000u)          /* back to space */
        bt.on = 0, g.foe_n = 0, g_set_mode(GM_SPACE);
}

/* ---- drawing */
static const uint16_t BT_GREY[4] = {0, RGB(0x26, 0x26, 0x32), RGB(0x3C, 0x3E, 0x50), RGB(0x66, 0x6A, 0x80)};
static void bt_boss_draw(int32_t y0, int32_t y1)        /* the boss from above: its hull, lights, the burning */
{
    int32_t x, y, ox = bt.bx - BT_BW;
    uint32_t now = fm1_ms;
    for (y = 0; y < BT_BH; y++) {
        int32_t sy = BT_BY + y * 2;
        if (sy + 2 < y0 || sy >= y1)
            continue;
        for (x = 0; x < BT_BW; x++) {
            uint32_t c = bt.hull[y][x];
            uint16_t col;
            if (!c)
                continue;
            if (c < 4u) {                               /* (a run of the same grey: one rectangle) */
                int32_t x1 = x;
                while (x1 + 1 < BT_BW && bt.hull[y][x1 + 1] == c)
                    x1++;
                cv_rect(ox + x * 2, sy, (x1 - x + 1) * 2, 2, BT_GREY[c]);
                x = x1;
                continue;
            }
            if (c < 4u)
                col = BT_GREY[c];
            else if (c == 4u)
                col = ((now / 400u + (uint32_t)x) % 7u) ? RGB(0x5A, 0xE6, 0xF0) : NW;   /* its lights, a few blinking */
            else {                                      /* burning, in layers: the hottest pale, then light orange,
                                                         * orange, a dark red rim (flickering a step) */
                uint32_t heat = c - 5u + (g_hash(x, y, now / 90u) & 1u);
                col = heat >= 7u ? RGB(0xFF, 0xF4, 0xB0) : heat >= 5u ? RGB(0xFF, 0xB0, 0x48) : heat >= 3u ? RGB(0xF8, 0x68, 0x10) : RGB(0xB8, 0x30, 0x10);
                if (y > 0 && !bt.hull[y - 1][x] && sy > 4 && (g_hash(x, y, now / 110u) & 1u)) {   /* a tongue rising off it, smoke above */
                    int32_t tl = 2 + (int32_t)(heat / 2);
                    cv_rect(ox + x * 2, sy - tl * 2, 2, tl * 2, RGB(0xB8, 0x30, 0x10));
                    cv_rect(ox + x * 2, sy - tl, 2, tl, RGB(0xF8, 0x68, 0x10));
                    cv_rect(ox + x * 2 - 1, sy - tl * 2 - 4, 3, 3, RGB(0x3A, 0x22, 0x2A));
                }
            }
            cv_rect(ox + x * 2, sy, 2, 2, col);
        }
    }
}
static void bt_sparks_draw(void)
{
    uint32_t i;
    for (i = 0; i < BT_NP; i++)
        if (bt.p[i].t) {
            uint8_t t = bt.p[i].t;
            uint16_t col = bt.p[i].c == 2u ? RGB(0x40, 0x3C, 0x48) : t > 18u ? EY : t > 10u ? NO : bt.p[i].c ? ER : RGB(0x60, 0x28, 0x20);
            int32_t s = bt.p[i].c == 2u ? 3 : t > 18u ? 2 : 1;
            cv_rect((bt.p[i].x >> 4) - s / 2, (bt.p[i].y >> 4) - s / 2, s, s, col);
        }
}
static void bt_me(int32_t cx, int32_t cy, int32_t sc)   /* your ship (the triangle) from above: two blue jets in lines
                                                         * from its wings, a short yellow flame in the middle */
{
    int32_t ogz = gz, ogw = gw, ogh = gh, f = (int32_t)((fm1_ms / 45u) & 3u);
    cv_rect(cx - 3 * sc, cy + 2 * sc, 1, 8 + 3 * sc + f, RGB(0x30, 0x60, 0xF0)), cv_rect(cx - 3 * sc, cy + 2 * sc, 1, 3, RGB(0xB0, 0xE0, 0xFF));
    cv_rect(cx + 3 * sc, cy + 2 * sc, 1, 8 + 3 * sc + f, RGB(0x30, 0x60, 0xF0)), cv_rect(cx + 3 * sc, cy + 2 * sc, 1, 3, RGB(0xB0, 0xE0, 0xFF));
    cv_rect(cx - 1, cy + sc + 1, 3, 2 + (f & 1), EY), cv_pset(cx, cy + sc + 3 + f / 2, NO);
    gz = 1, gw = gh = 240;
    if (sc > 1 && g.cls)                                /* (the user) your ship as bought; the wingmen mini triangles */
        g_hull(cx, cy, g.cls, 2, RGB(0x90, 0xA0, 0xC0));
    else
        g_ship(cx, cy, 768u, 0, sc);
    gz = ogz, gw = ogw, gh = ogh;
}
static void bt_backdrop(int32_t y0, int32_t y1)         /* space behind the fight: the planet you left, the sun, asteroids,
                                                         * in big pixels (4 x fewer to work out: the frame rate) and through a
                                                         * dark mesh (the fight in front) */
{
    uint32_t i, now = fm1_ms;
    int32_t ogz = gz, ogw = gw, ogh = gh, ofade = g.fade;
    for (i = 0; i < 40u; i++) {                         /* the stars, going by downward: you fly up */
        uint32_t h = g_hash((int32_t)i, 7, 0xB5u), sp = 20u + (h >> 20) % 90u;
        cv_rect((int32_t)(h % 240u), (int32_t)(((h >> 8) % 240u + now * sp / 1000u) % 240u), 1, 1 + (int32_t)sp / 50, sp > 80u ? NW : NB);
    }
    gz = 2, gw = gh = 120, g.fade = 7;                  /* (g.fade < 16: gp leaves out half of its pixels, the mesh) */
    {   /* the planet you left: flat, its sea and land in blocks of 4 (one hash a block), the night side dark */
        int32_t cx = 90, cy = 75 + (int32_t)(now / 800u % 20u), r = 34, i2, j2;
        uint16_t wc = GCOL[g.pl.water & 7u], lc = GCOL[g.pl.land & 7u];
        for (j2 = -r; j2 <= r; j2 += 2) {
            int32_t w = g_isqrt(r * r - j2 * j2);
            if (cy + j2 < y0 / 2 - 1 || cy + j2 > y1 / 2 + 1)
                continue;
            for (i2 = -w; i2 <= w; i2 += 2) {
                uint16_t c = (g_hash((i2 + (int32_t)(now / 300u)) >> 3, j2 >> 3, g.pl.seed) & 3u) ? wc : lc;
                if (i2 + j2 > r / 2)
                    c = EN;
                gp(cx + i2, cy + j2, c), gp(cx + i2 + 1, cy + j2, c), gp(cx + i2, cy + j2 + 1, c), gp(cx + i2 + 1, cy + j2 + 1, c);
            }
        }
    }
    for (i = 0; i < 4u; i++) {                          /* asteroids drifting down, rough and dark */
        uint32_t h = g_hash((int32_t)i, 11, 0xA57u);
        int32_t ax = (int32_t)(h % 120u), ay = (int32_t)(((h >> 8) % 160u + now * (4u + (h >> 24) % 10u) / 1000u) % 160u) - 20;
        int32_t r = 3 + (int32_t)((h >> 16) % 6u), i2, j2;
        for (j2 = -r; j2 <= r; j2++)
            for (i2 = -r; i2 <= r; i2++)
                if (i2 * i2 + j2 * j2 <= r * r - (int32_t)(g_hash(i2, j2, h) & 3u))
                    gp(ax + i2, ay + j2, i2 + j2 < -r / 2 ? RGB(0x5A, 0x40, 0x48) : RGB(0x3A, 0x28, 0x30));
    }
    gz = ogz, gw = ogw, gh = ogh, g.fade = (uint8_t)ofade;
}
static void bt_bolt(int32_t x, int32_t y, int kind)     /* (the user) your shot, a laser by the weapon's level: green, then
                                                         * doubled, cyan and longer, violet and wide, gold and widest */
{
    static const uint16_t WC[5][3] = {{RGB(0x20, 0x70, 0x10), RGB(0x70, 0xE0, 0x30), RGB(0xD0, 0xFF, 0xA0)},
                                      {RGB(0x20, 0x70, 0x10), RGB(0x70, 0xE0, 0x30), RGB(0xD0, 0xFF, 0xA0)},
                                      {RGB(0x10, 0x50, 0x80), RGB(0x40, 0xC0, 0xF0), RGB(0xC0, 0xF0, 0xFF)},
                                      {RGB(0x40, 0x10, 0x70), RGB(0xA0, 0x50, 0xF0), RGB(0xF0, 0xD0, 0xFF)},
                                      {RGB(0x70, 0x40, 0x00), RGB(0xF0, 0xC0, 0x30), RGB(0xFF, 0xF8, 0xD0)}};
    uint32_t lv = g.wpn > 4u ? 4u : g.wpn;
    int32_t L = (kind == 2 ? 26 : 18) + (int32_t)lv * 3, w = lv >= 3u ? 2 : 1;
    cv_rect(x - w, y - 2, 2 * w + 1, L, WC[lv][0]);
    cv_rect(x - w + 1, y - 3, 2 * w - 1, L, WC[lv][1]);
    cv_rect(x, y - 3, 1, L / 2, WC[lv][2]);
    if (lv == 1u)                                       /* (level 2: a twin beside it) */
        cv_rect(x + 3, y + 2, 1, L - 4, WC[lv][1]);
}
static void bt_orb(int32_t x, int32_t y, int32_t s)    /* their shot: a red orb, a hot core, a dark rim */
{
    cv_rect(x - s - 1, y - s - 1, 2 * s + 3, 2 * s + 3, RGB(0x30, 0x08, 0x10));
    cv_rect(x - s, y - s, 2 * s + 1, 2 * s + 1, (fm1_ms / 80u) & 1u ? ER : NO);
    cv_rect(x - s / 2, y - s / 2, s + 1, s + 1, EY);
}
static void bt_hp_draw(void)                            /* the boss's life: a column of style A cells on the right edge,
                                                         * the time over it (the messages keep the top left) */
{
    int32_t i, n = 30, f = bt.hpmax ? n * (bt.hp > 0 ? bt.hp : 0) / bt.hpmax : 0;
    char t[12];
    uint32_t s = (fm1_ms - bt.bt0) / 1000u;
    for (i = 0; i < n; i++) {
        int32_t y = 190 - i * 5;
        cv_rect(229, y, 8, 4, i < f ? ER : RGB(0x6E, 0x1E, 0x16));
        if (i < f)
            cv_rect(229, y + 1, 8, 1, NW);
    }
    fmt_int(t, (int32_t)(s / 60u)), str_cpy(t + str_len(t), ":", 2);
    t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s % 60u / 10u);
    t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s % 10u);
    n_texto(236 - n_w(t, 1), 22, t, 1, NW);
}
/* (from behind) a thing at lane x, depth z: its screen place and scale (Q8) */
static void bt_proj(int32_t x, int32_t z, int32_t *sx, int32_t *sy, int32_t *s)
{
    int32_t q = 1024 - (z < 0 ? 0 : z > 1024 ? 1024 : z);
    *s = 24 + q * q / 4400;
    *sx = 120 + (x - (bt.lane >> 8)) * *s / 256;
    *sy = BR_HZ + (208 - BR_HZ) * q * q / (1024 * 1024);
}
/* the billboards from behind: one shape for all, at a scale: a cloud (our clouds: noise, cream, lit up left, shaded
 * down right) or a mountain (jagged, snow on top, lit on its left) */
static void bt_bill(int32_t cx, int32_t by, int32_t w, int32_t h, int kind, uint32_t seed, int32_t y0, int32_t y1)
{
    int32_t i, j;
    for (j = -h; j <= 0; j++) {
        int32_t y = by + j;
        if (y < y0 || y >= y1)
            continue;
        for (i = -w; i <= w; i++) {
            int32_t x = cx + i, ax = i < 0 ? -i : i;
            if ((uint32_t)x >= 240u)
                continue;
            if (kind) {
                int32_t top = h * (w - ax) / (w + 1) + (int32_t)(g_vnoise(i * 32 / (w + 1), (int32_t)seed & 255, seed, 3, 0) % 12u) - 6;
                if (-j > top)
                    continue;
                cv_pset(x, y, -j > top - h / 6 ? NW : i < 0 ? GCOL[g.pl.land2 & 7u] : (BAYER[y & 3][x & 3] < 8 ? EN : GCOL[g.pl.land2 & 7u]));
            } else {
                int32_t dy2 = j + h / 2, q = i * i * 256 / (w * w + 1) + dy2 * dy2 * 256 / (h * h / 4 + 1);
                int32_t n = g_vnoise(i * 40 / (w + 1) + (int32_t)seed, dy2 * 40 / (h + 1) + 300, seed, 3, 0) - q / 3;
                if (n < 70)
                    continue;
                cv_pset(x, y, g_vnoise((i - 2) * 40 / (w + 1) + (int32_t)seed, (dy2 - 2) * 40 / (h + 1) + 300, seed, 3, 0) - q / 3 < 70 ? NW
                              : n < 82 ? RGB(0xA8, 0xA2, 0x8C) : RGB(0xF4, 0xEC, 0xC4));
            }
        }
    }
}

/* ------------------------------------------------------------- the race --- */
/* (the user) some planets hold a race (a chequered flag on them, from space): landing there you line up with five other
 * ships, the same ship in other colours, the engines start, 3 2 1 GO, and you race three laps of a road over the planet,
 * from behind your ship (a pseudo-3D road, as the night-drive visualizer: rows from the horizon down, its dashes and
 * edges, posts with lamps; the planet's ground small and slow on both sides; its mountains in two silhouettes; banks of
 * clouds, some stretches thick with them). KNOB 1 steers, KNOB 2 the speed, OCT- held a boost. Your place and the lap on
 * top, the time. Then the credits for your place, and the planet's ground */
#define RC_N 5u
#define RC_LAP 6000                                     /* a lap (track units) */
#define RC_HZ 100                                       /* the horizon */
#define RC_GO 4200u                                     /* the start: the engines (1.2 s), 3 2 1 (3 s), GO */
static struct {
    int32_t d, v, lane, rd[RC_N], rv[RC_N], rl[RC_N], sky;   /* distance, speed, lane: you and the others; the sky's slide */
    uint32_t t0, done_t;
    uint8_t place, passed, gear;
    int8_t thr;
    uint32_t shift_t;                                   /* (the user) a gear change: the moment */
} rc;
static const uint16_t RC_COL[RC_N][2] = {{RGB(0x30, 0x80, 0xF0), RGB(0x90, 0xD0, 0xFF)}, {RGB(0x30, 0xC0, 0x50), RGB(0xB0, 0xF0, 0x90)},
    {RGB(0xF0, 0xC0, 0x20), RGB(0xFF, 0xF0, 0xA0)}, {RGB(0x90, 0x40, 0xE0), RGB(0xD0, 0xB0, 0xFF)}, {RGB(0xE0, 0xE0, 0xE0), NW}};
static void g_race_start(void)
{
    uint32_t i;
    rc.d = 0, rc.v = 0, rc.lane = 0, rc.thr = 6, rc.t0 = fm1_ms, rc.done_t = 0, rc.place = RC_N + 1u, rc.sky = 0, rc.passed = 0, rc.gear = 0, rc.shift_t = 0;
    for (i = 0; i < RC_N; i++)                          /* the grid: two by two ahead of you */
        rc.rd[i] = 60 + (int32_t)(i / 2u) * 70 + (int32_t)(i & 1u) * 20, rc.rv[i] = 0, rc.rl[i] = (i & 1u) ? 60 : -60;
    g_set_mode(GM_RACE);
}
static int32_t rc_curve(int32_t d) { return (g_sin((uint32_t)(d / 4) & 1023u) * 80 >> 15) + (g_sin((uint32_t)(d / 11) & 1023u) * 50 >> 15); }
static void rc_tick(uint32_t dt)
{
    uint32_t i, ahead = 0, el = fm1_ms - rc.t0;
    int32_t top = (8 + g_spd() * 2 + rc.thr * 3 + (g_down(0) ? 12 : 0)) * 14;   /* (your ship and engine: real speed) */
    if (dt > 100u)
        dt = 100u;
    if (!rc.done_t && el >= RC_GO) {
        {   /* (the user) it all speeds up fast, through five gears: each gear's top, a breath at the change */
            int32_t gt = top * (int32_t)(rc.gear + 1u) / 5;
            if (fm1_ms - rc.shift_t > 140u)
                rc.v += (gt + gt / 8 - rc.v) * (int32_t)dt / (180 + (int32_t)rc.gear * 70);
            if (rc.v > gt)
                rc.v = gt;
            if (rc.v >= gt * 96 / 100 && rc.gear < 4u)
                rc.gear++, rc.shift_t = fm1_ms;
            else if (rc.gear && rc.v < top * (int32_t)rc.gear / 5 * 80 / 100)
                rc.gear--;
        }
        {   /* (the user) it drives itself: round the bends, past the ship ahead of it (KNOB 1 still nudges it) */
            int32_t want = 0;
            for (i = 0; i < RC_N; i++) {
                int32_t dz = rc.rd[i] - rc.d;
                if (dz > 0 && dz < 160 && (rc.rl[i] - rc.lane) * (rc.rl[i] - rc.lane) < 40 * 40)
                    want = rc.rl[i] > 0 ? -60 : 60;
            }
            rc.lane += (want - rc.lane) * (int32_t)dt / 400;
        }
        if (rc.lane > 100 || rc.lane < -100)
            rc.v -= rc.v * (int32_t)dt / 600;                          /* off the road: slower */
        rc.d += rc.v * (int32_t)dt / 1000;
        rc.sky += rc_curve(rc.d) * rc.v / 4000 * (int32_t)dt / 100;    /* the mountains slide with the bends */
    }
    for (i = 0; i < RC_N; i++) {                        /* the others: each its own pace, weaving */
        int32_t pace = i == RC_N - 1u ? 900 : 380 + (int32_t)((g_hash((int32_t)i, 4, 0xACE) % 120u));   /* (the last: a fast one) */
        if (el < RC_GO)
            break;
        rc.rv[i] += (pace - rc.rv[i]) * (int32_t)dt / 500;
        rc.rd[i] += rc.rv[i] * (int32_t)dt / 1000;
        rc.rl[i] = ((g_sin((uint32_t)(rc.rd[i] / 7 + (int32_t)i * 200) & 1023u) * 70) >> 15);
        if (i == RC_N - 1u && rc.rd[i] > rc.d + 1500)   /* (the fast one: far ahead, it comes again from behind) */
            rc.rd[i] = rc.d - 400, rc.rv[i] = 700;
        if (rc.rd[i] < rc.d - 60 && !(i == RC_N - 1u && rc.rv[i] > rc.v)) {   /* (the user) passed: another one comes up ahead */
            rc.rd[i] = rc.d + 500 + (int32_t)(g_hash((int32_t)i, rc.d, 0x7A5u) % 700u), rc.rv[i] = 200;
            if (rc.passed < 250u)
                rc.passed++;
        }
        ahead += rc.rd[i] > rc.d;
    }
    if (!rc.done_t && (rc.d >= 3 * RC_LAP || el > RC_GO + 50000u)) {   /* 3 laps (or 50 s: it always ends) */
        rc.done_t = fm1_ms, rc.place = (uint8_t)(ahead + 1u);
        g.cred = (uint16_t)(g.cred + 10u * rc.passed);
        g_say("FINISHED", rc.passed > 12u ? "GREAT RACE" : "CREDITS WON");
    }
    if (rc.done_t && fm1_ms - rc.done_t > 3500u) {
        if (rc_space)
            rc_space = 0, g_set_mode(GM_SPACE);
        else
            g_land_place(), g_set_mode(GM_SURF);
    }
}
/* the road at row y: its middle (x) and its half width */
static int32_t rc_w(int32_t y) { return (y - RC_HZ) * 2 + 4; }
static int32_t rc_cx(int32_t y)
{
    int32_t f = 240 - y;
    return 120 - rc.lane * rc_w(y) / 120 + rc_curve(rc.d) * f * f / 15000;
}
/* (the user) the race in space, after the synthwave drives: a road floating among the stars, neon at its edges, a
 * spiral galaxy in the sky; first person, the dashboard under it (pixel art, after the cars' and Knight Rider's):
 * time, lap, passed; the revs on a rising arc, the speed in big digits, the gear; shift lights, a radar */
#define RS_HZ 70
#define RS_BOT 150
static int32_t rs_w(int32_t y) { return (y - RS_HZ) * 9 / 8 + 2; }
static int32_t rs_cx(int32_t y)
{
    int32_t f = RS_BOT + 20 - y;
    return 120 - rc.lane * rs_w(y) / 120 + rc_curve(rc.d) * f * f / 9000;
}
static void rs_galaxy(int32_t gx, int32_t gy)
{
    uint32_t arm, t, now = fm1_ms;
    int32_t i, j;
    for (j = -5; j <= 5; j++)                           /* the core: white into yellow, dithered */
        for (i = -12; i <= 12; i++) {
            int32_t q = i * i / 4 + j * j * 4;
            if (q < 36 && (int32_t)BAYER[(gy + j) & 3][(gx + i) & 3] < 16 - q * 16 / 36)
                cv_pset(gx + i, gy + j, q < 8 ? NW : q < 20 ? RGB(0xFF, 0xF0, 0xB0) : RGB(0xF0, 0xA0, 0xC0));
        }
    for (arm = 0; arm < 2u; arm++)                      /* two arms, winding out, turning slowly */
        for (t = 0; t < 260u; t++) {
            uint32_t h = g_hash((int32_t)t, (int32_t)arm, 0x6A1u), a = t * 6u + arm * 512u + now / 90u + (h & 31u);
            int32_t r = 4 + (int32_t)t / 5 + (int32_t)((h >> 5) & 3u) - 1;
            int32_t x = gx + ((g_cos(a & 1023u) * r) >> 15), y = gy + (((g_sin(a & 1023u) * r) >> 15) * 2 / 5);
            if ((h >> 9) % 3u == 0u)
                continue;
            cv_pset(x, y, t < 50u ? RGB(0xFF, 0xE0, 0xC0) : t < 130u ? ((h >> 12) & 1u ? RGB(0xE0, 0x70, 0xD0) : NP) : ((h >> 12) & 1u ? NB : RGB(0x60, 0xB0, 0xFF)));
        }
}
static void rs_dash(void)
{
    static const uint16_t TD = RGB(0x0C, 0x36, 0x44), TM = RGB(0x18, 0x64, 0x74), TL = RGB(0x48, 0xB0, 0xC0), LCD = RGB(0xD8, 0xF0, 0x90);
    uint32_t now = fm1_ms, el = now - rc.t0, s = el < RC_GO ? 0u : ((rc.done_t ? rc.done_t : now) - rc.t0 - RC_GO) / 100u;
    int32_t top = (8 + g_spd() * 2 + rc.thr * 3) * 14, gt = top * (int32_t)(rc.gear + 1u) / 5, g0 = top * (int32_t)rc.gear / 5;
    int32_t rev = el < RC_GO ? (int32_t)el * 400 / (int32_t)RC_GO + (int32_t)((now / 60u) % 3u) * 30 : gt > g0 ? (rc.v - g0) * 1000 / (gt - g0) : 0;
    int32_t i, sh = el >= RC_GO && now - rc.shift_t < 300u;
    char t[16];
    cv_rect(0, RS_BOT, 240, 90, TD);                    /* the dash: its top edge lit, the column in the middle */
    cv_rect(0, RS_BOT, 240, 2, TL), cv_rect(0, RS_BOT + 2, 240, 1, TM);
    cv_rect(106, RS_BOT, 28, 90, TM), cv_rect(110, RS_BOT + 10, 20, 44, NK), n_frame(110, RS_BOT + 10, 20, 44, 1, TL);
    {   /* the column's cross: where you are on the road (a dot running left / right) */
        int32_t lx = 120 + rc.lane * 8 / 110;
        cv_rect(119, RS_BOT + 14, 2, 36, ER), cv_rect(113, RS_BOT + 31, 14, 2, ER);
        cv_rect(lx - 2, RS_BOT + 30, 4, 4, (now / 200u) & 1u ? EY : NW);
    }
    cv_rect(4, RS_BOT + 8, 98, 52, NK), n_frame(4, RS_BOT + 8, 98, 52, 1, TL);   /* left: time, lap, passed */
    n_texto(8, RS_BOT + 10, "TIME", 1, ER);
    fmt_int(t, (int32_t)(s / 600u)), str_cpy(t + str_len(t), ":", 2);
    t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s / 10u % 60u / 10u);
    t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s / 10u % 10u);
    t[str_len(t) + 1] = 0, t[str_len(t)] = '.';
    t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s % 10u);
    n_texto(98 - n_w(t, 1), RS_BOT + 10, t, 1, LCD);
    {
        uint32_t lap = (uint32_t)(rc.d / RC_LAP) + 1u;
        str_cpy(t, "LAP ", 5), fmt_int(t + 4, (int32_t)(lap > 3u ? 3u : lap)), str_cpy(t + str_len(t), "/3", 3);
        n_texto(8, RS_BOT + 26, t, 1, NW);
        fmt_int(t, (int32_t)rc.passed);
        n_texto(8, RS_BOT + 42, "PASSED", 1, NB);
        n_texto(98 - n_w(t, 1), RS_BOT + 42, t, 1, LCD);
    }
    cv_rect(138, RS_BOT + 8, 98, 52, NK), n_frame(138, RS_BOT + 8, 98, 52, 1, TL);   /* right: the revs, the speed, the gear */
    rev = clamp(rev, 0, 1000);
    for (i = 0; i < 14; i++) {                          /* the arc: cells rising, yellow to orange to red */
        int32_t q = (13 - i) * (13 - i), y = RS_BOT + 12 + q * 12 / 169, x = 142 + i * 5, on = i * 1000 / 14 < rev;
        uint16_t c = i < 8 ? EY : i < 11 ? NO : ER;
        cv_rect(x, y, 4, 3 + i / 5, on ? c : RGB(0x30, 0x20, 0x10));
        if (on)
            cv_rect(x, y, 4, 1, NW);
    }
    fmt_int(t, rc.v / 3);
    n_texto(200 - n_w(t, 2), RS_BOT + 28, t, 2, LCD);
    n_texto(204, RS_BOT + 40, "U/S", 1, NP);
    cv_rect(212, RS_BOT + 12, 20, 22, sh ? EY : TD), n_frame(212, RS_BOT + 12, 20, 22, 1, TL);
    t[0] = (char)('1' + rc.gear), t[1] = 0;
    n_texto(218, RS_BOT + 14, t, 1, sh ? NK : EY);
    for (i = 0; i < 5; i++)                             /* the gears: a cell each, lit up to this one */
        cv_rect(142 + i * 8, RS_BOT + 50, 6, 6, (uint32_t)i <= rc.gear ? (sh ? EY : NB) : TM);
    for (i = 0; i < 6; i++) {                           /* bottom left: the shift lights; all flashing at the change */
        int32_t on = rev > 500 + i * 80;
        uint16_t c = i < 3 ? EG : i < 5 ? EY : ER;
        cv_rect(8 + i * 14, RS_BOT + 68, 10, 8, sh ? ((now / 60u) & 1u ? ER : NB) : on ? c : TM);
    }
    n_texto(8, RS_BOT + 72 + 4, g_down(0) ? "BOOST" : "", 1, NO);
    cv_rect(150, RS_BOT + 64, 82, 24, RGB(0x06, 0x22, 0x2C)), n_frame(150, RS_BOT + 64, 82, 24, 1, TL);   /* the radar */
    for (i = 0; i < (int32_t)RC_N; i++) {
        int32_t dz = rc.rd[i] - rc.d, rx = 190 + rc.rl[i] / 6, ry = RS_BOT + 82 - dz / 60;
        if (ry > RS_BOT + 65 && ry < RS_BOT + 87)
            cv_rect(rx, ry, 3, 2, i == (int32_t)RC_N - 1 ? NO : ER);
    }
    cv_rect(190 + rc.lane / 6, RS_BOT + 82, 3, 3, EY);
}
static void rc_draw_space(int32_t y0, int32_t y1)
{
    uint32_t now = fm1_ms, el = now - rc.t0;
    int32_t x, y, k;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < RS_BOT; y++)
        cv_rect(0, y, 240, 1, NK);
    for (k = 0; k < 70; k++) {                          /* the stars, sliding with the bends */
        uint32_t h = g_hash(k, 1, 0x57A2u);
        x = (int32_t)(((h % 240u) + 2400u - (uint32_t)(rc.sky / 8) % 240u) % 240u), y = (int32_t)((h >> 8) % (uint32_t)(RS_BOT - 2));
        cv_pset(x, y, (h >> 20) % 4u == 0u ? EY : (h >> 20) % 4u == 1u ? NB : NW);
    }
    rs_galaxy(160 - (rc.sky / 24) % 300 + ((rc.sky / 24) % 300 < -140 ? -300 : 0), 34);
    for (k = 0; k < 40; k++) {                          /* under the road: stars rushing at you */
        uint32_t h = g_hash(k, 2, 0x57A3u);
        int32_t z = 1024 - (int32_t)(((h >> 8) % 1024u + (uint32_t)rc.d) % 1024u), sx = (int32_t)(h % 300u) - 150;
        if (z < 40)
            continue;
        x = 120 + sx * 60 / z * 2, y = RS_HZ + 4 + 2400 / z;
        if (y < RS_BOT && x >= 0 && x < 240)
            cv_rect(x, y, 1 + (z < 200), 1 + (z < 120), z < 200 ? NW : NB);
    }
    for (y = RS_HZ + 1; y < RS_BOT; y++) {              /* the road: dark, neon edges, green dashes, lamps */
        int32_t d = y - RS_HZ, z = 6000 / d, w = rs_w(y), cx = rs_cx(y), on = ((z + rc.d) / 40) & 1, dw = 1 + d / 14;
        if (y < y0 || y >= y1)
            continue;
        cv_rect(cx - w, y, 2 * w, 1, RGB(0x14, 0x12, 0x22));
        cv_rect(cx - w - 1 - d / 24, y, 2 + d / 24, 1, NO), cv_rect(cx + w - 1, y, 2 + d / 24, 1, NO);
        if ((y + (int32_t)(rc.d / 8)) & 1)              /* (their glow: a dither out from the edges) */
            cv_pset(cx - w - 3 - d / 24, y, ER), cv_pset(cx + w + 2 + d / 24, y, ER);
        if (on)
            cv_rect(cx - w / 3 - dw / 2, y, dw, 1, RGB(0x40, 0xF0, 0x90)), cv_rect(cx + w / 3 - dw / 2, y, dw, 1, RGB(0x40, 0xF0, 0x90));
        if (((z + rc.d) % 180) < 3 && d > 3) {          /* lamps on the edges: cyan, white hot */
            cv_rect(cx - w - 2 - d / 12, y - 1 - d / 20, 2 + d / 12, 2 + d / 20, RGB(0x60, 0xE0, 0xFF));
            cv_rect(cx + w, y - 1 - d / 20, 2 + d / 12, 2 + d / 20, RGB(0x60, 0xE0, 0xFF));
        }
        cv_rect(cx - w, y + 1, 2 * w, (y == RS_BOT - 1) ? 0 : 0, NK);
    }
    for (k = (int32_t)RC_N - 1; k >= 0; k--) {          /* the others, the far ones first */
        int32_t dz = rc.rd[k] - rc.d, yy, w;
        if (dz < -20 || dz > 1400)
            continue;
        yy = RS_HZ + (RS_BOT - RS_HZ) * 60 / (dz + 60);
        if (yy >= RS_BOT + 6 || yy <= RS_HZ)
            continue;
        w = rs_w(yy);
        g_ship_tint(rs_cx(yy) + rc.rl[k] * w / 120, yy - w / 14, 2 + w * 16 / 256, RC_COL[k][0], RC_COL[k][1]);
    }
    if (rc.v > 450)                                     /* fast: streaks down the sides */
        for (k = 0; k < 8; k++) {
            uint32_t h = g_hash(k, (int32_t)(now / 50u), 0x5BEu);
            int32_t sx = (h & 1u) ? 4 + (int32_t)((h >> 4) % 50u) : 186 + (int32_t)((h >> 4) % 50u), sy = 90 + (int32_t)((h >> 12) % 50u);
            cv_rect(sx, sy, 1, 6 + (int32_t)((h >> 20) % 10u), NW);
        }
    rs_dash();
    if (el < RC_GO) {                                   /* the countdown */
        const char *t = el < 1200u ? "ENGINES ON" : el < 2200u ? "3" : el < 3200u ? "2" : "1";
        uint32_t zz = el < 1200u ? 1u : 5u;
        n_texto(120 - n_w(t, zz) / 2, el < 1200u ? 100 : 76, t, zz, el < 1200u ? EY : NW);
        if (el >= 1200u)
            for (k = 0; k < 3; k++)
                n_disc(96 + k * 24, 30, 7, (int32_t)(el - 1200u) / 1000 >= k ? ER : RGB(0x40, 0x10, 0x10));
    } else if (el < RC_GO + 800u) {
        n_texto(120 - n_w("GO", 5) / 2, 76, "GO", 5, EG);
    }
}
static void rc_draw(int32_t y0, int32_t y1)
{
    uint32_t i, now = fm1_ms, el = now - rc.t0;
    int32_t x, y, k, ahead = 0;
    uint16_t lc = GCOL[g.pl.land & 7u], hc = GCOL[g.pl.land2 & 7u], wc = GCOL[g.pl.water & 7u];
    int32_t cloudy = (int32_t)g_vnoise(rc.d / 40, 3, g.pl.seed ^ 0xC10Du, 4, 0);   /* how cloudy this stretch (0..255) */
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < 240; y++) {
        if (y < RC_HZ) {                                /* the sky, its air at the bottom; the two ranges */
            int32_t t = y * 16 / RC_HZ;
            for (x = 0; x < 240; x++) {
                int32_t far = RC_HZ - 10 - (int32_t)(g_vnoise(x + rc.sky / 4, 3, g.pl.seed, 5, 0) * 30 / 255);
                int32_t near = RC_HZ - 2 - (int32_t)(g_vnoise(x * 2 + rc.sky, 9, g.pl.seed ^ 0x77u, 5, 0) * 26 / 255);
                uint16_t c = (int32_t)BAYER[y & 3][x & 3] < t - 6 ? (t > 13 ? wc : NP) : NK;
                if (y > far)
                    c = (int32_t)BAYER[y & 3][x & 3] < 9 ? NP : EN;
                if (y > near)
                    c = y < near + 3 && ((x + y) & 1) ? NW : (x & 3) == 0 && y < near + 8 ? lc : hc;   /* snow on top, lit lines */
                cv_pset(x, y, c);
            }
        } else {                                        /* the ground (small, slow), the road over it */
            int32_t d = y - RC_HZ + 1, z = 6000 / d, w = rc_w(y), cx = rc_cx(y), on = ((z + rc.d) / 40) & 1, dw = 1 + d / 16;
            for (x = 0; x < 240; x++) {
                int32_t gx = (x - cx) * 64 / (w + 8), cell = (((gx >> 3) ^ ((z + rc.d / 2) >> 4)) & 1);
                uint32_t hh = g_hash(gx >> 1, (z + rc.d / 2) >> 2, g.pl.seed);
                cv_pset(x, y, (hh & 15u) == 0u ? hc : cell ? lc : ((hh & 3u) == 1u ? hc : (BAYER[y & 3][x & 3] < 8 ? lc : EN)));
            }
            cv_rect(cx - w, y, 2 * w, 1, RGB(0x28, 0x26, 0x34));   /* the road, its edges, its dashes */
            cv_rect(cx - w - 1 - d / 20, y, 2 + d / 20, 1, on ? NW : ER), cv_rect(cx + w - 1, y, 2 + d / 20, 1, on ? NW : ER);
            if (on)
                cv_rect(cx - w / 3 - dw / 2, y, dw, 1, EY), cv_rect(cx + w / 3 - dw / 2, y, dw, 1, EY);
            if (((z + rc.d) % 260) < 3 && d > 4) {      /* posts with their lamps, both sides */
                int32_t ph = d / 2 + 4, l;
                for (l = -1; l <= 1; l += 2) {
                    int32_t px = cx + l * (w + 8 + d / 3);
                    cv_rect(px, y - ph, 1 + d / 30, ph, NB), cv_rect(l < 0 ? px : px - 2 - d / 12, y - ph, 3 + d / 12, 1 + d / 40, NB);
                    cv_rect(l < 0 ? px + 2 + d / 12 : px - 2 - d / 12, y - ph, 2 + d / 30, 2 + d / 30, EY);
                }
            }
        }
    }
    {   /* clouds: banks of them going by over the road; some stretches thick with them */
        int32_t n = 3 + (cloudy > 120 ? (cloudy - 120) * 16 / 135 : 0);
        for (k = 0; k < n; k++) {
            uint32_t h = g_hash(k, 5, 0xC1Du);
            int32_t z = 1024 - (int32_t)(((h >> 8) % 1024u + (uint32_t)rc.d / 2u) % 1024u), q = 1024 - z, s = 24 + q * q / 4400;
            int32_t sx = 120 + ((int32_t)(h % 400u) - 200) * s / 256 - rc.sky / 2, sy = RC_HZ - 6 + (int32_t)((h >> 20) % 60u) * s / 256 - 30 * q / 1024;
            bt_bill(sx, sy, 6 + s * 30 / 256, 4 + s * 12 / 256, 0, h, y0, y1);
        }
    }
    for (k = (int32_t)RC_N - 1; k >= 0; k--) {          /* the others on the road, the far ones first */
        int32_t dz = rc.rd[k] - rc.d, yy, w;
        if (dz > 0)
            ahead++;
        if (dz < -30 || dz > 1400)
            continue;
        yy = RC_HZ + 7000 / (dz + 52);
        if (yy >= 236 || yy <= RC_HZ)
            continue;
        w = rc_w(yy);
        g_ship_tint(rc_cx(yy) + rc.rl[k] * w / 120, yy - w / 10, 3 + w * 22 / 256, RC_COL[k][0], RC_COL[k][1]);
    }
    {   /* you: (the user) no shake: the jets behind swelling as the engines wake, then with the speed */
        int32_t pw = el < RC_GO ? (int32_t)el * 160 / (int32_t)RC_GO : 160 + rc.v * 96 / 900, j2, f = (int32_t)((now / 50u) % 3u);
        if (pw > 256)
            pw = 256;
        for (j2 = -1; j2 <= 1; j2 += 2) {               /* two jets: a glow, a core, flames streaming down */
            int32_t ex = 120 + j2 * 9, ey = 208, r = 1 + pw * 6 / 256 + (f == 1), ln = pw * 26 / 256 + f * 2, q;
            for (q = 0; q < ln; q++) {
                int32_t wq = r - q * r / (ln + 1);
                cv_rect(ex - wq, ey + q, wq * 2 + 1, 1, q < ln / 3 ? NW : q < ln * 2 / 3 ? RGB(0x60, 0xB0, 0xFF) : ((q + (int32_t)(now / 40u)) & 1) ? NB : EN);
            }
            n_disc(ex, ey, r, RGB(0x60, 0xB0, 0xFF)), n_disc(ex, ey, r / 2 + 1, NW);
        }
        g_ship_px(120, 204, 0, 24, 0);
        if (pw > 200 && rc.v > 500)                     /* fast: lines of speed at the sides */
            for (k = 0; k < 6; k++) {
                uint32_t h = g_hash(k, (int32_t)(now / 60u), 0x5BDu);
                int32_t sx = (h & 1u) ? 8 + (int32_t)((h >> 4) % 40u) : 192 + (int32_t)((h >> 4) % 40u), sy = 120 + (int32_t)((h >> 12) % 90u);
                cv_rect(sx, sy, 1, 10 + (int32_t)((h >> 20) % 14u), NW);
            }
    }
    if (el >= RC_GO) {   /* (the user) the speed: a number, the gear, its revs in cells (red at the top), SHIFT */
        char t[8];
        int32_t gt = (8 + g_spd() * 2 + rc.thr * 3) * 14 * (int32_t)(rc.gear + 1u) / 5, g0 = gt * (int32_t)rc.gear / (int32_t)(rc.gear + 1u);
        int32_t rev = gt > g0 ? (rc.v - g0) * 1000 / (gt - g0) : 0, sh = now - rc.shift_t < 300u;
        n_tex(4, 170, 76, 46, PT_CHECK, NK), n_frame(4, 170, 76, 46, 1, sh ? EY : EN);
        fmt_int(t, rc.v / 3);
        n_texto(8, 173, t, 2, NW);
        n_texto(8 + n_w(t, 2) + 3, 181, "U/S", 1, NP);
        n_knob(8, 200, 46, 10, rev, BK_GROW, NW, rev > 850 ? ER : rev > 600 ? NO : NB);
        cv_rect(58, 190, 20, 24, sh ? EY : NK), n_frame(58, 190, 20, 24, 1, NW);
        t[0] = (char)('1' + rc.gear), t[1] = 0;
        n_texto(63, 192, t, 2, sh ? NK : EY);
        if (sh)
            n_texto(8, 160, "SHIFT", 1, EY);
    }
    if (el < RC_GO) {                                   /* the countdown */
        const char *t = el < 1200u ? "ENGINES ON" : el < 2200u ? "3" : el < 3200u ? "2" : "1";
        uint32_t z = el < 1200u ? 1u : 6u;
        n_texto(120 - n_w(t, z) / 2, el < 1200u ? 120 : 92, t, z, el < 1200u ? EY : NW);
        if (el >= 1200u)                                /* the start lights */
            for (k = 0; k < 3; k++)
                n_disc(96 + k * 24, 60, 7, (int32_t)(el - 1200u) / 1000 >= k ? ER : RGB(0x40, 0x10, 0x10));
    } else if (el < RC_GO + 800u) {
        n_texto(120 - n_w("GO", 6) / 2, 92, "GO", 6, EG);
    }
    {   /* LAP 2/3, the time, your place */
        char t[16];
        uint32_t s = el < RC_GO ? 0u : ((rc.done_t ? rc.done_t : now) - rc.t0 - RC_GO) / 100u, lap = (uint32_t)(rc.d / RC_LAP) + 1u;
        str_cpy(t, "LAP ", 5), fmt_int(t + 4, (int32_t)(lap > 3u ? 3u : lap)), str_cpy(t + str_len(t), "/3", 3);
        n_texto(4, 22, t, 1, NW);
        fmt_int(t, (int32_t)(s / 600u)), str_cpy(t + str_len(t), ":", 2);
        t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s / 10u % 60u / 10u);
        t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s / 10u % 10u);
        t[str_len(t) + 1] = 0, t[str_len(t)] = '.';
        t[str_len(t) + 1] = 0, t[str_len(t)] = (char)('0' + s % 10u);
        n_texto(120 - n_w(t, 1) / 2, 22, t, 1, EY);
        fmt_int(t, (int32_t)rc.passed), str_cpy(t + str_len(t), " PASSED", 8);
        n_texto(236 - n_w(t, 1), 40, t, 1, NW);
        (void)ahead;
    }
    (void)i;
}
static void bt_draw(int32_t y0, int32_t y1)
{
    uint32_t i, now = fm1_ms;
    int32_t px = bt.px >> 8, x, y, j;
    bt_backdrop(y0, y1);
    if (bt.wave == BT_BOSS && !(bt.hp <= 0 && fm1_ms - bt.won_t > 1800u))
        bt_boss_draw(y0, y1);
    for (i = 0; i < bt.n; i++) {                        /* the little ships; flashing white when hit */
        bt_epos(i, &x, &y);
        if (bt.e[i].hp <= 0)
            continue;
        {
            uint32_t kind = (i + bt.wave) % 5u, fr = (now / 400u + i) & 1u;   /* (they wiggle: every other frame its legs out) */
            uint16_t c = now - bt.e[i].flash < 90u ? ER : BT_INVC[(bt.wave + kind) % 5u];
            for (j = 0; j < 88; j++) {
                uint32_t row = BT_INV[kind][j / 11];
                if (j / 11 == 7 && fr)
                    row = ((row << 1) | (row >> 1)) & 0x7FFu;
                if ((row >> (10 - j % 11)) & 1u)
                    cv_rect(x - 11 + (j % 11) * 2, y - 8 + (j / 11) * 2, 2, 2, c);
            }
        }
    }
    for (i = 0; i < BT_NM; i++) {                       /* the shots */
        if (!bt.m[i].on)
            continue;
        x = bt.m[i].x >> 8, y = bt.m[i].y >> 8;
        if (bt.m[i].kind == 0u)
            bt_orb(x, y, 1);
        else if (bt.m[i].kind == 3u)
            cv_rect(x - 2, y - 2, 5, 4, (now / 60u) & 1u ? NO : ER), cv_rect(x - 1, y - 1, 3, 2, BT_GREY[1]);
        else if (bt.m[i].kind == 4u) {                  /* a wingman's orb: as theirs, smaller, blue / purple */
            cv_rect(x - 2, y - 2, 5, 5, RGB(0x10, 0x10, 0x40));
            cv_rect(x - 1, y - 1, 3, 3, (i & 1u) ? NP : RGB(0x30, 0x70, 0xF0));
            cv_pset(x, y, RGB(0xB0, 0xE0, 0xFF));
        } else
            bt_bolt(x, y, (int)bt.m[i].kind);
    }
    bt_sparks_draw();
    for (i = 0; i < 8u; i++)                            /* the wingmen's lasers: white lines to their marks */
        if (bt_zl[i].t && now - bt_zl[i].t < 160u) {
            cv_line(bt_zl[i].x0, bt_zl[i].y0, bt_zl[i].x1, bt_zl[i].y1, now - bt_zl[i].t < 80u ? NW : RGB(0x90, 0xA0, 0xC0));
            cv_rect(bt_zl[i].x1 - 1, bt_zl[i].y1 - 1, 3, 3, NW);
        }
    if (bt_wings() > 0) {                               /* (the user) more keys: a big laser weaving up from you */
        int32_t yy;
        for (yy = BT_Y - 12; yy > 20; yy -= 2) {
            int32_t wx = px + ((g_sin((uint32_t)(yy * 9 + (int32_t)(now / 3u)) & 1023u) * 7) >> 15);
            cv_rect(wx - 2, yy, 5, 2, RGB(0x20, 0x70, 0x10)), cv_rect(wx - 1, yy, 3, 2, RGB(0x70, 0xE0, 0x30)), cv_rect(wx, yy, 1, 2, RGB(0xD0, 0xFF, 0xA0));
        }
    }
    if (!(now - bt.hit_t < 900u && ((now / 90u) & 1u))) {   /* you, and your wingmen */
        {
            int32_t ogz = gz, ogw = gw, ogh = gh, w, nw2 = bt_wings();
            gz = 1, gw = gh = 240;
            for (w = 0; w <= nw2; w++) {
                int32_t wx = px + (w == 0 ? 0 : (w & 1 ? -1 : 1) * 28 * ((w + 1) / 2)), wy = BT_Y + bt_kick + (w ? 8 : 0);
                bt_me(wx, wy, w ? 1 : 2);
            }
            gz = ogz, gw = ogw, gh = ogh;
            if (bt_kick)
                bt_kick--;
        }
    }
    if (bt.wave == BT_BOSS)
        bt_hp_draw();
}

/* ------------------------------------------------------- the fight in space --- */
/* (the user) KNOB 3 out of the battle: the fight goes on out here, from above. Their little ships (red) chase you and
 * fire; two of yours (blue) fly with you, chasing them and firing white lasers; the boss (if it was the boss) comes
 * after you slowly, and your keys fire green lasers at it (else at the nearest of them). Everyone leaves a trail.
 * Fast enough and you leave them all behind; too slow and the little ones keep up */
#define SF_NA 2u
#define SF_TR 10u
#define SF_NB 24u                                       /* the boss's shots */
#define SF_LIFE 30000u                                  /* (the user) the boss always goes down in 30 s */
static struct {
    uint8_t boss, ti, raid_left, noal;                  /* (the user) pirates still to come; no allies of the fight's own */
    int8_t fhp[G_NSEN];
    uint32_t alert_t;                                   /* the pirates' warning, since */
    int32_t bx, by;                                     /* the boss (Q8 world px) */
    uint32_t b0, bdie, tt, notes, spawn, burst;         /* the boss since, its end (blowing up), the next of theirs, its next burst */
    struct { int32_t x, y, vx, vy; uint32_t shot; } al[SF_NA];
    struct { int32_t x, y, vx, vy; uint32_t t; } bl[SF_NB];   /* its shots, loose (Q8 world px, Q8 px / s) */
    struct { int16_t x, y; uint32_t t; } ex[6];         /* bursts: a ship gone */
    int16_t tr[G_NSEN + SF_NA][SF_TR][2];               /* trails: the last places (world px) */
    struct { int16_t x0, y0, x1, y1; uint16_t c; uint32_t t; } zl[6];
} sf;
static void sf_zap(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* a laser line, a moment (world px) */
{
    uint32_t q;
    for (q = 0; q < 5u && sf.zl[q].t && fm1_ms - sf.zl[q].t < 150u; q++)
        ;
    sf.zl[q].x0 = (int16_t)x0, sf.zl[q].y0 = (int16_t)y0, sf.zl[q].x1 = (int16_t)x1, sf.zl[q].y1 = (int16_t)y1, sf.zl[q].c = c, sf.zl[q].t = fm1_ms;
}
static void sf_boom(int32_t x, int32_t y)               /* a burst where a ship was (world px) */
{
    uint32_t q, o = 0;
    for (q = 1; q < 6u; q++)
        if (sf.ex[q].t < sf.ex[o].t)
            o = q;
    sf.ex[o].x = (int16_t)x, sf.ex[o].y = (int16_t)y, sf.ex[o].t = fm1_ms | 1u;
}
static void sf_hurt(void)                               /* a shot on you: a unit of cargo lost */
{
    uint32_t k, m = 0;
    if (fm1_ms - g.hit_t < 900u)
        return;
    g.hit_t = fm1_ms;
    for (k = 1; k < 4u; k++)
        if (g.res[k] > g.res[m])
            m = k;
    if (g.res[m])
        g.res[m]--, g_say("HIT", "CARGO LOST");
    else
        g_say("HIT", "NOTHING TO LOSE");
}
static void sf_hit(uint32_t i)                          /* one of theirs hit: two and it is gone */
{
    uint32_t l = g.foe_n - 1u, q;
    foe_hit_t[i] = fm1_ms;
    if (--sf.fhp[i] > 0)
        return;
    sf_boom(g.foe[i].x >> 8, g.foe[i].y >> 8);
    g.foe[i] = g.foe[l], sf.fhp[i] = sf.fhp[l], foe_look[i] = foe_look[l], foe_hit_t[i] = foe_hit_t[l];
    for (q = 0; q < SF_TR; q++)
        sf.tr[i][q][0] = sf.tr[l][q][0], sf.tr[i][q][1] = sf.tr[l][q][1];
    g.foe_n--;
}
static int32_t sf_near(int32_t x, int32_t y)           /* the nearest of theirs to (x, y) world px, -1 none */
{
    uint32_t i;
    int32_t best = -1, bd = 1 << 30;
    for (i = 0; i < g.foe_n; i++) {
        int32_t dx = (g.foe[i].x >> 8) - x, dy = (g.foe[i].y >> 8) - y;
        if (dx * dx + dy * dy < bd)
            bd = dx * dx + dy * dy, best = (int32_t)i;
    }
    return best;
}
static void sf_add(void)                                /* one more of theirs, from out of the view */
{
    uint32_t i = g.foe_n, a = g_hash((int32_t)fm1_ms, (int32_t)i, 0x5F1u) & 1023u, q;
    int32_t x, y;
    if (i >= G_NSEN)
        return;
    x = g.sx + (g_cos(a) >> 7) * 75, y = g.sy + (g_sin(a) >> 7) * 75;   /* (just out of the view: in sight soon) */
    g.foe[i].x = x, g.foe[i].y = y, g.foe[i].vx = g.foe[i].vy = 0, g.foe[i].shot = fm1_ms + 1500u, sf.fhp[i] = (int8_t)(sf.noal ? 6 : 2);   /* (pirates: tougher) */
    foe_look[i] = (uint8_t)((1u + (a >> 2) % 4u) | ((g_hash((int32_t)fm1_ms, (int32_t)i, 0xC01u) % 8u) << 3)), foe_hit_t[i] = 0;   /* (a ship, a colour: each its own) */
    for (q = 0; q < SF_TR; q++)
        sf.tr[i][q][0] = (int16_t)(x >> 8), sf.tr[i][q][1] = (int16_t)(y >> 8);
    g.foe_n++;
}
static void sf_start(int boss)                          /* (the user) the fight, out in space: the boss too, or not */
{
    uint32_t q;
    for (q = 0; q < SF_NA; q++)
        sf.al[q].x = g.sx + (g_cos(g.ang + 300u + q * 424u) >> 7) * 18, sf.al[q].y = g.sy + (g_sin(g.ang + 300u + q * 424u) >> 7) * 18,
        sf.al[q].vx = g.vx, sf.al[q].vy = g.vy, sf.al[q].shot = fm1_ms + 500u + q * 250u;
    for (q = 0; q < (G_NSEN + SF_NA) * SF_TR; q++)
        sf.tr[q / SF_TR][q % SF_TR][0] = (int16_t)(g.sx >> 8), sf.tr[q / SF_TR][q % SF_TR][1] = (int16_t)(g.sy >> 8);
    for (q = 0; q < SF_NB; q++)
        sf.bl[q].t = 0;
    g.foe_n = 0;
    for (q = 0; q < 3u; q++)
        sf_add();
    g.foe_t = fm1_ms;
    sf.boss = (uint8_t)(boss != 0), sf.bdie = 0, sf.b0 = sf.spawn = sf.burst = fm1_ms + 1500u;
    if (boss) {
        bt_boss_make(fm1_ms);
        sf.bx = g.sx + (g_cos(g.ang) >> 7) * 60, sf.by = g.sy + (g_sin(g.ang) >> 7) * 60;
        g_say("WARNING", "SOMETHING COLOSSAL");
    }
    sf_on = 1, sf.notes = fm1_in.notes, sf.ti = 0, sf.raid_left = 0, sf.noal = 0;
}
/* (the user) pirates come into the system after you: n of them (2 to 6), three at a time, the rest once those are down;
 * a warning on the screen. Your escorts (held keys) fight with you */
static void sf_raid(uint32_t n)
{
    uint32_t k;
    sf_start(0);
    g.foe_n = 0, sf.noal = 1;
    for (k = 0; k < n && k < 3u; k++)
        sf_add();
    sf.raid_left = (uint8_t)(n - k), sf.alert_t = fm1_ms | 1u;
    g_say("PIRATES!", "ENTERING THE SYSTEM");
}
static void sf_burn(void)                               /* your laser on the boss: a cell of its hull catches fire */
{
    uint32_t h = g_hash((int32_t)fm1_ms, 7, 0xB0Bu), k;
    for (k = 0; k < 24u; k++, h = h * 1103515245u + 12345u) {
        uint32_t x = (h >> 8) % (uint32_t)BT_BW, y = (h >> 20) % (uint32_t)BT_BH;
        if (bt.hull[y][x] && bt.hull[y][x] < 5u) {
            bt.hull[y][x] = (uint8_t)(12u - (h & 3u));
            if (x + 1u < (uint32_t)BT_BW && bt.hull[y][x + 1u])
                bt.hull[y][x + 1u] = 10;
            return;
        }
    }
}
static uint32_t g_atan2(int32_t dy, int32_t dx)         /* the angle of (dx, dy), 1024 a turn: the best of 128 */
{
    uint32_t a, best = 0;
    int32_t bd = -(1 << 30);
    for (a = 0; a < 1024u; a += 8u) {
        int32_t d = (dx * (g_cos(a) >> 7) + dy * (g_sin(a) >> 7));
        if (d > bd)
            bd = d, best = a;
    }
    return best;
}
static void sf_fire(int32_t x, int32_t y, uint32_t a, int32_t v)   /* one of the boss's shots, angle a, v px / s */
{
    uint32_t q;
    for (q = 0; q < SF_NB && sf.bl[q].t; q++)
        ;
    if (q == SF_NB)
        return;
    sf.bl[q].x = x << 8, sf.bl[q].y = y << 8, sf.bl[q].vx = (g_cos(a) >> 7) * v, sf.bl[q].vy = (g_sin(a) >> 7) * v, sf.bl[q].t = fm1_ms | 1u;
}
static void sf_tick(uint32_t dt)
{
    uint32_t i, nw = fm1_in.notes & ~sf.notes, now = fm1_ms;
    int32_t mx = g.sx >> 8, my = g.sy >> 8;
    sf.notes = fm1_in.notes;
    if (!sf_on)
        return;
    if (sf.boss && !sf.bdie) {                          /* the boss: round you slowly, firing loose bursts */
        int32_t bx = sf.bx >> 8, by = sf.by >> 8, dx = mx - bx, dy = my - by, d = g_isqrt(dx * dx + dy * dy) + 1;
        uint32_t orb = now / 14u;
        int32_t tx = mx + ((g_cos(orb) * 34) >> 15), ty = my + ((g_sin(orb) * 24) >> 15), ex = tx - bx, ey = ty - by, ed = g_isqrt(ex * ex + ey * ey) + 1;
        int32_t sp = ed > 20 ? 30 : ed;
        sf.bx += ex * sp * 256 / ed * (int32_t)dt / 1000, sf.by += ey * sp * 256 / ed * (int32_t)dt / 1000;
        if ((int32_t)(now - sf.burst) >= 0) {           /* a ring of 10, turning a little each time; every third, aimed */
            uint32_t a0 = now / 3u, k;
            if ((now / 1000u) % 3u == 0u) {
                uint32_t at = g_atan2(dy, dx);
                for (k = 0; k < 5u; k++)
                    sf_fire(bx, by, at + k * 24u - 48u, 62);
            } else {
                for (k = 0; k < 10u; k++)
                    sf_fire(bx, by, a0 + k * 102u, 44);
            }
            sf.burst = now + 950u;
        }
        if (d > 300)
            sf.boss = 0, g_say("THE BOSS", "LOST YOU");
        else if (now - sf.b0 > SF_LIFE && (int32_t)(now - sf.b0) > 0)
            sf.bdie = now | 1u, g_say("BOSS DOWN", "+250 CREDITS"), g.cred = (uint16_t)(g.cred + 250u), g.dirty = 1;
    }
    if (sf.bdie) {                                      /* blowing up: bursts all over it, then gone, theirs with it */
        if (((now / 90u) & 1u))
            sf_boom((sf.bx >> 8) - 15 + (int32_t)(g_hash((int32_t)now, 1, 9u) % 30u), (sf.by >> 8) - 6 + (int32_t)(g_hash((int32_t)now, 2, 9u) % 12u));
        if (now - sf.bdie > 1600u) {
            sf.boss = 0, sf.bdie = 0;
            while (g.foe_n)
                sf.fhp[0] = 1, sf_hit(0);
        }
    }
    if (sf.boss && !sf.bdie && g.foe_n < 3u && (int32_t)(now - sf.spawn) >= 0)   /* (the user) new ones keep coming */
        sf_add(), sf.spawn = now + 2600u;
    for (i = 0; i < SF_NB; i++) {                       /* its shots: 2.4 s each; one on you hurts */
        int32_t dx, dy;
        if (!sf.bl[i].t)
            continue;
        if (now - sf.bl[i].t > 2400u) {
            sf.bl[i].t = 0;
            continue;
        }
        sf.bl[i].x += sf.bl[i].vx * (int32_t)dt / 1000, sf.bl[i].y += sf.bl[i].vy * (int32_t)dt / 1000;
        dx = (sf.bl[i].x - g.sx) >> 8, dy = (sf.bl[i].y - g.sy) >> 8;
        if (dx * dx + dy * dy <= 9)
            sf.bl[i].t = 0, sf_hurt();
    }
    if (nw) {                                           /* your keys: green lasers at the boss, or at the nearest of them */
        int32_t f = sf_near(mx, my);
        if (sf.boss && !sf.bdie) {
            sf_zap(mx, my, (sf.bx >> 8) + (int32_t)(g_hash((int32_t)now, 1, 3u) % 24u) - 12, (sf.by >> 8) + (int32_t)(g_hash((int32_t)now, 2, 3u) % 8u) - 4, NW);
            sf_burn();
            if (g.wpn)
                sf_burn();
        } else if (f >= 0) {
            sf_zap(mx, my, g.foe[f].x >> 8, g.foe[f].y >> 8, NW);
            sf_hit((uint32_t)f);
        }
    }
    for (i = 0; i < SF_NA && !sf.noal; i++) {          /* yours: after the nearest of theirs, firing */
        int32_t f = sf_near(sf.al[i].x >> 8, sf.al[i].y >> 8), tx, ty, dx, dy, d;
        uint32_t orb = now / 5u + i * 512u;
        if (f >= 0)
            tx = (g.foe[f].x >> 8) + ((g_cos(orb) * 26) >> 15), ty = (g.foe[f].y >> 8) + ((g_sin(orb) * 26) >> 15);
        else
            tx = mx + ((g_cos(orb) * 16) >> 15), ty = my + ((g_sin(orb) * 16) >> 15);
        dx = tx - (sf.al[i].x >> 8), dy = ty - (sf.al[i].y >> 8), d = g_isqrt(dx * dx + dy * dy) + 1;
        sf.al[i].vx += (dx * 150 * 256 / d + g.vx / 2 - sf.al[i].vx) * (int32_t)dt / 250;
        sf.al[i].vy += (dy * 150 * 256 / d + g.vy / 2 - sf.al[i].vy) * (int32_t)dt / 250;
        sf.al[i].x += sf.al[i].vx * (int32_t)dt / 1000, sf.al[i].y += sf.al[i].vy * (int32_t)dt / 1000;
        if (f >= 0 && (int32_t)(now - sf.al[i].shot) >= 0) {
            int32_t ex = g.foe[f].x >> 8, ey = g.foe[f].y >> 8, ax = ex - (sf.al[i].x >> 8), ay = ey - (sf.al[i].y >> 8);
            if (ax * ax + ay * ay < 70 * 70) {
                sf_zap(sf.al[i].x >> 8, sf.al[i].y >> 8, ex, ey, NW);
                sf_hit((uint32_t)f);
            }
            sf.al[i].shot = now + 900u;
        }
    }
    if (now - sf.tt > 70u) {                            /* the trails */
        uint32_t q = sf.ti++ % SF_TR;
        sf.tt = now;
        for (i = 0; i < G_NSEN; i++)
            sf.tr[i][q][0] = (int16_t)(g.foe[i].x >> 8), sf.tr[i][q][1] = (int16_t)(g.foe[i].y >> 8);
        for (i = 0; i < SF_NA; i++)
            sf.tr[G_NSEN + i][q][0] = (int16_t)(sf.al[i].x >> 8), sf.tr[G_NSEN + i][q][1] = (int16_t)(sf.al[i].y >> 8);
    }
    if (!g.foe_n && !sf.boss && sf.raid_left) {         /* (the user) the first three down: the rest of them come */
        uint32_t k;
        for (k = 0; k < sf.raid_left && k < 3u; k++)
            sf_add();
        sf.raid_left = (uint8_t)(sf.raid_left - k), sf.alert_t = now | 1u;
        g_say("MORE PIRATES", "INCOMING");
    }
    if (!g.foe_n && !sf.boss)                           /* nobody left after you: over */
        sf_on = 0, g_say("CLEAR", "THE FIGHT IS OVER");
}
static void sf_draw(void)
{
    uint32_t i, k, now = fm1_ms;
    int32_t x, y, x1, y1;
    if (!sf_on)
        return;
    for (i = 0; i < G_NSEN + SF_NA; i++) {              /* the trails: theirs orange, yours white and blue */
        int ally = i >= G_NSEN;
        if ((!ally && i >= g.foe_n) || (ally && sf.noal))
            continue;
        for (k = 1; k < SF_TR; k++) {
            uint32_t a = (sf.ti + k) % SF_TR, b = (sf.ti + k - 1u) % SF_TR;
            g_w2s(sf.tr[i][a][0], sf.tr[i][a][1], &x, &y), g_w2s(sf.tr[i][b][0], sf.tr[i][b][1], &x1, &y1);
            if ((x - x1) * (x - x1) + (y - y1) * (y - y1) < 400)
                gline(x, y, x1, y1, ally ? (k > SF_TR / 2 ? NW : NB) : (k > SF_TR / 2 ? NO : ER));
        }
    }
    for (i = 0; i < SF_NA && !sf.noal; i++) {          /* yours: little blue triangles, along their way */
        int32_t vx = sf.al[i].vx >> 8, vy = sf.al[i].vy >> 8, v, dx, dy, t;
        g_w2s(sf.al[i].x >> 8, sf.al[i].y >> 8, &x, &y);
        if (gsr || gcr != 32767)                        /* (turned with the view) */
            t = (vx * gcr - vy * gsr) >> 15, vy = (vx * gsr + vy * gcr) >> 15, vx = t;
        v = g_isqrt(vx * vx + vy * vy) + 1;
        for (dy = -2; dy <= 2; dy++)
            for (dx = -2; dx <= 2; dx++) {
                int32_t u = (dx * vx + dy * vy) * 4 / v, w = (-dx * vy + dy * vx) * 4 / v;
                w = w < 0 ? -w : w;
                if (u >= -6 && u <= 8 && w * 2 <= 8 - u)
                    gp(x + dx, y + dy, u > 4 ? NW : NB);
            }
    }
    if (sf.boss) {                                      /* the boss: its hull, a quarter, burning where hit */
        int32_t cx, cy;
        for (cy = 0; cy < BT_BH; cy += 4)
            for (cx = 0; cx < BT_BW; cx += 4) {
                uint32_t c = bt.hull[cy][cx];
                if (!c)
                    continue;
                g_w2s((sf.bx >> 8) + (cx - BT_BW / 2) / 4, (sf.by >> 8) + (cy - BT_BH / 2) / 4, &x, &y);
                gp(x, y, c < 4u ? BT_GREY[c] : c == 4u ? RGB(0x5A, 0xE6, 0xF0) :
                         (g_hash(cx, cy, now / 90u) & 1u) ? (c > 9u ? RGB(0xFF, 0xB0, 0x48) : RGB(0xF8, 0x68, 0x10)) : RGB(0xB8, 0x30, 0x10));
            }
    }
    for (i = 0; i < SF_NB; i++)                         /* its shots: loose, orange with a pale core, blinking */
        if (sf.bl[i].t) {
            g_w2s(sf.bl[i].x >> 8, sf.bl[i].y >> 8, &x, &y);
            gp(x, y, (now / 70u + i) & 1u ? RGB(0xFF, 0xF4, 0xB0) : NO);
            gp(x + 1, y, NO), gp(x, y + 1, ER);
        }
    for (i = 0; i < 6u; i++)                            /* bursts: a ring of sparks, opening */
        if (sf.ex[i].t && now - sf.ex[i].t < 500u) {
            int32_t r = 2 + (int32_t)(now - sf.ex[i].t) / 40;
            g_w2s(sf.ex[i].x, sf.ex[i].y, &x, &y);
            for (k = 0; k < 8u; k++)
                gp(x + ((g_cos(k * 128u + i * 40u) * r) >> 15), y + ((g_sin(k * 128u + i * 40u) * r) >> 15), r < 8 ? EY : r < 12 ? NO : ER);
        }
    if (sf.alert_t && now - sf.alert_t < 2600u && ((now / 180u) & 1u)) {   /* (the user) the pirates' warning */
        cv_rect(40, 44, 160, 22, NK), n_frame(40, 44, 160, 22, 2, ER);
        n_texto(120 - n_w("! PIRATES !", 1) / 2, 47, "! PIRATES !", 1, ER);
    }
    {   /* the lasers: lines in screen px (so they are thin at any zoom) */
        int32_t ogz = gz;
        for (i = 0; i < 6u; i++)
            if (sf.zl[i].t && now - sf.zl[i].t < 150u) {
                g_w2s(sf.zl[i].x0, sf.zl[i].y0, &x, &y), g_w2s(sf.zl[i].x1, sf.zl[i].y1, &x1, &y1);
                x = x * ogz + ogz / 2, y = y * ogz + ogz / 2, x1 = x1 * ogz + ogz / 2, y1 = y1 * ogz + ogz / 2;
                cv_line(x, y, x1, y1, now - sf.zl[i].t < 70u ? sf.zl[i].c : RGB(0x60, 0x70, 0x90));
                cv_line(x + 1, y, x1 + 1, y1, now - sf.zl[i].t < 70u ? sf.zl[i].c : RGB(0x60, 0x70, 0x90));
                cv_rect(x1 - 1, y1 - 1, 3, 3, NW);
            }
    }
}

/* (the user) out in space: hold keys and little ships of yours come, one a key held (up to four), and stay 5 s after
 * you let go. With nobody about they wander round you; with ships of theirs about they hunt them (the nearest each),
 * firing on their own; their trail a long white line, always. Every key struck fires too: yours white, at the big one
 * if there is one (the fight's own), else at the nearest in sight; theirs white, yellow, blue */
#define ES_N 4u
static struct { int32_t x, y, vx, vy; uint32_t keep, shot; uint8_t on; } es[ES_N];   /* on: flying; keep: last wanted */
#define ES_TR 32u                                       /* (the user) their trails: long */
static int16_t es_tr[ES_N][ES_TR][2];
static uint8_t es_ti;
static uint32_t es_tt, es_notes;
static const uint16_t ES_SHOT[ES_N] = {NW, EY, NB, NW};
static void es_tick(uint32_t dt)
{
    uint32_t i, n = fm1_in.notes, held = 0, nw = fm1_in.notes & ~es_notes, now = fm1_ms;
    int32_t mx = g.sx >> 8, my = g.sy >> 8, fx = g_cos(g.ang) >> 7, fy = g_sin(g.ang) >> 7;   /* (Q8: ahead) */
    es_notes = fm1_in.notes;
    while (n)
        held += n & 1u, n >>= 1;
    if (g.mode != GM_SPACE)
        held = 0, nw = 0;
    for (i = 0; i < ES_N; i++) {
        int32_t tx, ty, f, ex, ey;
        uint32_t orb = now / 11u + i * 256u;
        if (i < held) {                                 /* called (or kept): it comes in from behind */
            if (!es[i].on) {
                uint32_t q;
                es[i].x = g.sx - fx * 40, es[i].y = g.sy - fy * 40, es[i].vx = g.vx, es[i].vy = g.vy, es[i].on = 1;
                for (q = 0; q < ES_TR; q++)
                    es_tr[i][q][0] = (int16_t)(es[i].x >> 8), es_tr[i][q][1] = (int16_t)(es[i].y >> 8);
            }
            es[i].keep = now;
        }
        if (!es[i].on)
            continue;
        if (now - es[i].keep > 5000u) {                 /* 5 s after the key: gone */
            es[i].on = 0;
            continue;
        }
        ex = es[i].x >> 8, ey = es[i].y >> 8, f = sf_near(ex, ey);
        if (f >= 0 && ((g.foe[f].x >> 8) - mx) * ((g.foe[f].x >> 8) - mx) + ((g.foe[f].y >> 8) - my) * ((g.foe[f].y >> 8) - my) > 70 * 70)
            f = -1;                                     /* (they hunt near you, where you see it) */
        if (f >= 0)                                     /* hunting: round the nearest of theirs */
            tx = (g.foe[f].x >> 8) + ((g_cos(orb * 2u) * 18) >> 15), ty = (g.foe[f].y >> 8) + ((g_sin(orb * 2u) * 18) >> 15);
        else                                            /* about you, wandering round */
            tx = mx + ((g_cos(orb) * (26 + (int32_t)i * 7)) >> 15), ty = my + ((g_sin(orb) * (26 + (int32_t)i * 7)) >> 15);
        {
            int32_t dx = tx - ex, dy = ty - ey, d = g_isqrt(dx * dx + dy * dy) + 1, sp = d > 40 ? 160 : d * 4;
            int32_t wx = dx * sp * 256 / d + (f < 0 ? g.vx : 0), wy = dy * sp * 256 / d + (f < 0 ? g.vy : 0);
            es[i].vx += (wx - es[i].vx) * (int32_t)dt / 200, es[i].vy += (wy - es[i].vy) * (int32_t)dt / 200;
        }
        es[i].x += es[i].vx * (int32_t)dt / 1000, es[i].y += es[i].vy * (int32_t)dt / 1000;
        if (f >= 0) {                                   /* their shots: on their own, or with a key struck */
            int32_t qx = (g.foe[f].x >> 8) - ex, qy = (g.foe[f].y >> 8) - ey;
            if (qx * qx + qy * qy < 55 * 55 && (nw || (int32_t)(now - es[i].shot) >= 0)) {
                sf_zap(ex, ey, g.foe[f].x >> 8, g.foe[f].y >> 8, ES_SHOT[i]), sf_hit((uint32_t)f);
                es[i].shot = now + 650u;
            }
        } else if (nw) {
            int32_t vx = es[i].vx >> 8, vy = es[i].vy >> 8, v = g_isqrt(vx * vx + vy * vy) + 1;
            sf_zap(ex, ey, ex + vx * 50 / v, ey + vy * 50 / v, ES_SHOT[i]);
        }
    }
    if (nw && !sf_on) {                                 /* yours (the fight fires its own): white, at one in sight, else ahead */
        int32_t f = sf_near(mx, my);
        if (f >= 0 && ((g.foe[f].x >> 8) - mx) * ((g.foe[f].x >> 8) - mx) + ((g.foe[f].y >> 8) - my) * ((g.foe[f].y >> 8) - my) < 60 * 60)
            sf_zap(mx, my, g.foe[f].x >> 8, g.foe[f].y >> 8, NW), sf_hit((uint32_t)f);
        else
            sf_zap(mx + fx * 5 / 256, my + fy * 5 / 256, mx + fx * 70 / 256, my + fy * 70 / 256, NW);
    }
    if (now - es_tt > 70u) {                            /* the trails */
        uint32_t q = es_ti++ % ES_TR;
        es_tt = now;
        for (i = 0; i < ES_N; i++)
            es_tr[i][q][0] = (int16_t)(es[i].x >> 8), es_tr[i][q][1] = (int16_t)(es[i].y >> 8);
    }
}
static void es_draw(void)
{
    uint32_t i, k, now = fm1_ms;
    int32_t x, y, x1, y1;
    for (i = 0; i < ES_N; i++) {
        int32_t vx = es[i].vx >> 8, vy = es[i].vy >> 8, v, dx, dy;
        if (!es[i].on)
            continue;
        for (k = 1; k < ES_TR; k++) {                   /* its trail: (the user) long and bright: white near, cyan, blue
                                                         * far; two pixels wide where it is new */
            uint32_t a = (es_ti + k) % ES_TR, b = (es_ti + k - 1u) % ES_TR;
            g_w2s(es_tr[i][a][0], es_tr[i][a][1], &x, &y), g_w2s(es_tr[i][b][0], es_tr[i][b][1], &x1, &y1);
            if ((x - x1) * (x - x1) + (y - y1) * (y - y1) < 900)
                gline(x, y, x1, y1, NW);                /* (the user) a white line, always */
        }
        g_w2s(es[i].x >> 8, es[i].y >> 8, &x, &y);
        v = g_isqrt(vx * vx + vy * vy);
        if (v < 4)
            vx = g_cos(g.ang) >> 10, vy = g_sin(g.ang) >> 10, v = g_isqrt(vx * vx + vy * vy);
        v++;
        for (dy = -2; dy <= 2; dy++)                    /* (the user) small and white: an arrow along its way */
            for (dx = -2; dx <= 2; dx++) {
                int32_t u = (dx * vx + dy * vy) * 4 / v, w = (-dx * vy + dy * vx) * 4 / v;
                w = w < 0 ? -w : w;
                if (u >= -5 && u <= 7 && w * 2 <= 7 - u)
                    gp(x + dx, y + dy, NW);
            }
    }
    if (sf_on)                                          /* (the fight draws its own lasers) */
        return;
    for (i = 0; i < 6u; i++)
        if (sf.zl[i].t && now - sf.zl[i].t < 150u) {
            g_w2s(sf.zl[i].x0, sf.zl[i].y0, &x, &y), g_w2s(sf.zl[i].x1, sf.zl[i].y1, &x1, &y1);
            x = x * gz + gz / 2, y = y * gz + gz / 2, x1 = x1 * gz + gz / 2, y1 = y1 * gz + gz / 2;
            cv_line(x, y, x1, y1, now - sf.zl[i].t < 70u ? sf.zl[i].c : RGB(0x60, 0x70, 0x90));
            cv_rect(x1 - 1, y1 - 1, 3, 3, NW);
        }
}

/* KNOB 3 all the way out, in space: the system from far above, after the game's own system view (tilted, its orbits
 * as ellipses). The sun in the middle in its colour, each planet's orbit and the planet where it is now (they turn), the
 * station on a pin, the ship (an arrow, at the rim when it is out of the system); big rings round it all, an orange arc
 * among them; the system's name on top, how far the core is under it. Drawn in screen px into the band (cv_oy set) */
#define SV_X 120
#define SV_Y 140
/* (the user) the system view opens over the screen like a projection: from small and see-through (KNOB 3 past the
 * widest) to its size, solid. sv_k its scale (Q8, round its middle), sv_a how much of each pixel shows (of 16) */
static int32_t sv_k = 256, sv_a = 16;
static void svp(int32_t x, int32_t y, uint16_t c)
{
    x = SV_X + (x - SV_X) * sv_k / 256, y = SV_Y + (y - SV_Y) * sv_k / 256;
    if (sv_a >= 16 || (int32_t)BAYER[y & 3][x & 3] < sv_a)
        cv_pset(x, y, c);
}
static void sv_pt(int32_t wx, int32_t wy, int32_t ux, int32_t uy, int32_t *x, int32_t *y)   /* world -> the tilted view */
{
    *x = SV_X + (wx - ux) / 2, *y = SV_Y + (wy - uy) * 21 / 100;
}
static void sv_ell(int32_t rx, int32_t ry, uint32_t step, uint32_t on, uint16_t c)   /* an ellipse, dotted: on of each step */
{
    uint32_t a;
    for (a = 0; a < 1024u; a += step / 4u ? step / 4u : 1u)
        if (((a / (step / 4u ? step / 4u : 1u)) % 4u) < on)
            svp(SV_X + ((g_cos(a) * rx) >> 15), SV_Y + ((g_sin(a) * ry) >> 15), c);
}
static void g_sysview(int32_t y0, int32_t y1)
{
    int32_t sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8), ux, uy, i, j, k, x, y, qx, qy;
    uint16_t sc = g_star_col(sx, sy);
    char t[28];
    planet_t p;
    g_sun_at(sx, sy, &ux, &uy);
    sv_ell(112, 48, 4, 4, NW);                          /* the rings: a white one, a dashed one out of it, an orange arc */
    sv_ell(119, 53, 16, 2, NB);
    {
        uint32_t a;
        for (a = 560u; a < 980u; a += 2u)
            svp(SV_X + ((g_cos(a) * 104) >> 15), SV_Y + ((g_sin(a) * 44) >> 15), NO);
    }
    for (k = 0; k < (int32_t)G_NPL && g_planet(sx, sy, (uint32_t)k, &p); k++) {   /* the orbits */
        int32_t dx = (p.x - ux) / 2, dy = (p.y - uy) / 2, rr = g_isqrt(dx * dx + dy * dy);
        sv_ell(rr, rr * 21 / 50, 4, 3, NB);
    }
    {                                                   /* the sun (through gp: its mesh by g.fade) */
        uint8_t of = g.fade;
        g.fade = (uint8_t)sv_a;
        g_sun_draw(SV_X, SV_Y, 9 * sv_k / 256 + 1, g_star_kind(sx, sy), y0, y1);
        g.fade = of;
    }
    (void)i, (void)j;
    for (k = 0; k < (int32_t)G_NPL && g_planet(sx, sy, (uint32_t)k, &p); k++) {   /* the planets: their globes, turning */
        uint8_t of = g.fade;
        sv_pt(p.x, p.y, ux, uy, &x, &y);
        x = SV_X + (x - SV_X) * sv_k / 256, y = SV_Y + (y - SV_Y) * sv_k / 256;
        g_cmesh = 1, g.fade = (uint8_t)sv_a;
        g_planet_draw(&p, x, y, (5 + p.r / 3) * sv_k / 256 + 1, y0, y1);
        g_cmesh = 0, g.fade = of;
    }
    if (g_tr_i) {                                       /* the trail of full speed */
        uint32_t q, nq = g_tr_i < G_TRN ? g_tr_i : G_TRN;
        for (q = 1; q < nq; q++) {
            uint32_t i0 = (g_tr_i - q) % G_TRN, i1 = (g_tr_i - q - 1u) % G_TRN;
            int32_t x0, y0b, x1, y1b;
            sv_pt(g_tr[i0][0], g_tr[i0][1], ux, uy, &x0, &y0b), sv_pt(g_tr[i1][0], g_tr[i1][1], ux, uy, &x1, &y1b);
            if ((x0 - x1) * (x0 - x1) + (y0b - y1b) * (y0b - y1b) < 900)
                cv_line(SV_X + (x0 - SV_X) * sv_k / 256, SV_Y + (y0b - SV_Y) * sv_k / 256, SV_X + (x1 - SV_X) * sv_k / 256, SV_Y + (y1b - SV_Y) * sv_k / 256, q < 6u ? EY : q < 14u ? NO : NP);
        }
    }
    if (g_station(sx, sy, &qx, &qy)) {                  /* the station: a pin, its icon on top */
        sv_pt(qx, qy, ux, uy, &x, &y);
        for (j = 2; j < 14; j++)
            svp(x, y - j, NW);
        svp(x, y, NW);
        for (j = -4; j <= 4; j++)
            for (i = -4; i <= 4; i++) {
                int32_t m = (i < 0 ? -i : i) + (j < 0 ? -j : j);
                if (m <= 4)
                    svp(x + i, y - 19 + j, m == 3 ? EY : m == 4 && (!i || !j) ? NW : !m ? NW : NK);
            }
    }
    {   /* the ship: an arrow its way, held at the rim when it is out */
        int32_t vx = ((g.sx >> 8) - ux) / 2, vy = ((g.sy >> 8) - uy) / 2, m = vx * vx + vy * vy;
        if (m > 108 * 108) {
            int32_t r0 = g_isqrt(m);
            vx = vx * 108 / r0, vy = vy * 108 / r0;
        }
        x = SV_X + vx, y = SV_Y + vy * 21 / 50;
        for (k = 0; k < 7; k++) {
            int32_t w2 = k / 2;
            for (i = -w2; i <= w2; i++)
                svp(x + ((g_cos(g.ang) * (3 - k) - g_sin(g.ang) * i) >> 15),
                        y + (((g_sin(g.ang) * (3 - k) + g_cos(g.ang) * i) >> 15) * 21 / 50), k ? NW : ER);
        }
    }
    {   /* the way out: little arrows at the four sides, breathing (fly out of the system: the next one) */
        int32_t o = (int32_t)((fm1_ms / 250u) & 3u), m;
        for (m = 0; m < 4; m++) {
            svp(6 - o + m, SV_Y - m, NW), svp(6 - o + m, SV_Y + m, NW);
            svp(233 + o - m, SV_Y - m, NW), svp(233 + o - m, SV_Y + m, NW);
            svp(120 - m, 70 - o + m, NW), svp(120 + m, 70 - o + m, NW);
            svp(120 - m, 212 + o - m, NW), svp(120 + m, 212 + o - m, NW);
        }
    }
    if (sv_a < 14)
        return;                                         /* (opening: no words yet) */
    str_cpy(t, "THE ", 5);                              /* THE MOZE SYSTEM, the core's distance */
    g_sys_name(sx, sy, t + 4);
    str_cpy(t + str_len(t), " SYSTEM", 8);
    n_texto(120 - n_w(t, 1) / 2, 30, t, 1, NW);
    for (i = 120 - n_w(t, 1) / 2; i < 120 + n_w(t, 1) / 2; i += 2)
        svp(i, 48, NB);
    {
        int32_t dx = G_CX - sx, dy = G_CY - sy;
        fmt_int(t, g_isqrt(dx * dx + dy * dy) * 1000 + (int32_t)(g_hash(sx, sy, 3u) % 1000u));
        str_cpy(t + str_len(t), " LY FROM CORE", 14);
        n_texto(120 - n_w(t, 1) / 2, 50, t, 1, sc == NP || sc == NW ? NB : sc);
    }
}
/* ui_draw: HOME tapped once on HOME (no second tap in 0.4 s): 1, once (the tracks' WAV windows, open / closed) */
static int g_home_single(void)
{
    if (g.home_t && (int32_t)(fm1_ms - g.home_t) >= (int32_t)G_DBL_MS) {
        g.home_t = 0;
        return 1;
    }
    return 0;
}
/* ui_input.c: HOME held on HOME (the game). In space it is the menu (0: not taken); on a planet it calls the ship;
   elsewhere in the game (the map, a station, the films) nothing */
static int g_home_hold(void)
{
    if (!g.inited || g.mode == GM_SPACE)
        return 0;
    if (g.mode == GM_SURF)
        g_call_ship();
    return 1;
}
/* in space, a planet in reach (its edge within 8 px): land on it (B); 0 = none */
static int g_try_land(void)
{
    planet_t p;
    int32_t a, b, cx = g.sx >> 8, cy = g.sy >> 8, sx = g_sector(cx), sy = g_sector(cy);
    uint32_t k;
    for (a = sx - 1; a <= sx + 1; a++)
        for (b = sy - 1; b <= sy + 1; b++)
            for (k = 0; k < G_NPL && g_planet(a, b, k, &p); k++)
                if ((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) < (G_PR(p) + 8) * (G_PR(p) + 8)) {
                    g.pl = p;
                    g.psx = (int16_t)a, g.psy = (int16_t)b, g.pk = (uint8_t)k;
                    g.thr = 0, g.vx = g.vy = 0;
                    g.foe_n = 0;                        /* (landing: they lose you) */
                    g.space_z = g.z;                    /* (kept for the take-off) */
                    if (g.z > 4)
                        g.z = 4;                        /* (the close-up and the cabin are for space) */
                    g_set_mode(GM_LAND);
                    return 1;
                }
    return 0;
}
static void g_action(void)
{
    if (g.mode == GM_INTRO) {
        g_tune_stop();                                  /* (skipped: the music too) */
        g_set_mode(g.after);
        return;
    }
    if (g.mode == GM_MAP) {                             /* B on the map: warp to the cursor */
        int32_t sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8), j = g_jump(g.mx - sx, g.my - sy);
        if (!j)
            return;
        if (j > G_CLASS[g.cls].range) {
            ui_message("OUT OF WARP RANGE");
            return;
        }
        if (j > g.fuel)
            g_refuel();
        if (j > g.fuel) {
            ui_message("NOT ENOUGH FUEL");
            return;
        }
        g.fuel = (uint8_t)(g.fuel - j);
        g.wx = g.mx, g.wy = g.my;
        g_set_mode(GM_WARP);
        g.foe_n = 0;                                    /* (a warp: no one follows) */
        g_say("WARP", "INTERSTELLAR JUMP");
        return;
    }
    if (g.mode == GM_DOCK) {
        g_dock_do();
        return;
    }
    if (g.mode == GM_SPACE && g.fr_t && !g.fr_ev && fm1_ms - g.fr_t > 500u && fm1_ms - g.fr_t < 8500u) {
        /* (the user) a ship going by: OCT+ hails it first (before a planet): a gift, a trade, or a call for help */
        uint32_t k = (g.fr_t >> 4) & 3u;
        g.fr_ev = (uint8_t)(1u + (g.fr_t / 331u) % 3u), g.fr_e0 = fm1_ms;
        if (g.fr_ev == 1u)
            g.fuel = (uint8_t)(g.fuel + 2u > G_CLASS[g.cls].tank ? G_CLASS[g.cls].tank : g.fuel + 2u);
        else if (g.fr_ev == 2u && g.res[k] >= 3u)
            g.res[k] = (uint16_t)(g.res[k] - 3u), g.cred = (uint16_t)(g.cred + 3 * 2 * g_price(g_sector(g.sx >> 8), g_sector(g.sy >> 8), k));
        else if (g.fr_ev == 2u)
            g.fr_ev = 4u;                               /* (nothing to sell them) */
        else if (g.fr_ev == 3u)
            sf_start(0);
        g.dirty = 1;
        return;
    }
    if (g.mode == GM_SPACE) {
        planet_t p;
        int32_t a, b, cx = g.sx >> 8, cy = g.sy >> 8, sx = g_sector(cx), sy = g_sector(cy), x, y;
        for (a = sx - 1; a <= sx + 1; a++)
            for (b = sy - 1; b <= sy + 1; b++)
                if (g_gate_at(a, b, &x, &y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) < 20 * 20) {   /* a race gate */
                    g.thr = 0, g.vx = g.vy = 0, rc_space = 1;
                    g_race_start();
                    return;
                }
        for (a = sx - 1; a <= sx + 1; a++)
            for (b = sy - 1; b <= sy + 1; b++)
                if (g_station(a, b, &x, &y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) < 26 * 26) {   /* dock */
                    g.stx = a, g.sty = b, g.row = 4, g.pick = g.cls;
                    g.thr = 0, g.vx = g.vy = 0;
                    g_set_mode(GM_DOCK);
                    g.dirty = 1;
                    return;
                }
        (void)p;
        if (g_try_land())
            return;
        ui_message("NO PLANET IN REACH");
        return;
    }
    if (g.mode == GM_SURF) {
        int32_t px = g.px >> 8, py = g.py >> 8, cx, cy;
        if ((px - g.lx) * (px - g.lx) + (py - g.ly) * (py - g.ly) <= 64) {   /* by the ship: board, refuel, take off */
            g_refuel();
            g_set_mode(GM_TAKEOFF);
            return;
        }
        {   /* the beam (B held, again every 0.5 s): a sentinel near first (it hurts it), else the nearest resource
               (taken when B is let go: g_take), else into the air */
            uint32_t i, best = 0, keep = g.zap_sen;
            int32_t bd = 30 * 30 + 1, x, y;
            if (keep && keep <= g.sen_n) {              /* (B still held: the same one while it is within 40 px) */
                int32_t dx = (g.sen[keep - 1u].x >> 8) - px, dy = (g.sen[keep - 1u].y >> 8) - 3 - py;
                if (dx * dx + dy * dy <= 40 * 40)
                    bd = -1, best = keep;
            }
            for (i = 0; i < g.sen_n && bd >= 0; i++) {
                int32_t dx = (g.sen[i].x >> 8) - px, dy = (g.sen[i].y >> 8) - 6 - py + 3, d = dx * dx + dy * dy;
                if (d < bd)
                    bd = d, best = i + 1u;
            }
            g.zap_t = G_STAMP, g.zap_sen = 0;
            if (best) {
                g.zap_sen = (uint8_t)best;
                g.zx = g.sen[best - 1u].x >> 8, g.zy = (g.sen[best - 1u].y >> 8) - 6;
                g.facing = (uint8_t)(g.zx > px ? 1 : g.zx < px ? 0 : g.facing);
                g_sen_hurt(best - 1u);
                return;
            }
            bd = 18 * 18 + 1, best = 0;
            for (cy = (py >> 4) - 2; cy <= (py >> 4) + 2; cy++)
                for (cx = (px >> 4) - 2; cx <= (px >> 4) + 2; cx++)
                    if (g_res(cx, cy, &x, &y) && (x - px) * (x - px) + (y - py) * (y - py) < bd)
                        bd = (x - px) * (x - px) + (y - py) * (y - py), g.zrx = cx, g.zry = cy, g.zx = x, g.zy = y, best = 1;
            if (best) {
                g.zres = 1;
                g.facing = (uint8_t)(g.zx > px ? 1 : g.zx < px ? 0 : g.facing);
                return;
            }
            g.zres = 0;
            g.zx = px + (g.facing ? 18 : -18), g.zy = py - 3;   /* nothing in reach: the beam into the air */
        }
    }
}

/* KNOB 4 in the game: the volume of the track selected (the one the keys play; the drum track: its level), shown in the
 * corner while it turns (the text kept up, only its number changing) */
static void g_volume(int32_t s)
{
    static char b[16];
    int16_t *lv = is_drum(TSEL) ? &song.g[G_DRLVL] : &TSEL->p[P_LEVEL];
    uint32_t n;
    *lv = (int16_t)clamp(*lv + s * 4, 0, 127);
    str_cpy(b, "TRACK 1: ", sizeof b);
    b[6] = (char)('1' + song.sel);
    fmt_int(b + 9, *lv * 100 / 127);
    n = str_len("VOLUME") + str_len(b);
    if (g.say_t0 && g.say_t && str_eq(g.say_t, "VOLUME"))
        g.say_t0 = fm1_ms - n * G_SAY_LET;              /* (already up: in full, the 2 s again) */
    else
        g_say("VOLUME", b);
    g.say_s = b;
}
static int g_sel(int32_t s)                             /* ui_input.c: the SELECT knob on HOME; 1: the game took it */
{
    if (!g.inited || g.mode != GM_DOCK)
        return 0;
    g.row = 4, g_dock_turn(s);
    return 1;
}
static void g_knob(uint32_t k, int32_t s)               /* ui_input.c: KNOB 1..4 on HOME */
{
    if (!g.inited)
        g_init();
    if (g.mode == GM_INTRO) {
        g_tune_stop();                                  /* (skipped: the music too) */
        g_set_mode(g.after);
        return;
    }
    if (g.mode == GM_MAP) {                             /* the map: KNOB 1 / 2 move the cursor, a sector a detent */
        int32_t sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8);
        if (k == 0u)
            g.mx = clamp(g.mx + (s > 0 ? 1 : -1), sx - 20, sx + 20);
        else if (k == 1u)
            g.my = clamp(g.my + (s > 0 ? 1 : -1), sy - 20, sy + 20);
        else if (k == 2u)                               /* KNOB 3: the map's zoom */
            g.mz = (uint8_t)clamp((int32_t)g.mz + (s > 0 ? 1 : -1), 0, 2);
        else                                            /* KNOB 4: the selected track's volume */
            g_volume(s);
        return;
    }
    if (g.mode == GM_DOCK) {                            /* docked: KNOB 1 / 2 pick the row */
        if (k == 1u && g.row == 4)                      /* KNOB 2 on the ships: turn them */
            g_dock_turn(s);
        else if (k < 2u) {                              /* KNOB 1 / 2: the next box, in DK_ORD's order */
            int32_t o;
            for (o = 0; o < 8 && DK_ORD[o] != g.row; o++)
                ;
            g.row = DK_ORD[clamp(o + (s > 0 ? 1 : -1), 0, 7)];
        }
        return;
    }
    if (g.mode == GM_WARP)
        return;
    if (g.mode == GM_BATTLE && k == 2u && s < 0) {      /* (the user) KNOB 3 out: away from the fight */
        bt.on = 0, g_set_mode(GM_SPACE);                /* (the user) the fight goes on out here */
        sf_start(1);
        g.z = 2;
        g_say("OUT IN SPACE", "FIGHT OR RUN");
        return;
    }
    if (g.mode == GM_RACE && k == 2u && s < 0) {        /* KNOB 3 out: out of the race, back on the ground */
        if (rc_space)
            rc_space = 0, g_set_mode(GM_SPACE);
        else
            g_land_place(), g_set_mode(GM_SURF);
        g_say("RACE", "GAVE UP");
        return;
    }
    if (g.mode == GM_RACE) {                            /* the race: KNOB 1 steers, KNOB 2 the speed */
        if (k == 0u)
            rc.lane = clamp(rc.lane + s * 8, -110, 110);
        else if (k == 1u)
            rc.thr = (int8_t)clamp(rc.thr + s, 0, 10);
        return;
    }
    if (k == 2u) {                                      /* KNOB 3: the pixel size */
        {   /* (in space a fourth step, 5: the close-up, the ship from above, big) */
            int32_t was = g.z;
            if (g.mode == GM_SPACE && g.z == 2 && s < 0) {   /* (the user) past the widest: a charge, the lines growing;
                                                         * let go before it is full and it falls back */
                g.zc = (int16_t)(g.zc + 56), g.zc_t = fm1_ms;
                if (g.zc >= 256)
                    g.z = 1, g.zc = 0, g.zin_t = fm1_ms;
                return;
            }
            g.z = (uint8_t)clamp((int32_t)g.z + (s > 0 ? 1 : -1), g.mode == GM_SPACE ? 1 : 2, g.mode == GM_SPACE ? 6 : 4);
            if (g.z == 5 && was != 5)
                g_say("CLOSE-UP", "CHASE CAMERA");
            if (g.z == 6 && was != 6)
                g_say("CABIN", "INSIDE THE SHIP");
        }
        return;
    }
    if (k == 3u) {                                      /* KNOB 4: the selected track's volume (the radar: HOME twice) */
        g_volume(s);
        return;
    }
    if (g.mode == GM_SPACE) {
        if (k == 0u)
            g.ang = (g.ang + (uint32_t)(s * 32)) & 1023u;   /* 32 a detent: 32 directions */
        else
        {
            int32_t was = g.thr;
            g.thr = clamp(g.thr + s, 0, 10);
            if (!was && g.thr)
                g_say("THRUST", "STANDARD FLIGHT");
        }
    } else if (g.mode == GM_SURF) {
        int32_t run = g_held(0), st = (run ? 4 : 2) * s * 256, lead = (run ? 14 : 9) << 8;
        if (k == 0u)                                    /* (a short lead: it stops soon after the knob does) */
            g.tx = clamp(g.tx + st, g.px - lead, g.px + lead);
        else
            g.ty = clamp(g.ty + st, g.py - lead, g.py + lead);
    }
}

/* B let go on foot: the resource the beam was on is taken (if it is still there and in reach) */
static void g_take(void)
{
    int32_t x, y, px = g.px >> 8, py = g.py >> 8;
    uint32_t k;
    g.zap_sen = 0;                                      /* (the next hold picks again) */
    if (!g.zres || g.mode != GM_SURF)
        return;
    g.zres = 0;
    k = g_res(g.zrx, g.zry, &x, &y);
    if (!k || (x - px) * (x - px) + (y - py) * (y - py) > 24 * 24)
        return;
    if (g_cargo() >= G_CLASS[g.cls].cargo) {
        ui_message("CARGO FULL");
        return;
    }
    g.res[k - 1u]++;
    g.dirty = 1;
    g.got[g.got_n++ % G_NGOT] = g_hash(g.zrx, g.zry, g.pl.seed ^ 0xBEEFu);
    g_puff(x << 8, y << 8);
}
static void g_button(uint32_t b)                        /* ui_input.c: OCT- (0, A) / OCT+ (1, B) on HOME */
{
    if (!g.inited)
        g_init();
    if (b)
        g_action();
    else if (g.mode == GM_INTRO)
        g_tune_stop(), g_set_mode(g.after);
    else if (g.mode == GM_MAP)                          /* A: the map closes */
        g_set_mode(GM_SPACE);
    else if (g.mode == GM_DOCK)                         /* A: leave the station */
        g.row = 7, g_dock_do();
}

/* ui_input.c, every pass on HOME (on = the game is showing): OCT- / OCT+. A click (let go before G_HOLD_MS) is the
   octave: returns -1 / +1 for ui_input.c to step it. Held, it is A / B once (B on foot again every 0.5 s: the beam
   keeps mining; A in flight is the boost while held, g_held). Both together: the warp's map. In the game's menus
   (the opening, the map, a station) a press acts at once, as before */
static int32_t g_oct_poll(uint32_t pressed, int on)
{
    uint32_t b, now = fm1_ms;
    int32_t step = 0;
    int menu = g.mode == GM_INTRO || g.mode == GM_MAP || g.mode == GM_DOCK;
    if (!on) {
        go.t0[0] = go.t0[1] = 0, go.both = 0, go.dbl = 0;
        return 0;
    }
    for (b = 0; b < 2u; b++)
        if (((pressed >> panel.btn[b ? B_OCTUP : B_OCTDN]) & 1u) && !go.t0[b]) {
            go.t0[b] = G_STAMP, go.fired[b] = 0;
            if (menu && !go.both) {
                go.fired[b] = 1;
                g_button(b);
            }
        }
    if (go.t0[0] && go.t0[1] && !go.both && g_down(0) && g_down(1)) {   /* both: the map (in space) */
        go.both = 1;
        if (g.inited && g.mode == GM_SPACE) {
            g.mx = g_sector(g.sx >> 8), g.my = g_sector(g.sy >> 8);
            g_set_mode(GM_MAP);
        }
    }
    for (b = 0; b < 2u; b++) {
        if (!go.t0[b])
            continue;
        if (!g_down(b)) {                               /* let go: a click, if nothing fired */
            if (b && go.fired[b] && !go.both)
                g_take();                               /* (B let go on foot: the beam's resource is taken) */
            if (!go.fired[b] && !go.both) {
                if (b || g.mode != GM_SPACE) {
                    step = b ? 1 : -1;
                } else if (g.cruise) {                  /* in space, A: a click stops the cruise */
                    g.cruise = 0, go.dbl = 0;
                    ui_message("AUTO BOOST OFF");
                } else if (go.dbl) {                    /* .. clicked twice: the cruise (no octave) */
                    g.cruise = 1, go.dbl = 0;
                    ui_message("AUTO BOOST ON");
                } else {
                    go.dbl = G_STAMP;                   /* .. once: the octave, if no second click comes */
                }
            }
            go.t0[b] = 0;
            continue;
        }
        if (go.both)
            continue;
        if (!go.fired[b] && now - go.t0[b] >= G_HOLD_MS) {
            go.fired[b] = 1;
            g_button(b);
        } else if (!b && g.mode == GM_SURF && !go.called && !g.walk && now - go.t0[b] >= 1200u) {
            go.called = 1;                              /* A held 1.2 s standing still on a planet: the ship (as HOME
                                                         * held); held while walking it is the run */
            g_call_ship();
        } else if (b && go.fired[b] && g.mode == GM_SURF && now - go.t0[b] >= G_HOLD_MS + 500u) {
            go.t0[b] = now - G_HOLD_MS;                 /* (B kept held on foot: the beam again) */
            g_action();
        }
    }
    if (!go.t0[0] && !go.t0[1])
        go.both = 0;
    if (!go.t0[0])
        go.called = 0;
    if (go.dbl && !go.t0[0] && now - go.dbl >= G_DBL_A_MS) {   /* no second click: it was the octave */
        go.dbl = 0;
        step = -1;
    }
    return step;
}

/* one frame: the world, then the screen in two halves */
/* ------------------------------------------------------------- the close-up --- */
/* KNOB 3 past 4, in space: the camera right over the ship. The ship seen from above (assets/nave-cima.png, cut out of the
 * user's picture of its four views) at 2 x, turned to its heading by sampling it backwards (nearest), bobbing a little;
 * the stars in three depths go by fast, each leaving a long trail of light in its own colour (head bright, then dithered
 * to deep blue), longer the faster you go; the engines leave streaks of yellow and orange. In screen pixels. */
#include "nms_nave_cima.h"
/* the close-up over a planet: it shows far below, behind the ship, huge (3 screen px a world px: between the stars'
   depths), drawn in 2 x 2 px like the space view */
static void g_close_planet(void)
{
    int32_t a, b, sx = g_sector(gcx), sy = g_sector(gcy), oz = gz, ow = gw, oh = gh, y0 = (-cv_oy) / 2 - 1, y1 = y0 + 62;
    gz = 2, gw = gh = 120;
    for (a = sx - 1; a <= sx + 1; a++)
        for (b = sy - 1; b <= sy + 1; b++) {
            planet_t p;
            uint32_t k;
            for (k = 0; k < G_NPL && g_planet(a, b, k, &p); k++) {
                int32_t cx = 60 + (p.x - gcx) * 3 / 2, cy = 60 + (p.y - gcy) * 3 / 2, r = G_PR(p) * 3 / 2;
                if (cx + r * 3 < 0 || cx - r * 3 > gw || cy + r < y0 || cy - r > y1)
                    continue;
                g_planet_draw(&p, cx, cy, r, y0, y1);
            }
        }
    gz = oz, gw = ow, gh = oh;
}
static void g_close_draw(void)
{
    static const uint16_t COL[3][6] = {{NP, NB, EN, NP, NB, NO}, {NB, NG, NP, NO, EG, NB}, {NW, EY, NB, EG, NB, NW}};   /* (the user: no red streak, it read as a shot) */
    static const int32_t DEPTH[3] = {2, 4, 8};          /* the camera's move, times (close: the stars rush by) */
    int32_t speed = g_isqrt((g.vx >> 8) * (g.vx >> 8) + (g.vy >> 8) * (g.vy >> 8)), l, i;
    int32_t ux = 0, uy = 0;                              /* (the direction of travel, Q8) */
    uint32_t a = g.ang;
    if (speed > 0)
        ux = (g.vx >> 8) * 256 / speed, uy = (g.vy >> 8) * 256 / speed;
    else
        ux = g_cos(a) >> 7, uy = g_sin(a) >> 7;
    g_close_planet();
    for (l = 0; l < 3; l++)
        for (i = 0; i < 40; i++) {
            uint32_t h = g_hash(i, l, 0xC105u);
            int32_t x = (int32_t)(h % 480u) - gcx * DEPTH[l], y = (int32_t)((h >> 9) % 480u) - gcy * DEPTH[l], len, k;
            uint16_t c = COL[l][(h >> 20) % 6u];
            x = ((x % 480) + 480) % 480 - 120, y = ((y % 480) + 480) % 480 - 120;
            len = 8 + speed * DEPTH[l] / 3;              /* the trail: long, longer with speed and nearness */
            if (len > 150)
                len = 150;
            for (k = 0; k < len; k++) {                  /* head to tail: its colour, then dithered off to deep blue */
                int32_t px = x - ux * k / 256, py = y - uy * k / 256;
                if (k < len / 3)
                    cv_pset(px, py, k < 2 && l == 2 ? NW : c);
                else if (k < len * 2 / 3) {
                    if ((k & 1) == 0)
                        cv_pset(px, py, c);
                } else if ((k % 3) == 0)
                    cv_pset(px, py, EN);
            }
        }
    {   /* the engines' light: streaks behind it, yellow and orange, longer with the boost */
        int32_t bx = -(g_cos(a) >> 7), by = -(g_sin(a) >> 7), n = g.thr ? 6 + g.thr * 2 + (g.boost_on ? 20 : 0) : 0;
        for (i = 0; i < 6; i++) {
            int32_t side = i < 3 ? -1 : 1, off = 13 * side, sx = 120 + bx * 24 / 256 - by * off / 256, sy = 120 + by * 24 / 256 + bx * off / 256;
            int32_t len = n > 0 ? n + (int32_t)(g_hash(i, (int32_t)(fm1_ms / 50u), 9u) % 8u) : 0, k;
            for (k = 0; k < len; k++)
                cv_pset(sx + bx * k / 256 + (i % 3) - 1, sy + by * k / 256, k < len / 3 ? EY : k < len * 2 / 3 ? NO : ((k & 1) ? ER : NK));
        }
    }
    {   /* (the user) near its sun: the sun behind, big */
        int32_t ux, uy, dx, dy, sxs = g_sector(g.sx >> 8), sys2 = g_sector(g.sy >> 8);
        g_sun_at(sxs, sys2, &ux, &uy);
        dx = ux - (g.sx >> 8), dy = uy - (g.sy >> 8);
        g_lit = 0;
        if (dx * dx + dy * dy < 90 * 90 && !(sxs == G_CX && sys2 == G_CY)) {
            int32_t ogz = gz, ogw = gw, ogh = gh;
            gz = 1, gw = gh = 240;
            g_sun_draw(120 + dx * 2, 120 + dy * 2, 26, g_star_kind(sxs, sys2), -cv_oy - 2, -cv_oy + 122);
            gz = ogz, gw = ogw, gh = ogh;
        }
    }
#if NMS_NAVE_CIMA
    {   /* the ship: turned to its heading (its picture's nose is up: 768), 2 x, bobbing; against its sun dark, its rim lit */
        static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
        uint32_t phi = (a - 768u) & 1023u;
        int32_t c = g_cos(phi), sn = g_sin(phi), ox, oy, R = NMS_NC_W + 4, cy = 120 + ((g_sin(fm1_ms / 3u) * 2) >> 15);
        for (oy = -R; oy <= R; oy++)
            for (ox = -R; ox <= R; ox++) {
                int32_t sx = ((c * ox + sn * oy) >> 15) / 2 + NMS_NC_W / 2, sy = ((-sn * ox + c * oy) >> 15) / 2 + NMS_NC_H / 2;
                uint32_t k, v;
                if ((uint32_t)sx >= (uint32_t)NMS_NC_W || (uint32_t)sy >= (uint32_t)NMS_NC_H)
                    continue;
                k = (uint32_t)(sy * NMS_NC_W + sx), v = (NMS_NC[k >> 1] >> ((k & 1u) ? 0u : 4u)) & 15u;
                if (v < 10u) {
                    if (g_lit) {
                        int edge = 0, q;
                        for (q = 0; q < 4 && !edge; q++) {     /* its outline: a picture pixel with a hole beside it */
                            int32_t ex = sx + (q == 0) - (q == 1), ey = sy + (q == 2) - (q == 3);
                            uint32_t k2 = (uint32_t)(ey * NMS_NC_W + ex);
                            edge = (uint32_t)ex >= (uint32_t)NMS_NC_W || (uint32_t)ey >= (uint32_t)NMS_NC_H ||
                                   ((NMS_NC[k2 >> 1] >> ((k2 & 1u) ? 0u : 4u)) & 15u) >= 10u;
                        }
                        cv_pset(120 + ox, cy + oy, edge ? g_lit : NK);
                    } else {
                        cv_pset(120 + ox, cy + oy, PAL[v]);
                    }
                }
            }
    }
    g_lit = 0;
#else
    g_ship_px(120, 120, a, 40, g.thr ? 1 : 0);
#endif
}

/* ------------------------------------------------------------- the cabin --- */
/* KNOB 3's last level in space: inside the ship (assets/cabine.png, 120 x 120 drawn 2 x; its window see-through). Through
   the window: the nebula, the stars coming at you from the middle (faster and longer with the speed), and the nearest
   planet ahead, in perspective, growing as you get nearer */
#include "nms_cabine.h"
#define G_CB_VX 80                                      /* the window's vanishing point (screen px) */
#define G_CB_VY 76
static void g_cabin_draw(void)
{
    int32_t speed = g_isqrt((g.vx >> 8) * (g.vx >> 8) + (g.vy >> 8) * (g.vy >> 8)), i, y0 = (-cv_oy) / 2 - 1, y1 = y0 + 62;
    int32_t oz = gz, ow = gw, oh = gh;
    uint32_t now = fm1_ms;
    gz = 2, gw = gh = 120;
    g_nebula(y0, y1);
    {   /* the nearest planet ahead */
        int32_t a, b, sx = g_sector(gcx), sy = g_sector(gcy), best = 1 << 30, bx = 0, by = 0, br = 0;
        int32_t fx = g_cos(g.ang) >> 7, fy = g_sin(g.ang) >> 7;   /* (Q8: ahead) */
        planet_t bp;
        for (a = sx - 1; a <= sx + 1; a++)
            for (b = sy - 1; b <= sy + 1; b++) {
                planet_t p;
                uint32_t k;
                for (k = 0; k < G_NPL && g_planet(a, b, k, &p); k++) {
                    int32_t dx = p.x - gcx, dy = p.y - gcy, dot = (dx * fx + dy * fy) >> 8, side = (dy * fx - dx * fy) >> 8;
                    if (dot < 8 || dot >= best)
                        continue;
                    best = dot, bp = p, bx = G_CB_VX + side * 110 / dot, by = G_CB_VY + 900 / dot, br = G_PR(p) * 110 / dot;
                }
            }
        {   /* (the user: no blinking) the one ahead grows in, eased (fast, then soft); another one: the first shrinks out,
             * then the new one grows; its place glides */
            static planet_t cbp;
            static int32_t cbx, cby, cbr, cbs;          /* cbr: its radius, cbs: how much of it shown, Q8 */
            int has = best < (1 << 30) && br > 1, same = has && bp.seed == cbp.seed;
            if (br > 160)
                br = 160;
            if (has && !same && cbs < 8)                /* (the old one gone: the new one comes) */
                cbp = bp, cbx = bx, cby = by, cbr = br, cbs = 0, same = 1;
            if (same && (y0 < 2))                       /* (once a frame: the first band) */
                cbx += (bx - cbx) / 3, cby += (by - cby) / 3, cbr += (br - cbr) / 3;
            if (y0 < 2)
                cbs += ((same ? 256 : 0) - cbs) / 5 + (same ? 1 : -1), cbs = clamp(cbs, 0, 256);
            if (cbs > 0) {
                int32_t rr = cbr * cbs / 256;
                if (rr > 1)
                    g_planet_draw(&cbp, cbx / 2, cby / 2, rr / 2, y0, y1);
            }
        }
    }
    gz = oz, gw = ow, gh = oh;
    for (i = 0; i < 70; i++) {                          /* the stars, coming at you */
        uint32_t h = g_hash(i, 11, 0xCAB1u), an = h & 1023u;
        int32_t z = 1000 - (int32_t)(((h >> 10) % 1000u + now * (uint32_t)(30 + speed * 6) / 1000u) % 1000u), r, len;
        uint16_t c = (h >> 22) % 4u == 0u ? EY : (h >> 22) % 4u == 1u ? NB : NW;
        if (z < 20)
            continue;
        r = 6000 / z, len = 1 + speed * r / 600;
        cv_line(G_CB_VX + ((g_cos(an) * r) >> 15), G_CB_VY + ((g_sin(an) * r) >> 15),
                G_CB_VX + ((g_cos(an) * (r + len)) >> 15), G_CB_VY + ((g_sin(an) * (r + len)) >> 15), c);
    }
#if NMS_CABINE
    {   /* the room over it all, but its window */
        static const uint16_t PAL[10] = {NK, NW, NB, NG, NP, NO, EG, ER, EY, EN};
        int32_t k, n = 0;
        for (k = 0; k < NMS_CB_LEN; k++) {
            int32_t run = (NMS_CB[k] >> 4) + 1, c = NMS_CB[k] & 15;
            while (run > 0) {
                int32_t y = n / NMS_CB_N, x = n % NMS_CB_N, len = NMS_CB_N - x < run ? NMS_CB_N - x : run;
                if (c < 10 && y * 2 >= y0 * 2 - 2 && y * 2 <= y1 * 2 + 2)
                    cv_rect(x * 2, y * 2, len * 2, 2, PAL[c]);
                n += len, run -= len;
            }
        }
    }
#endif
    if (g.foe_n && ((now / 300u) & 1u))                 /* pursued: the alarm, red at the window's edge */
        for (i = 0; i < 4; i++)
            cv_rect(0, 22 + i * 2, 4, 110, ER);
}

/* the first time the game is on (settings.zoom bit 29 not set): how to fly, in a box of 60 % of the screen; after
   3 s any button takes it away (and that press does nothing else: ui_input.c), and it never comes back */
/* the opening's music: a soft synthwave phrase on track 2 (its sound as it is: power-on, RHODES) with a long reverb (its
   REV raised while the opening plays, given back after): an arpeggio over Am and F, a bass under it, then an Am(add9)
   chord left ringing through the arrival. A press that skips the opening stops it */
static const struct { uint16_t t, len; uint8_t n, v; } G_OPMUS[] = {
    {0, 1400, 45, 43}, {0, 230, 57, 37}, {250, 230, 60, 36}, {500, 230, 64, 38}, {750, 230, 69, 41}, {1000, 230, 64, 36}, {1250, 230, 60, 35},
    {1500, 1400, 41, 43}, {1500, 230, 53, 37}, {1750, 230, 57, 36}, {2000, 230, 60, 38}, {2250, 230, 65, 41}, {2500, 230, 60, 36}, {2750, 230, 57, 35},
    {3000, 600, 48, 41}, {3000, 600, 60, 33}, {3000, 600, 64, 33}, {3000, 600, 67, 33},
    {3700, 2300, 45, 40}, {3700, 2300, 57, 32}, {3700, 2300, 60, 31}, {3700, 2300, 64, 31}, {3700, 2300, 71, 30}};
#define G_OPMUS_N (sizeof G_OPMUS / sizeof G_OPMUS[0])
static uint32_t g_tune_on, g_tune_off;                  /* (bits: the notes started, the notes let go) */
static int16_t g_tune_rev = -1, g_tune_lv, g_tune_mu;  /* track 2's REV, LEVEL, MUTE, while borrowed */
static void g_tune_stop(void)
{
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < G_OPMUS_N; i++)
        if (((g_tune_on >> i) & 1u) && !((g_tune_off >> i) & 1u))
            trk_note_off(&trk[1], G_OPMUS[i].n);
    if (g_tune_rev >= 0)
        trk[1].p[P_REV] = g_tune_rev, trk[1].p[P_LEVEL] = g_tune_lv, trk[1].p[P_MUTE] = g_tune_mu;
    fm1_irq_on();
    g_tune_off = g_tune_on = 0xFFFFFFFFu, g_tune_rev = -1;
}
static void g_tune_tick(int32_t t)                      /* t: ms into the opening (its 4 s and the arrival) */
{
    uint32_t i;
    if (t < 0 || g_tune_on == 0xFFFFFFFFu)
        return;
    fm1_irq_off();
    if (g_tune_rev < 0) {                               /* (the user) heard whatever the project did to track 2 */
        g_tune_rev = trk[1].p[P_REV], g_tune_lv = trk[1].p[P_LEVEL], g_tune_mu = trk[1].p[P_MUTE];
        trk[1].p[P_REV] = 110, trk[1].p[P_LEVEL] = 104, trk[1].p[P_MUTE] = 0;
    }
    for (i = 0; i < G_OPMUS_N; i++) {
        if (!((g_tune_on >> i) & 1u) && t >= (int32_t)G_OPMUS[i].t) {
            trk_note_on(&trk[1], G_OPMUS[i].n, G_OPMUS[i].v);
            g_tune_on |= 1u << i;
        }
        if (((g_tune_on >> i) & 1u) && !((g_tune_off >> i) & 1u) && t >= (int32_t)(G_OPMUS[i].t + G_OPMUS[i].len)) {
            trk_note_off(&trk[1], G_OPMUS[i].n);
            g_tune_off |= 1u << i;
        }
    }
    fm1_irq_on();
    if (t > 7800 && g_tune_rev >= 0) {                  /* (the tail rung out: the REV given back) */
        fm1_irq_off();
        trk[1].p[P_REV] = g_tune_rev, trk[1].p[P_LEVEL] = g_tune_lv, trk[1].p[P_MUTE] = g_tune_mu;
        fm1_irq_on();
        g_tune_rev = -1, g_tune_on = 0xFFFFFFFFu;
    }
}

static void g_help_draw(void)
{
    static const char *const L[7] = {"KNOB 1  TURN", "KNOB 2  SPEED", "OCT-  A", "OCT+  B", "BOTH  WARP MAP", "REST  AS EVER", ""};
    uint32_t age = fm1_ms - g_help_t, i;
    cv_rect(46, 38, 148, 166, NK);
    cv_rect(46, 38, 148, 2, NB), cv_rect(46, 202, 148, 2, NB), cv_rect(46, 38, 2, 166, NB), cv_rect(192, 38, 2, 166, NB);
    n_texto(120 - n_w("HOW TO FLY", 1) / 2, 46, "HOW TO FLY", 1, EY);
    for (i = 0; i < 6u; i++)
        n_texto(56, 70 + (int32_t)i * 19, L[i], 1, i < 2u ? NW : i < 5u ? NO : NB);
    if (age >= 3000u && ((age / 400u) & 1u))
        n_texto(120 - n_w("ANY BUTTON", 1) / 2, 184, "ANY BUTTON", 1, EG);
}

static uint32_t gs_el, gs_view, gs_frame_done;
static int32_t gs_arrive, gs_arr_v;
/* the game a frame on (once a frame: n_home, or under a screen over it): the step, the fade, the camera */
static void g_step(void)
{
    uint32_t now = fm1_ms, dt, el, view;
    int32_t arrive, arr_v = 0;
    if (gs_frame_done == ui.frame + 1u)
        return;
    gs_frame_done = ui.frame + 1u;
    if (!g.inited)
        g_init();
    if (g.zc && now - g.zc_t > 140u)                    /* the charge, let go: it falls back */
        g.zc = (int16_t)(g.zc > 14 ? g.zc - 14 : 0);
    if (g.mode == GM_SPACE && now - g_tr_t > 250u) {    /* the trail, at full speed (the system view draws it) */
        int32_t v = g_isqrt((g.vx >> 8) * (g.vx >> 8) + (g.vy >> 8) * (g.vy >> 8)) * 10;
        g_tr_t = now;
        if (v >= 600)
            g_tr[g_tr_i % G_TRN][0] = g.sx >> 8, g_tr[g_tr_i % G_TRN][1] = g.sy >> 8, g_tr_i++;
    }
    dt = now - g.t_last;
    if (dt > 60u)
        dt = 60u;
    g.t_last = now;
    g.frame++;
    el = now - g.t_mode;
    if (g.z < 2 && g.mode != GM_SPACE && g.mode != GM_MAP && g.mode != GM_DOCK && g.mode != GM_BATTLE && g.mode != GM_RACE)
        g.z = 2;                                        /* (the system view is for space) */
    gz = g.mode == GM_MAP || g.mode == GM_DOCK || g.mode == GM_BATTLE || g.mode == GM_RACE || g.z > 4 || g.z < 2 ? 1 : g.z, gw = 240 / gz, gh = 240 / gz;   /* (the close-up: px) */
    if (g.z > 4 && g.mode != GM_SPACE && g.mode != GM_INTRO)
        gz = 4, gw = gh = 60;                           /* (out of space it is the closest pixel) */
    /* the step */
    if (g.mode == GM_INTRO && el > G_LOGO_END + G_ARRIVE_MS) {       /* the opening over: playing (already faded in) */
        g_set_mode(g.after), el = 0;
        if (g.after == GM_SPACE && !((settings.zoom >> 29) & 1u))
            g_help_t = G_STAMP;                         /* the first time ever: how to fly */
        g.t_fade = now - 60000u;
    }
    if (g.mode == GM_INTRO || (g_tune_on && g_tune_on != 0xFFFFFFFFu && el < 2000u))
        g_tune_tick(g.mode == GM_INTRO ? (int32_t)(el - G_PIC_MS) : (int32_t)(G_LOGO_END + G_ARRIVE_MS - G_PIC_MS + el));   /* the music */
    view = g.mode == GM_INTRO ? g.after : g.mode;       /* (the opening fades into the game as saved) */
    if (view == GM_SPACE)
        g_space_tick(dt);
    if (g.mode == GM_WARP && el > G_WARP_MS) {          /* the warp over: in the middle of the sector chosen */
        g.sx = (g.wx * G_SECT + G_SECT / 2) << 8, g.sy = (g.wy * G_SECT + G_SECT / 2 + 40) << 8;
        g.vx = g.vy = 0;
        g.warps++;
        g.dirty = 1;
        g_set_mode(GM_SPACE), el = 0;
        view = GM_SPACE;
    }
    if (g.mode == GM_SPACE && !(g.flags & 2u) && g_sector(g.sx >> 8) == G_CX && g_sector(g.sy >> 8) == G_CY) {
        int32_t dx = (g.sx >> 8) - (G_CX * G_SECT + G_SECT / 2), dy = (g.sy >> 8) - (G_CY * G_SECT + G_SECT / 2);
        if (dx * dx + dy * dy < 60 * 60) {              /* the aim: the centre reached */
            g.flags |= 2u;
            g.dirty = 1;
            ui_message("THE CENTRE OF THE GALAXY");
        }
    }
    if (g.mode == GM_SURF) {
        g_surf_tick(dt);
        g_sen_tick(dt);
    }
    if (g.home_t && (int32_t)(now - g.home_t) >= (int32_t)G_DBL_MS)   /* HOME once (no second tap): nothing (the ship: HOME held) */
        g.home_t = 0;
    if (g.call_t && now - g.call_t > G_CALL_MS) {      /* the ship called: down */
        g.lx = g.cx_, g.ly = g.cy_, g.call_t = 0, g.dirty = 1;
        ui_message("SHIP LANDED");
    }
    if (g.mode == GM_LAND && el > 1500u) {
        g_land_place();
        g.dirty = 1;
        g_set_mode(GM_SURF), el = 0;
        if (g_race_here(&g.pl))                         /* (the user) a race planet: the race */
            g_race_start();
    }
    if (g.mode == GM_RACE)
        rc_tick(dt);
    g_npc_tick();
    if (g.fr_ev && now - g.fr_e0 > 4500u)               /* (the hail over: it flies on) */
        g.fr_t += now - g.fr_e0, g.fr_ev = 0;
    {   /* (the user) pirates: a raid now and then (the first a minute in, then every 1.5 to 2.5 min), 2 to 6 of them */
        static uint32_t pir_next;
        if (g.mode == GM_SPACE && !sf_on && !g.foe_n && !g.fr_ev) {
            if (!pir_next)
                pir_next = now + 60000u;
            else if ((int32_t)(now - pir_next) > 0)
                sf_raid(2u + g_hash((int32_t)now, 3, 0x9124u) % 5u), pir_next = now + 90000u + g_hash((int32_t)now, 4, 0x9125u) % 60000u;
        }
    }
    if (g.mode == GM_SPACE && !g.foe_n && !g.fr_ev && (!g.fr_t || now - g.fr_t > 45000u))   /* a ship by, every 45 s */
        g.fr_t = now;
    if (g.mode == GM_TAKEOFF && el > G_TO_MS) {
        int32_t a = (int32_t)g.ang;
        g.sx = (g.pl.x << 8) + ((g_cos((uint32_t)a) >> 7) * (G_PR(g.pl) + 6));
        g.sy = (g.pl.y << 8) + ((g_sin((uint32_t)a) >> 7) * (G_PR(g.pl) + 6));
        g.vx = g.vy = 0, g.thr = 2;
        g.dirty = 1;
        if (g.space_z >= 2u)
            g.z = g.space_z;                            /* (the zoom you had before landing) */
        g_set_mode(GM_SPACE), el = 0;
        g_foes_start();
        if (g.foe_n)                                    /* (the user) they follow you up: the battle */
            sf_start(1);                                /* (the user) the fight out in space, the boss too */
    }
    if (g.mode == GM_BATTLE) {
        if (bt.on)
            bt_tick(dt);
        else
            g_set_mode(GM_SPACE);                       /* (a battle left in a save: space) */
    }

    /* the fade: the opening slowly (expo out over 2.6 s after the logo), any other mode in 0.5 s */
    {
        uint32_t fe = now - g.t_fade, span = g.mode == GM_INTRO ? (g.after == GM_SPACE ? 350u : 2600u) : 500u;
        if (g.mode == GM_INTRO)
            fe = el > G_LOGO_END ? el - G_LOGO_END : 0u;
        if (fe >= span)
            g.fade = 16;
        else {
            uint32_t q = fe * 256u / span, e = 256u - ((256u - q) * (256u - q) * (256u - q) >> 16);   /* ease out */
            g.fade = (uint8_t)(e * 16u >> 8);
        }
    }
    /* the camera */
    if (view == GM_SURF || view == GM_TAKEOFF)
        gcx = g.px >> 8, gcy = g.py >> 8;
    else
        gcx = g.sx >> 8, gcy = g.sy >> 8;
    arrive = g.mode == GM_INTRO && el >= G_LOGO_END && g.after == GM_SPACE ? (int32_t)(el - G_LOGO_END) : -1;
    if (arrive >= 0) {                                  /* the arrival: close up and fast, then slowing, pulling out */
        int32_t q = arrive * 256 / (int32_t)G_ARRIVE_MS, left = (256 - q) * (256 - q) / 256;   /* (the way still to go) */
        gz = arrive < 1000 ? 1 : arrive < 1700 ? 4 : 3, gw = gh = 240 / gz;
        gcx -= (g_cos(g.ang) * left * 3) >> 15, gcy -= (g_sin(g.ang) * left * 3) >> 15;   /* (coming up from behind) */
        arr_v = left * 4;                               /* px / s, for the trails */
    }
    gs_el = el, gs_view = view, gs_arrive = arrive, gs_arr_v = arr_v;
}

/* the game's scene in rows ytop .. ytop + hh of the screen (the canvas begun by the caller); hud: the head over it */
static void g_band(int32_t ytop, int32_t hh, int hud)
{
    uint32_t now = fm1_ms, el = gs_el, view = gs_view;
    int32_t arrive = gs_arrive, arr_v = gs_arr_v, lz = 0;
    int32_t y0 = ytop / gz - 1, y1 = (ytop + hh) / gz + 1;
    planet_t near;
    int st_near = 0;
    near.r = 0;
    (void)lz;
    cv_oy = -ytop;
    if (g.mode == GM_INTRO && el < G_LOGO_END) {          /* the opening: the ship flying, the title */
        if (el < G_PIC_MS)
            g_picture();                            /* (the picture first, when there is one) */
        else
            g_open_draw((int32_t)(el - G_PIC_MS));
    } else if (arrive >= 0 && arrive < 1000) {      /* the arrival, close up: the trails long with the speed */
        int32_t ovx = g.vx, ovy = g.vy;
        g.vx = (g_cos(g.ang) >> 7) * arr_v, g.vy = (g_sin(g.ang) >> 7) * arr_v;
        g_nebula(y0, y1);
        g_close_draw();
        g.vx = ovx, g.vy = ovy;
    } else if (g.mode == GM_MAP) {
        g_map();
    } else if (g.mode == GM_DOCK) {
        g_dock();
    } else if (g.mode == GM_TAKEOFF) {             /* the take-off: a little film, in screen pixels */
        g_takeoff_draw(el);
    } else if (g.mode == GM_WARP) {
        g_warp_draw(el);
    } else if (g.mode == GM_BATTLE) {
        bt_draw(ytop, ytop + hh);
    } else if (g.mode == GM_RACE) {
        if (rc_space)
            rc_draw_space(ytop, ytop + hh);
        else
            rc_draw(ytop, ytop + hh);
    } else if (view == GM_SPACE && g.z < 2 && g.mode == GM_SPACE) {
        g_nebula(y0, y1);                           /* the system, from far above */
        {                                           /* just in: it opens the last bit, easing */
            int32_t q = now - g.zin_t < 400u ? (int32_t)(now - g.zin_t) * 256 / 400 : 256;
            q = 256 - (256 - q) * (256 - q) / 256;
            sv_k = 200 + q * 56 / 256, sv_a = 13 + q * 3 / 256;
        }
        g_sysview(y0, y1);
        sv_k = 256, sv_a = 16;
    } else if (view == GM_SPACE && g.z > 4 && g.mode == GM_SPACE) {
        if (g.z > 5) {
            g_cabin_draw();                         /* the cabin: inside the ship, space through its window */
        } else {
            g_nebula(y0, y1);                       /* (the nebula far behind, here too) */
            g_close_draw();                         /* the close-up: the ship from above among streaking stars */
        }
    } else if (view == GM_SPACE) {
        int32_t sa = (int32_t)g.ang;
        g_nebula(y0, y1);
        g_stars(y0, y1);
        g_planets(y0, y1, &near, &st_near);
        g_markers();
        g_particles();
        g_foes_draw();
        sf_draw();
        es_draw();
        if (g.zc) {                                 /* KNOB 3 past the widest: the system view opening over it */
            int32_t ogz = gz, ogw = gw, ogh = gh;
            gz = 1, gw = gh = 240;
            sv_k = 60 + g.zc * 140 / 256, sv_a = 2 + g.zc * 11 / 256;
            g_sysview(ytop, ytop + hh);
            sv_k = 256, sv_a = 16;
            gz = ogz, gw = ogw, gh = ogh;
        }
        if (near.r && !st_near && ((now / 300u) & 1u)) {   /* in reach: a dotted ring */
            int32_t vx, vy, a;
            g_w2s(near.x, near.y, &vx, &vy);
            for (a = 0; a < 1024; a += 24)
                gp(vx + ((g_cos((uint32_t)a) * (G_PR(near) + 3)) >> 15), vy + ((g_sin((uint32_t)a) * (G_PR(near) + 3)) >> 15), NW);
        }
        if (st_near && ((now / 300u) & 1u)) {       /* a station in reach: brackets round the ship */
            int32_t a;
            for (a = 0; a < 1024; a += 128)
                gp(gw / 2 + ((g_cos((uint32_t)a + 64u) * 8) >> 15), gh / 2 + ((g_sin((uint32_t)a + 64u) * 8) >> 15), NO);
        }
        {
            int32_t ux, uy, dx, dy, d, sxs = g_sector(gcx), sys2 = g_sector(gcy);
            g_sun_at(sxs, sys2, &ux, &uy);
            dx = ux - gcx, dy = uy - gcy, d = g_isqrt(dx * dx + dy * dy);
            g_rotv(&dx, &dy);
            (void)d;
            g_ship(gw / 2, gh / 2, (uint32_t)sa & 1023u, g.thr ? 1 + (int)(g_held(0) && g.fuel) : 0, 1);
        }
        g_scan();
        gcr = 32767, gsr = 0;
    } else if (g.mode == GM_LAND) {                 /* the planet grows to fill the view; the ship goes down */
        uint32_t q = el * 256u / 1500u, e = q * q >> 8;
        int32_t vx = g.pl.x - gcx + gw / 2, vy = g.pl.y - gcy + gh / 2;
        vx += ((gw / 2 - vx) * (int32_t)e) >> 8, vy += ((gh / 2 - vy) * (int32_t)e) >> 8;
        lz = G_PR(g.pl) + (int32_t)(((gw * 2) - G_PR(g.pl)) * (int32_t)e >> 8);
        g_nebula(y0, y1);
        g_stars(y0, y1);
        g_globe(vx, vy, lz, &g.pl, now / 40u + (g.pl.seed & 255u), y0, y1);
        if (el < 900u)
            g_ship(gw / 2, gh / 2, g.ang + el / 4u, 0, 1);
    } else {                                        /* surface (and the take-off from it) */
        int32_t lx = g.lx - gcx + gw / 2, ly = g.ly - gcy + gh / 2;
        g_surface(y0, y1);
        g_wake();
        g_res_draw();
        g_creatures();
        if (g.call_t) {                             /* called: it comes down from the sky, its shadow growing */
            uint32_t q = (now - g.call_t) * 256u / G_CALL_MS;
            int32_t tx = g.cx_ - gcx + gw / 2, ty = g.cy_ - gcy + gh / 2, h = (int32_t)((256u - q) * (256u - q) >> 10);
            int32_t a2;
            for (a2 = -1 - (int32_t)q / 90; a2 <= 1 + (int32_t)q / 90; a2++)
                gp(tx + a2, ty + 2, NK), gp(tx + a2, ty + 3, EN);
            g_ship(tx, ty - h, 768u, 2, 1);
        } else {
            g_parked(lx, ly);
        }
        if (g_npc_here(&g.pl) && !(g.q_done & g_npc_bit(&g.pl)))
            g_npc_draw(g.nx - gcx + gw / 2, g.ny - gcy + gh / 2, g.pl.seed);
        g_player(gw / 2, gh / 2);
        g_sen_draw();
        g_scan();
    }
    cv_oy = 0;
    if (hud && !(g.mode == GM_INTRO && el < G_LOGO_END) && g.mode != GM_TAKEOFF) {   /* (no head over the films) */
        cv_oy = -ytop;
        g_hud();
        g_npc_talk_draw();
        g_radio_draw();
        if (g.mode != GM_MAP && g.mode != GM_DOCK)
            g_say_draw();
        if (g_help_t && g.mode == GM_SPACE)
            g_help_draw();
        cv_oy = 0;
    }
}
/* a screen over the game (cv_backdrop, gfx.c): its scene under the band, without the head */
static void g_backdrop(int32_t y, int32_t h)
{
    g_band(y, h, 0);
}

static void g_draw(void)
{
    uint32_t half;
    if (!g.inited)
        g_init();
    g_step();
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, NK);
        g_band((int32_t)(half * 120u), 120, 1);
        cv_blit(0, half * 120u);
    }
    g_save_tick();
}

static void n_home(void)
{
    n_screen(SCR_HOME);
    g_draw();
}
