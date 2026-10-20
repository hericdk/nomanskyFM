/* SPDX-License-Identifier: GPL-3.0-only
 *
 * No Man Sky FM web emulator: the real firmware (SLOOP 2.6 / Felucca + the No Man Sky FM screens) sources compiled to
 * wasm32, with a browser HAL in place of the FM-1 hardware (as tests/hostsim.c
 * does for the DSP alone, but with the whole UI: screen, layers, LEDs).
 *
 * The firmware is one compilation unit (firmware/src/felucca.c); this file
 * plays that role for the web: the HAL below replaces the fm1_*.h headers,
 * then the same sources are included in the same order. No firmware source is
 * modified.
 *
 *   audio   emu_render(n): mix_block into a float buffer (an AudioWorklet)
 *   screen  an ST7789 panel model behind lcd.c: 240x240 RGB565 framebuffer
 *   input   emu_input(notes, buttons) / emu_enc(e, steps) from JS events
 *   boot    emu_init (splash) then emu_start, emu_frame() ~60/s (ui loop)
 */
#include <stdint.h>
#define FM1_XIP(off) ((uintptr_t)(emu_flash + ((uint32_t)(off) - EMU_FLASH_BASE)))      /* the flash window the firmware reads songs and samples from: our RAM image */

#define WASM_EXPORT(n) __attribute__((export_name(n), used))

/* ------------------------------------------------------------- time HAL --- */
#define FM1_TICKS_PER_US 24u
static uint64_t emu_samples;                   /* total frames rendered */
static uint32_t fm1_ticks(void) { return (uint32_t)(emu_samples * 544u); } /* ~24 MHz */
static void fm1_delay_ms(uint32_t ms) { (void)ms; }   /* never busy-wait in a worklet */
static void fm1_wdt_feed(void) {}

/* -------------------------------------------------------------- irq HAL --- */
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}

/* ------------------------------------------------------------ input HAL --- */
/* as hal/fm1_input.h, without the matrix scan: JS writes the debounced state */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };

static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

static volatile struct {
    uint32_t notes;              /* bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* bit i = matrix button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges */
    uint8_t raw[FM1_NCOL];
    uint8_t cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t enc_sub[FM1_NENC];
    int16_t enc_steps[FM1_NENC];
    uint32_t frames;
} fm1_in;

static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_bg[FM1_NCOL];
static volatile uint32_t fm1_remote_btn;          /* (2.7) buttons held over MIDI */
static uint8_t fm1_led_lv[41];                 /* the brightness of each button / key LED (ui_input.c: effects, hints) */
char _etext[1]; uint32_t _data_start[1], _data_end[1], _bss_start[1], _bss_end[1], _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1];   /* (the linker's: the ABOUT page reads them) */
static volatile uint16_t fm1_led_bg_ns;

static void fm1_input_init(void) {}

static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}

static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* -------------------------------------------------------------- adc HAL --- */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static volatile int32_t emu_adc_v[8] = {0, 0, 0, 800, 1023, 0, 0, 0};
static int32_t fm1_adc_read(uint32_t ch) { return emu_adc_v[ch & 7u]; }
static void fm1_adc_init(void) {}

/* -------------------------------------------------- lcd HAL (ST7789 model) */
/* lcd.c speaks the real panel protocol; this consumes it into a framebuffer.
 * Only CASET (2A), RASET (2B) and RAMWR (2C) matter; init commands are
 * ignored. Pixels arrive RGB565 big-endian (gfx.c pre-swaps). */
static uint16_t emu_fb[240 * 240];
static volatile uint32_t emu_fb_gen;           /* bumped on every pixel write */
static uint32_t fm1_lcd_timeouts;
static uint8_t st_cmd;
static uint32_t st_argn;
static uint8_t st_arg[4];
static uint16_t st_x0, st_x1 = 239, st_y0, st_y1 = 239, st_cx, st_cy;

static void fm1_lcd_hw_init(void) {}
static void fm1_lcd_baud(uint32_t b) { (void)b; }
static void fm1_lcd_wait(void) {}
static void fm1_lcd_deselect(void) {}

static void fm1_lcd_send_cmd(uint8_t c)
{
    st_cmd = c;
    st_argn = 0;
    if (c == 0x2Cu) {                           /* RAMWR resets the cursor */
        st_cx = st_x0;
        st_cy = st_y0;
    }
}

