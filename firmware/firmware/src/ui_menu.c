/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The menu (HOME held), in sections: LIGHTS (LIGHTS, KEYS, NOTES), KEY LED (EFFECT, SPEED, FADE, HOLD: the light a
 * key press makes, from winampFM), AUDIO (LOWCUT, USB AUDIO, USB RATE (2.7), USB SERIAL (a build with the serial
 * console only)), VISUAL (how see-through the numbers are over the visuals; KNOB ACCEL (2.7)), SYSTEM (2ND CORE
 * (2.7), HARDWARE CALIBRATION, LEARN CLEAR, ABOUT). SELECT goes to the section before / after (stopping at the
 * ends), KNOB 1..4 set the section's settings in order, PRESETS moves the cursor; OCT+ steps the cursor's setting
 * round or opens it (CALIBRATION, ABOUT), OCT- closes (from ABOUT: back to the section). ui_nms.c draws it.
 * (No Man Sky FM: SLOOP's SCREEN section, COLOR (and 2.7's theme) and ZOOM, is gone: the look is its own) */
/* ------------------------------------------------------------ menu --- */
enum { MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LEDFX, MI_LEDSP, MI_LEDFD, MI_LEDHOLD, MI_LOWCUT, MI_USB, MI_RATE,
#if FELUCCA_CDC
       MI_SERIAL,
#endif
       MI_VZNOTE, MI_ACCEL, MI_CPU1, MI_PANEL, MI_LCLEAR, MI_ABOUT, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"LIGHTS", "KEYS", "NOTES", "KEY LED", "LED SPEED", "LED FADE",
                                              "LED WHILE HELD", "SPEAKER LOWCUT", "USB AUDIO", "USB RATE",
#if FELUCCA_CDC
                                              "USB SERIAL",
#endif
                                              "NUMBERS OVER VISUAL", "KNOB ACCEL", "2ND CORE",
                                              "HARDWARE CALIBRATION", "MIDI LEARN CLEAR", "ABOUT"};
enum { MS_LIGHTS, MS_LED, MS_AUDIO, MS_VISUAL, MS_SYSTEM, MS_COUNT };
static const uint8_t MS_FIRST[MS_COUNT + 1] = {MI_LIGHTS, MI_LEDFX, MI_LOWCUT, MI_VZNOTE, MI_CPU1, MI_COUNT};   /* rows of each */
static int mi_cpu1_have(void)                           /* the second core is running (cpu1.c) */
{
#if FELUCCA_CPU1
    return cpu1_state == CPU1_OK;
#else
    return 0;
#endif
}
static uint32_t vz_set(uint32_t which);                 /* ui_nms.c: the visuals by themselves */
static void vz_put(uint32_t which, uint32_t v);
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */

static uint32_t mi_sec(uint32_t i)                 /* the section of item i */
{
    uint32_t k = 0;
    while (k + 1u < MS_COUNT && i >= MS_FIRST[k + 1u])
        k++;
    return k;
}

