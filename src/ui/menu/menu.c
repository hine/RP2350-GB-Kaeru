#include "ui/menu/menu.h"
#include "hal/display.h"
#include "drivers/input/input_keyboard.h"
#include <stdio.h>
#include <string.h>

#define N_ITEMS 6

static bool    s_open                = false;
static int     s_cursor              = 0;
static bool    s_confirm             = false;
static bool    s_confirm_sd_restore  = false;  // true=SD復元確認
static bool    s_confirm_sd_backup   = false;  // true=SDバックアップ確認
static bool    s_confirm_full_erase  = false;  // true=完全消去確認
static bool    s_toast              = false;
static char    s_toast_msg[48];

static uint8_t s_palette;
static uint8_t s_audio_en;
static uint8_t s_backlight;

// パレットプレビュー用: menu_open 時のフレームバッファ参照（Core 0 はポーズ中に変更しない）
static const uint8_t (*s_fb)[160];

static char s_labels[N_ITEMS][40];
static const char *s_label_ptrs[N_ITEMS];

static void build_labels(void) {
    snprintf(s_labels[0], sizeof(s_labels[0]), "Palette : %s", lcd_palette_name(s_palette));
    snprintf(s_labels[1], sizeof(s_labels[1]), "Audio   : %s", s_audio_en ? "ON" : "OFF");
    snprintf(s_labels[2], sizeof(s_labels[2]), "Backup to SD");
    snprintf(s_labels[3], sizeof(s_labels[3]), "Restore from SD");
    snprintf(s_labels[4], sizeof(s_labels[4]), "Clear Flash");
    snprintf(s_labels[5], sizeof(s_labels[5]), "Full Erase Flash");
    for (int i = 0; i < N_ITEMS; i++) s_label_ptrs[i] = s_labels[i];
}

void menu_open(const uint8_t fb[144][160],
               uint8_t palette, uint8_t audio_en, uint8_t backlight) {
    s_fb        = fb;
    s_palette   = palette;
    s_audio_en  = audio_en;
    s_backlight = backlight;
    s_cursor    = 0;
    s_confirm   = false;
    s_open      = true;

    build_labels();
    lcd_menu_draw(s_label_ptrs, N_ITEMS, s_cursor);
}

bool menu_is_open(void)          { return s_open; }
uint8_t menu_get_palette(void)       { return s_palette;   }
uint8_t menu_get_audio_enabled(void) { return s_audio_en;  }
uint8_t menu_get_backlight(void)     { return s_backlight;  }

static menu_action_t handle_confirm(int key) {
    if (key == '.' || key == ']' || key == KEY_ENTER) {
        s_confirm = false;
        if (s_confirm_sd_backup) {
            s_confirm_sd_backup = false;
            return MENU_ACT_SRAM_TO_SD;
        } else if (s_confirm_sd_restore) {
            s_confirm_sd_restore = false;
            return MENU_ACT_SD_TO_FLASH;
        } else if (s_confirm_full_erase) {
            s_confirm_full_erase = false;
            s_open = false;
            return MENU_ACT_FULL_ERASE_EXEC;
        } else {
            s_open = false;
            return MENU_ACT_FLASH_CLEAR_EXEC;
        }
    }
    if (key == ',' || key == '[') {
        s_confirm             = false;
        s_confirm_sd_restore  = false;
        s_confirm_sd_backup   = false;
        s_confirm_full_erase  = false;
        lcd_menu_draw(s_label_ptrs, N_ITEMS, s_cursor);
    }
    return MENU_ACT_NONE;
}

static menu_action_t handle_select(int idx) {
    switch (idx) {
        case 0: {
            s_palette = (uint8_t)((s_palette + 1) % lcd_palette_count());
            lcd_palette_set(s_palette);
            // ゲーム画面を新パレットで再描画してからメニューを重ねる
            lcd_gb_frame_redraw(s_fb);
            build_labels();
            lcd_menu_draw(s_label_ptrs, N_ITEMS, s_cursor);
            return MENU_ACT_NONE;
        }
        case 1: {
            s_audio_en ^= 1;
            build_labels();
            lcd_menu_item_redraw(s_labels[1], 1, true);
            return MENU_ACT_NONE;
        }
        case 2:
            s_confirm           = true;
            s_confirm_sd_backup = true;
            lcd_menu_draw_backup_confirm();
            return MENU_ACT_NONE;
        case 3:
            s_confirm            = true;
            s_confirm_sd_restore = true;
            lcd_menu_draw_sd_confirm();
            return MENU_ACT_NONE;
        case 4:
            s_confirm = true;
            lcd_menu_draw_confirm();
            return MENU_ACT_NONE;
        case 5:
            s_confirm            = true;
            s_confirm_full_erase = true;
            lcd_menu_draw_full_erase_confirm();
            return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

void menu_show_toast(const char *msg) {
    strncpy(s_toast_msg, msg, sizeof(s_toast_msg) - 1);
    s_toast_msg[sizeof(s_toast_msg) - 1] = '\0';
    s_toast = true;
    lcd_menu_draw_toast(s_toast_msg);
}

menu_action_t menu_tick(int key) {
    if (!s_open) return MENU_ACT_NONE;

    if (s_toast) {
        if (key != 0) {
            s_toast = false;
            lcd_menu_draw(s_label_ptrs, N_ITEMS, s_cursor);
        }
        return MENU_ACT_NONE;
    }

    if (s_confirm) return handle_confirm(key);

    if (key == KEY_UP) {
        int prev = s_cursor;
        s_cursor = (s_cursor - 1 + N_ITEMS) % N_ITEMS;
        lcd_menu_item_redraw(s_labels[prev],     prev,     false);
        lcd_menu_item_redraw(s_labels[s_cursor], s_cursor, true);
    } else if (key == KEY_DOWN) {
        int prev = s_cursor;
        s_cursor = (s_cursor + 1) % N_ITEMS;
        lcd_menu_item_redraw(s_labels[prev],     prev,     false);
        lcd_menu_item_redraw(s_labels[s_cursor], s_cursor, true);
    } else if (key == '.' || key == ']' || key == KEY_ENTER) {
        return handle_select(s_cursor);
    } else if (key == ',' || key == '[') {
        s_open = false;
        return MENU_ACT_CLOSE;
    }

    return MENU_ACT_NONE;
}