static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    if (st_cmd == 0x2Au || st_cmd == 0x2Bu) {
        while (n-- && st_argn < 4u)
            st_arg[st_argn++] = *b++;
        if (st_argn == 4u) {
            uint16_t a = (uint16_t)(st_arg[0] << 8 | st_arg[1]);
            uint16_t e = (uint16_t)(st_arg[2] << 8 | st_arg[3]);
            if (st_cmd == 0x2Au) {
                st_x0 = a;
                st_x1 = e;
            } else {
                st_y0 = a;
                st_y1 = e;
            }
        }
        return;
    }
    if (st_cmd != 0x2Cu && st_cmd != 0x3Cu)
        return;
    for (; n >= 2u; n -= 2u, b += 2u) {
        if (st_cx < 240u && st_cy < 240u)
            emu_fb[st_cy * 240u + st_cx] = (uint16_t)(b[0] << 8 | b[1]);
        if (++st_cx > st_x1) {
            st_cx = st_x0;
            if (++st_cy > st_y1)
                st_cy = st_y0;
        }
    }
    emu_fb_gen++;
}

/* ------------------------------------------------------------ audio HAL --- */
/* audio.c's ISR glue compiles against these but is never driven; the worklet
 * pulls mix_block through emu_render instead. */
#define FM1_AUDIO_HALF 0x80u
static void fm1_audio_init(int32_t *b, uint32_t n, void (*i)(void), uint32_t p)
{
    (void)b; (void)n; (void)i; (void)p;
}
static uint8_t fm1_audio_pending(void) { return 0; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}

/* ----------------------------------------------------------- system HAL --- */
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}

/* ------------------------------------------------------ firmware sources --- */
#include "felucca_tables.h"
#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "core.h"
#define FELUCCA_CPU1 0                                  /* (2.7) no second core in the simulator: every unit renders here */
#include "cpu1.h"
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return 0; }
static uint32_t cpu1_ticks(void) { return 0; }

/* the flash image (declared before engines.c: eng_sample.c reads the user
 * sample slots through SMP_USER_XIP, which on hardware is the memory-mapped
 * XIP window — here it points into the same RAM image the storage uses) */
#define EMU_FLASH_BASE 0x90000u
#define EMU_FLASH_SIZE 0x70000u
static uint8_t emu_flash[EMU_FLASH_SIZE];
static volatile uint32_t emu_flash_gen;
#define SMP_USER_XIP(k) (emu_flash + (SMP_USER_BASE - EMU_FLASH_BASE) + (k) * SMP_USER_SIZE)

#include "engines.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#include "usb.c"
#define FELUCCA_ARRANGER 1
#include "arranger.c"
#include "seq.c"
#include "audio.c"
#include "panel.c"
#include "ui.c"
#include "ui_studio.c"
#include "ui_draw.c"
#include "ui_layers.c"
#include "ui_menu.c"
#include "ui_nms.c"          /* No Man Sky FM: every screen */
#include "ui_game.c"         /* the game on HOME (after Low Mem Sky) */
#include "ui_input.c"
/* ---- flash: a RAM image of the FM-1's persisted areas ----
 * storage.c's A/B sector scheme runs unchanged on this array; JS loads a
 * saved image into it before boot and writes it back to IndexedDB whenever
 * emu_flash_gen moves, so projects, autosave, settings and user presets
 * survive a reload. Offsets are the real flash map (0x97000..0xFEFFF). */
#define FELUCCA_FLASH 1
static uint8_t flash_ok;
#define FL_FAR(f) f
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the expected part: flash_ok */
static void fl_plain_window_init(void) {}
static int emu_fl_ok(uint32_t off, uint32_t n)
{
    return off >= EMU_FLASH_BASE && n <= EMU_FLASH_SIZE && off - EMU_FLASH_BASE <= EMU_FLASH_SIZE - n;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!emu_fl_ok(off, n))
        return -8;
    memcpy(dst, emu_flash + (off - EMU_FLASH_BASE), n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (!emu_fl_ok(off, 4096u))
        return -8;
    memset(emu_flash + (off - EMU_FLASH_BASE), 0xFF, 4096u);
    emu_flash_gen++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!emu_fl_ok(off, n))
        return -8;
    memcpy(emu_flash + (off - EMU_FLASH_BASE), src, n);
    emu_flash_gen++;
    return 0;
}
#define ST_XIP_PTR(off) ((const uint8_t *)(emu_flash + ((off) - EMU_FLASH_BASE)))   /* (2.7) objects read in place */
#include "storage.c"
#include "upreset.c"
#include "project.c"
static int ed_smp_erase(uint32_t k, uint32_t all) { (void)k; (void)all; return 0; }      /* (editor.c is not part of the emulator) */
static int ed_sng_erase(uint32_t sid) { (void)sid; return 0; }