/* item i's value as shown ("" for an action: OCT+ opens it) */
static const char *mi_value(uint32_t i)
{
    switch (i) {
    case MI_LIGHTS: return LIGHTS_NAME[lights_lvl % LIGHTS_N];
    case MI_KEYS: return KEYS_NAME[lights_keys % KEYS_N];
    case MI_NOTES: return lights_notes ? "ON" : "OFF";
    case MI_LOWCUT: return settings.lowcut ? "ON" : "OFF";
    case MI_USB: return usb_full ? "FULL" : "MASTER";
#if FELUCCA_CDC
    case MI_SERIAL: return usb_serial ? "ON" : "OFF";
#endif
    case MI_RATE: {                                   /* (2.7) the rates the USB audio input offers (usb.c) */
        static const char *const R[3] = {"AUTO", "44.1 KHZ", "48 KHZ"};
        return R[usb_rate % 3u];
    }
    case MI_ACCEL: return knob_accel_off ? "OFF" : "ON";   /* (2.7) the knobs speed up with a fast turn */
    case MI_CPU1: return cpu1_off ? "OFF" : mi_cpu1_have() ? "ON" : "ON (OFF NOW)";   /* (2.7) the sound on both cores */
    case MI_LEDFX: return LED_NAME[led_opt(0) % 6u];
    case MI_LEDSP:
    case MI_LEDFD: {
        static char b[2][4];
        uint32_t w = i == MI_LEDFD;
        fmt_int(b[w], (int32_t)led_opt(1u + w));
        return b[w];
    }
    case MI_LEDHOLD: return led_opt(3) ? "ON" : "OFF";
    case MI_VZNOTE: {                                 /* the number's opacity over the visuals, or OFF */
        static char b[12];
        uint32_t v = vz_set(0);
        if (!v)
            return "OFF";
        str_cpy(b, "NUMBER ", 12);
        fmt_int(b + 7, (int32_t)((v - 1u) * 5u));
        str_cpy(b + str_len(b), "%", 2);
        return b;
    }
    case MI_LCLEAR: {                                 /* (2.6) the CCs MIDI LEARN holds; OCT+ clears them */
        static char b[8];
        fmt_int(b, (int32_t)ml_count());
        return b;
    }
    default:
        return "";
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* the settings / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* item i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
static void mi_set(uint32_t i, int32_t s)
{
    switch (i) {
    case MI_LOWCUT:                                    /* right ON, left OFF; OCT+ toggles */
        settings.lowcut = s > 0 ? 1u : s < 0 ? 0u : !settings.lowcut;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        break;
    case MI_USB:                                       /* right FULL, left MASTER */
        usb_full = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_full);
        break;
#if FELUCCA_CDC
    case MI_SERIAL:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
#endif
    case MI_RATE:                                      /* (2.7) AUTO 44.1 48: the knob steps, OCT+ round; next start */
        if (s > 0 && usb_rate < 2u)
            usb_rate++;
        else if (s < 0 && usb_rate > 0u)
            usb_rate--;
        else if (!s)
            usb_rate = (uint8_t)((usb_rate + 1u) % 3u);
        if (usb_rate != usb_rate_now())
            ui_message("AT THE NEXT START");
        break;
    case MI_ACCEL:                                     /* (2.7) right ON, left OFF; OCT+ toggles */
        knob_accel_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !knob_accel_off);
        break;
    case MI_CPU1:                                      /* (2.7) right ON, left OFF; at once */
        cpu1_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !cpu1_off);
        cpu1_units = cpu1_off ? 0 : -1;
        if (!cpu1_off && !cpu1_on())                   /* (core 1 starts only at power-on) */
            ui_message("ON AT NEXT POWER-ON");
        break;
    case MI_LEDFX: {                                   /* the effect, in the menu's order; a turn shows it on the keys */
        uint32_t q, pos = 0, cur = led_opt(0);
        for (q = 0; q < 6u; q++)
            if (LED_ORDER[q] == cur)
                pos = q;
        pos = s > 0 || !s ? (pos + 1u) % 6u : (pos + 5u) % 6u;
        led_opt_put(0, LED_ORDER[pos]);
        led_demo_t0 = fm1_ms | 1u;
        break;
    }
    case MI_LEDSP:
    case MI_LEDFD: {                                   /* 0..15, stops at the ends; OCT+ round */
        uint32_t k = i == MI_LEDSP ? 1u : 2u;
        int32_t v = (int32_t)led_opt(k);
        v = s ? clamp(v + (s > 0 ? 1 : -1), 0, 15) : (v + 1) % 16;
        led_opt_put(k, (uint32_t)v);
        led_demo_t0 = fm1_ms | 1u;
        break;
    }
    case MI_LEDHOLD:
        led_opt_put(3, s > 0 ? 1u : s < 0 ? 0u : !led_opt(3));
        led_demo_t0 = fm1_ms | 1u;
        break;
    case MI_NOTES:
        lights_notes = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !lights_notes);
        break;
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = i == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = i == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (i == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
    case MI_VZNOTE: {                                  /* 5 % a detent (left past 0 %: OFF); OCT+ steps round */
        int32_t v = (int32_t)vz_set(0);
        v = s ? clamp(v + (s > 0 ? 1 : -1), 0, 21) : (v + 1) % 22;
        vz_put(0, (uint32_t)v);
        break;
    }
    case MI_LCLEAR:                                    /* (2.6) every learned CC: OCT+ */
        if (!s)
            ui_message(ml_clear_all() ? "LEARN CLEARED" : "NOTHING LEARNED");
        break;
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    default:
        break;
    }
}

/* menu: SELECT the section, KNOB 1..3 its settings, PRESETS the cursor, OCT+ ok, OCT- close (ABOUT -> the section) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, sec = mi_sec(sel), n = MS_FIRST[sec + 1u] - MS_FIRST[sec];
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the section before / after (stops at the ends) */
        int32_t to = clamp((int32_t)sec + (s > 0 ? 1 : -1), 0, MS_COUNT - 1);
        if ((uint32_t)to != sec) {
            ui.menu_sel = MS_FIRST[to];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)               /* the cursor, round the section's rows */
        ui.menu_sel = (uint8_t)(MS_FIRST[sec] + (sel - MS_FIRST[sec] + (s > 0 ? 1u : n - 1u)) % n);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = (uint8_t)(MS_FIRST[sec] + k);   /* (the cursor follows the knob turned) */
            mi_set(MS_FIRST[sec] + k, s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    enc_drop();                                        /* swallow the rest while the menu is up */
}