/* ------------------------------------------------------------- web entry --- */
static float emu_audio_f[2u * 512u];
static int32_t emu_mix[2u * CTL];
static uint32_t emu_ms_acc;

/* power-on defaults, as main.c felucca_init() */
static void emu_felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    layers_init();
    go_home();
    ui.force = 1;
}

WASM_EXPORT("emu_init") void emu_init(void)
{
    persist_boot();
    settings_init();
    lcd_init();
    sloop_splash();
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    emu_felucca_init();
}

static uint8_t emu_running;                      /* the splash stays up until emu_start (as fm1_main shows it during init) */
WASM_EXPORT("emu_start") void emu_start(void)   /* after the splash has shown */
{
    emu_running = 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
}

WASM_EXPORT("emu_frame") void emu_frame(void)   /* the main loop's UI pass */
{
    {   /* master volume as the pot in fm1_main */
        uint32_t k10 = (uint32_t)fm1_adc_read(FM1_ADC_MASTER);
        song.master_q12 = (k10 * k10) >> 8;
    }
    {   /* battery: a steady healthy reading */
        int32_t b = fm1_adc_read(FM1_ADC_BATT);
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    }
    ui_input();
    ui_leds();
    if (emu_running)
        ui_draw();
    autosave_tick();
    sections_flush();
}

WASM_EXPORT("emu_render") void emu_render(uint32_t n)
{
    uint32_t k, i;
    if (n > 512u)
        n = 512u;
    for (k = 0; k + CTL <= n; k += CTL) {
        mix_block(emu_mix, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t l = emu_mix[2u * i], r = emu_mix[2u * i + 1u];
            if (i & 1u)
                scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            emu_audio_f[2u * (k + i)] = (float)l * (1.0f / 32768.0f);
            emu_audio_f[2u * (k + i) + 1u] = (float)r * (1.0f / 32768.0f);
        }
    }
    emu_samples += n;
    emu_ms_acc += n * 1000u;
    while (emu_ms_acc >= FS) {
        emu_ms_acc -= FS;
        fm1_ms++;
    }
}

WASM_EXPORT("emu_input") void emu_input(uint32_t notes, uint32_t buttons)
{
    uint32_t pn = fm1_in.notes, pb = fm1_in.buttons;
    fm1_in.notes = notes & 0x7FFFFFFu;
    fm1_in.buttons = buttons & 0x3FFFu;
    fm1_in.notes_pressed |= notes & ~pn;
    fm1_in.pressed |= buttons & ~pb;
    fm1_in.released |= pb & ~buttons;
}

WASM_EXPORT("emu_enc") void emu_enc(uint32_t e, int32_t steps)
{
    if (e < FM1_NENC)
        fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + steps);
}

WASM_EXPORT("emu_adc_set") void emu_adc_set(uint32_t ch, int32_t v)
{
    emu_adc_v[ch & 7u] = v;
}

WASM_EXPORT("emu_midi") void emu_midi(uint32_t pkt)   /* a USB-MIDI event packet */
{
    midi_in_q[mi_w++ % MQ] = pkt;
}

WASM_EXPORT("emu_fb_ptr") uint32_t emu_fb_ptr(void) { return (uint32_t)(uintptr_t)emu_fb; }
WASM_EXPORT("emu_fb_gen") uint32_t emu_fb_gen_get(void) { return emu_fb_gen; }
WASM_EXPORT("emu_audio_ptr") uint32_t emu_audio_ptr(void) { return (uint32_t)(uintptr_t)emu_audio_f; }
WASM_EXPORT("emu_led_ptr") uint32_t emu_led_ptr(void) { return (uint32_t)(uintptr_t)fm1_led; }
WASM_EXPORT("emu_led_lv_ptr") uint32_t emu_led_lv_ptr(void) { return (uint32_t)(uintptr_t)fm1_led_lv; }
WASM_EXPORT("emu_led_dim_ptr") uint32_t emu_led_dim_ptr(void) { return (uint32_t)(uintptr_t)fm1_led_dim; }
WASM_EXPORT("emu_flash_ptr") uint32_t emu_flash_ptr(void) { return (uint32_t)(uintptr_t)emu_flash; }
WASM_EXPORT("emu_flash_size") uint32_t emu_flash_size(void) { return EMU_FLASH_SIZE; }
WASM_EXPORT("emu_flash_gen") uint32_t emu_flash_gen_get(void) { return emu_flash_gen; }

/* No Man Sky FM: what the simulator's status line shows: the selected track (0..3), the octave (+8), the layer, a
 * menu up, playing */
WASM_EXPORT("emu_state") uint32_t emu_state(void)
{
    return (uint32_t)song.sel | ((uint32_t)(song.octave + 8) & 15u) << 4 | ((uint32_t)ui.layer & 15u) << 8 |
           (uint32_t)(ui.menu != 0) << 22 | (uint32_t)(song.playing != 0) << 23;
}
WASM_EXPORT("emu_game") uint32_t emu_game(void)   /* (tests) the game: mode | resources carried << 8 */
{
    return (uint32_t)g.mode | (uint32_t)(g.res[0] + g.res[1] + g.res[2] + g.res[3]) << 8;
}
/* (tests) one figure of the game: 0 mode, 1 cargo, 2 fuel, 3 ship class, 4 credits, 5 / 6 the ship's sector, 7 the
 * sentinels, 8 the next save record, 9 / 10 the map's cursor, 11 the station row, 12 flags, 13 warps, 14 the save's seq */
WASM_EXPORT("emu_game_get") int32_t emu_game_get(uint32_t i)
{
    switch (i) {
    case 0: return g.mode;
    case 1: return g_cargo();
    case 2: return g.fuel;
    case 3: return g.cls;
    case 4: return g.cred;
    case 5: return g_sector(g.sx >> 8);
    case 6: return g_sector(g.sy >> 8);
    case 7: return g.sen_n;
    case 8: return g.slot;
    case 9: return g.mx;
    case 10: return g.my;
    case 11: return g.row;
    case 12: return g.flags;
    case 13: return g.warps;
    case 14: return (int32_t)g.seq;
    case 15: return g.sx >> 8;
    case 16: return g.sy >> 8;
    case 17: return (int32_t)g.ang;
    case 18: return (int32_t)g.zap_t;
    case 19: return (int32_t)fm1_ms;
    case 30: case 31: case 32: {                        /* (tests) an NPC planet in this system: its x, y, radius */
        planet_t p;
        uint32_t k;
        for (k = 0; k < G_NPL && g_planet(g_sector(g.sx >> 8), g_sector(g.sy >> 8), k, &p); k++)
            if (g_npc_here(&p))
                return i == 30 ? p.x : i == 31 ? p.y : G_PR(p);
        return -9999;
    }
    case 33: return g.q_on | g.talk_t ? 1 : 0;
    case 36: case 37: {                                 /* (tests) the nearest station: x, y */
        int32_t a, b, x, y, sx = g_sector(g.sx >> 8), sy = g_sector(g.sy >> 8);
        for (a = sx - 3; a <= sx + 3; a++)
            for (b = sy - 3; b <= sy + 3; b++)
                if (g_station(a, b, &x, &y))
                    return i == 36 ? x : y;
        return -9999;
    }
    case 34: return g.nx;
    case 35: return g.ny;
    case 20: return g.facing;
    case 21: return g.px >> 8;
    case 22: return g.zx;
    case 23: return g.angry;
    case 24: return g.sen_n ? g.sen[0].hp : -1;
    case 25: return g.alert_t && (int32_t)(g.alert_t - fm1_ms) > 0;
    case 26: return (int32_t)g.boom_t;
    case 27: return g.zres;
    case 28: { uint32_t k, n = 0; for (k = 0; k < 8u; k++) n += g.shot[k].t != 0; return (int32_t)n; }
    case 29: return rc.lane;
    default: return 0;
    }
}
/* (tests) set up a game off camera: 0..3 a resource, 4 credits, 5 fuel, 6 class, 7 / 8 the ship (world px), 9 heading */
WASM_EXPORT("emu_game_set") void emu_game_set(uint32_t i, int32_t v)
{
    if (!g.inited)
        g_init();
    if (i < 4u) g.res[i] = (uint16_t)v;
    else if (i == 4u) g.cred = (uint16_t)v;
    else if (i == 5u) g.fuel = (uint8_t)v;
    else if (i == 6u) g.cls = (uint8_t)clamp(v, 0, 4);
    else if (i == 7u) g.sx = v << 8;
    else if (i == 8u) g.sy = v << 8;
    else if (i == 9u) g.ang = (uint32_t)v & 1023u;
    else if (i == 10u) {                                /* (tests) a hostile planet's 5 sentinels round you */
        uint32_t k;
        g.angry = (uint8_t)(v != 0), g.sen_n = v ? 5u : 2u;
        for (k = 0; k < g.sen_n; k++)
            g.sen[k].x = g.px + ((g_cos(k * 205u) * 40) >> 7), g.sen[k].y = g.py + ((g_sin(k * 205u) * 40) >> 7), g.sen[k].hp = 3, g.sen[k].hit = 0;
    }
    else if (i == 12u)                                  /* (tests) the race, on the planet you are on */
        g_race_start();
    else if (i == 13u) {                                /* (tests) the battle, now, in space (v: the boss too) */
        uint32_t k;
        (void)k;
        g_set_mode(GM_SPACE), sf_start(v);
    }
    else if (i == 14u)                                  /* (tests) on foot: v px left of the NPC */
        g.px = g.tx = (g.nx - v) << 8, g.py = g.ty = g.ny << 8;
    else if (i == 18u)                                  /* (tests) pirates now: v of them */
        sf_raid((uint32_t)v);
    else if (i == 17u)                                  /* (tests) the space race, now */
        rc_space = 1, g_race_start();
    else if (i == 16u)                                  /* (tests) docked, here */
        g.stx = g_sector(g.sx >> 8), g.sty = g_sector(g.sy >> 8), g.row = 4, g.pick = g.cls, g.thr = 0, g.vx = g.vy = 0, g_set_mode(GM_DOCK);
    else if (i == 15u)                                  /* (tests) a passing ship now */
        g.fr_t = fm1_ms;
    else if (i == 11u)                                  /* (tests) unseen: no sentinel saw you, no one follows (a clean flight) */
        g.sen_seen = 0, g.alert_t = 0, g.angry = 0, g.sen_n = 0, g.foe_n = 0;
}
WASM_EXPORT("emu_bpm") uint32_t emu_bpm(void) { return (uint32_t)song.g[G_BPM]; }
/* (the video's composer) the selected track's sound in the PRESETS list (no favourites), a step of track t (synth: note |
 * notes << 8 | time << 12; drums: the lanes' bits), where each track's pattern is now (its index), the selected track */
WASM_EXPORT("emu_preset") uint32_t emu_preset(void) { uint32_t tot; return preset_pos_of(TSEL, &tot); }
WASM_EXPORT("emu_step") uint32_t emu_step(uint32_t t, uint32_t i)
{
    const track_t *tr = &trk[t % NTRK];
    if (is_drum(tr))
        return ((const uint16_t *)&tr->step[i % NSTEP])[0];
    return tr->step[i % NSTEP].note[0] | (uint32_t)tr->step[i % NSTEP].n << 8 | (uint32_t)tr->step[i % NSTEP].time << 12;
}
WASM_EXPORT("emu_seqidx") uint32_t emu_seqidx(uint32_t t) { return trk[t % NTRK].seq_idx; }
WASM_EXPORT("emu_len") uint32_t emu_len(uint32_t t) { return (uint32_t)trk[t % NTRK].p[P_SLEN]; }
WASM_EXPORT("emu_sel") uint32_t emu_sel(void) { return song.sel; }
/* (the video) the nearest planet whose land is orange, red or yellow (not the green ones): i 0 its x, 1 its y (world px),
 * 2 its radius in space (3 x), 3 its land colour; -1: none near */
static uint8_t emu_aim_any;                    /* (emu_aim: any planet, not only the warm ones) */
WASM_EXPORT("emu_aim_mode") void emu_aim_mode(uint32_t any) { emu_aim_any = (uint8_t)(any != 0); }
WASM_EXPORT("emu_aim") int32_t emu_aim(uint32_t i)
{
    int32_t a, b2, cx = g.sx >> 8, cy = g.sy >> 8, sx = g_sector(cx), sy = g_sector(cy), best = 1 << 30;
    planet_t p, q;
    uint32_t k;
    q.r = 0;
    for (a = sx - 2; a <= sx + 2; a++)
        for (b2 = sy - 2; b2 <= sy + 2; b2++)
            for (k = 0; k < G_NPL && g_planet(a, b2, k, &p); k++) {
                int32_t d = (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy);
                if ((emu_aim_any || p.land == 3u || p.land == 5u || p.land == 6u) && d < best)
                    best = d, q = p;
            }
    if (!q.r)
        return -1;
    return i == 0u ? q.x : i == 1u ? q.y : i == 2u ? G_PR(q) : (int32_t)q.land;
}
/* (tests) planet k of sector (sx, sy): i 0 x, 1 y, 2 its radius in space; -99999 none */
WASM_EXPORT("emu_pl") int32_t emu_pl(int32_t sx, int32_t sy, uint32_t k, uint32_t i)
{
    planet_t p;
    if (!g_planet(sx, sy, k, &p))
        return -99999;
    return i == 0u ? p.x : i == 1u ? p.y : G_PR(p);
}
/* (tests, the videos) a track's LEVEL / REV send / DLY send set off camera (what = 0, 1, 2; the drum track: its own GLO values) */
WASM_EXPORT("emu_tset") void emu_tset(uint32_t t, uint32_t what, int32_t v)
{
    static const uint8_t PI[3] = {P_LEVEL, P_REV, P_DLY}, GI[3] = {G_DRLVL, G_DRREV, G_DRDLY};
    if (what > 2u)
        return;
    if (t % NTRK == TRK_DRUM)
        song.g[GI[what]] = (int16_t)v;
    else
        trk[t % NTRK].p[PI[what]] = (int16_t)clamp(v, 0, 127);
}
WASM_EXPORT("emu_tget") int32_t emu_tget(uint32_t t, uint32_t what)
{
    static const uint8_t PI[3] = {P_LEVEL, P_REV, P_DLY}, GI[3] = {G_DRLVL, G_DRREV, G_DRDLY};
    if (what > 2u)
        return -1;
    return t % NTRK == TRK_DRUM ? song.g[GI[what]] : trk[t % NTRK].p[PI[what]];
}
WASM_EXPORT("emu_lvl") int32_t emu_lvl(uint32_t t) { return trk[t % NTRK].p[P_LEVEL]; }   /* (a track's LEVEL, 1/2 dB steps) */
WASM_EXPORT("emu_kit") uint32_t emu_kit(void) { return (uint32_t)TDRUM->p[P_E0]; }   /* the drum kit (DRUM_KIT_NAMES) */
/* (tests) the recorded FX lane: step i (255: its length), the effect playing now, a project round trip (capture, clear,
 * apply: 1 if the lane came back the same) */
WASM_EXPORT("emu_fxl") int32_t emu_fxl(uint32_t i) { return i == 255u ? fxl_len : fxl[i % NSTEP]; }
WASM_EXPORT("emu_punch") int32_t emu_punch(void) { return punch.req; }
static project_t emu_proj;
WASM_EXPORT("emu_fxl_roundtrip") int32_t emu_fxl_roundtrip(void)
{
    uint8_t keep[NSTEP], kl = fxl_len;
    memcpy(keep, fxl, sizeof keep);
    proj_capture(&emu_proj);
    memset(fxl, 0, sizeof fxl), fxl_len = 0;
    proj_apply(&emu_proj, 1);
    return fxl_len == kl && !memcmp(keep, fxl, kl) && proj_ok(&emu_proj);
}
/* (tests) an old format 6 project (as 2.7 wrote it) loads: 1 when it converts and keeps the steps */
static project_v6_t emu_v6;
WASM_EXPORT("emu_v6_import") int32_t emu_v6_import(void)
{
    proj_capture(&emu_proj);
    memcpy(&emu_v6, &emu_proj, sizeof emu_v6 - 4u);
    emu_v6.magic = PROJ_MAGIC_V6, emu_v6.size = sizeof emu_v6, emu_v6.sum = proj_hash(&emu_v6, sizeof emu_v6 - 4u);
    memset(&emu_proj, 0, sizeof emu_proj);
    if (!proj_import(&emu_proj, &emu_v6, (int)sizeof emu_v6))
        return 0;
    return proj_ok(&emu_proj) && !memcmp(emu_proj.t, emu_v6.t, sizeof emu_v6.t) && emu_proj.fx_n == 0u;
}
