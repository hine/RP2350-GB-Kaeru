#include "lgfx_config.hpp"
#include "hal/display.h"
#include <string.h>

static LGFX_PicoCalc lcd;

// パレット定義: [palette_idx][shade_0..3][R,G,B]
// 画像番号: 1,2,3,4,5,6,8,10,12,14  (AMOLED と共通)
#define N_PALETTES 10
static const uint8_t g_palettes[N_PALETTES][4][3] = {
    { // 0: DMGGreen
        {0x9B,0xBC,0x0F},{0x8B,0xAC,0x0F},{0x30,0x62,0x30},{0x0F,0x38,0x0F},
    },
    { // 1: DarkGreen
        {0x6A,0x8C,0x50},{0x48,0x64,0x2C},{0x24,0x38,0x10},{0x08,0x14,0x04},
    },
    { // 2: Mono
        {0xFF,0xFF,0xFF},{0xAA,0xAA,0xAA},{0x55,0x55,0x55},{0x00,0x00,0x00},
    },
    { // 3: Sepia
        {0xF5,0xE6,0xC8},{0xC4,0x96,0x6A},{0x7A,0x55,0x32},{0x2A,0x15,0x00},
    },
    { // 4: Amber
        {0xF8,0xE0,0x38},{0xD8,0x98,0x00},{0x8C,0x4C,0x00},{0x38,0x18,0x00},
    },
    { // 5: CoolBlue
        {0x94,0xC0,0xEC},{0x50,0x84,0xC4},{0x20,0x48,0x94},{0x04,0x14,0x4C},
    },
    { // 6: Teal
        {0x80,0xDC,0xC4},{0x38,0xA0,0x8C},{0x08,0x64,0x60},{0x00,0x24,0x28},
    },
    { // 7: Lavender
        {0xD4,0xC4,0xF4},{0x98,0x80,0xD8},{0x58,0x40,0xA8},{0x18,0x08,0x50},
    },
    { // 8: Rose
        {0xF0,0xB8,0xC8},{0xD4,0x74,0x94},{0xA0,0x34,0x58},{0x44,0x04,0x20},
    },
    { // 9: RedMono
        {0xFC,0xA4,0x9C},{0xDC,0x58,0x50},{0xA0,0x18,0x18},{0x30,0x00,0x00},
    },
};
static const char *g_palette_names[N_PALETTES] = {
    "DMGGreen", "DarkGreen", "Mono", "Sepia",
    "Amber", "CoolBlue", "Teal", "Lavender", "Rose", "RedMono"
};
static int g_palette_idx = 0;

// LCD 上での GB 画面オフセット: (320 - 144×2) / 2 = 16
#define GB_Y_OFF  16

// 差分検出用前フレームバッファ
static uint8_t prev_fb[144][160];

// 2x スケール行バッファ (320 pixels, RGB565)
static uint16_t row_buf[320];

// RGB565 パレット (lgfx::color565 で生成、writePixels swap=true で正しく転送)
static uint16_t dmg_pal565[4];

static void apply_palette(int idx) {
    for (int i = 0; i < 4; i++)
        dmg_pal565[i] = lgfx::color565(
            g_palettes[idx][i][0],
            g_palettes[idx][i][1],
            g_palettes[idx][i][2]);
}

// ─── ステータスバー定数 ───────────────────────────────────────────────────────
// 上部: y=0..15 (16px), 下部: y=304..319 (16px)
#define STATUS_BOTTOM_Y   304
#define STATUS_BOTTOM_BG  lcd.color888(24, 24, 24)
#define STATUS_HINT_FG    lcd.color888(200, 200, 200)

// Font0 (6×8) textSize=1 → 6×8px/char → 320/6 = 53文字/行
// ヒントテキストは 53 文字以内に収める（16px ストリップに Y+4 で縦中央揃え）
static const char HINTS[] = "ESC=Menu ./]=A ,/[=B Ent/BS=St Del=Sel WASD/^v<>=Dir";

void lcd_init(void) {
    lcd.init();
    lcd.setRotation(6);  // MADCTL: MX=0,MY=0 → ミラーなし正常ポートレート

    apply_palette(0);

    // 初回は全画面再描画を強制
    memset(prev_fb, 0xFF, sizeof(prev_fb));

    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setCursor(0, 0);
}

void lcd_clear(void) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setCursor(0, 0);
}

void lcd_print_string(const char *s) {
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.print(s);
}

// GB 画面を 2x スケールで LCD に差分描画する
void lcd_gb_frame_delta(const uint8_t fb[144][160]) {
    lcd.startWrite();

    int y = 0;
    while (y < 144) {
        if (memcmp(fb[y], prev_fb[y], 160) == 0) {
            y++;
            continue;
        }

        // 連続する変化行をまとめて転送
        int ys = y;
        while (y < 144 && memcmp(fb[y], prev_fb[y], 160) != 0)
            y++;

        // LCD ウィンドウ設定 (2x スケール)
        lcd.setWindow(0, ys * 2 + GB_Y_OFF, 319, (y - 1) * 2 + 1 + GB_Y_OFF);

        for (int row = ys; row < y; row++) {
            // 1行を 2x 水平スケールでバッファに展開
            for (int x = 0; x < 160; x++) {
                uint16_t c = dmg_pal565[fb[row][x] & 3];
                row_buf[x * 2]     = c;
                row_buf[x * 2 + 1] = c;
            }
            // 同じ行を 2 回送信（2x 垂直スケール）; swap=true で rgb565_t レイアウトとして転送
            lcd.writePixels(row_buf, 320, true);
            lcd.writePixels(row_buf, 320, true);
        }
    }

    lcd.endWrite();
    memcpy(prev_fb, fb, sizeof(prev_fb));
}

// セーブステートロード後などに全画面強制再描画
void lcd_gb_frame_invalidate(void) {
    memset(prev_fb, 0xFF, sizeof(prev_fb));
}

// 現在のパレットで全画面強制再描画（メニューのパレットプレビュー用）
void lcd_gb_frame_redraw(const uint8_t fb[144][160]) {
    lcd_gb_frame_invalidate();
    lcd_gb_frame_delta(fb);
}

// ── パレット ─────────────────────────────────────────────────────────────────

int lcd_palette_count(void) { return N_PALETTES; }
int lcd_palette_get(void)   { return g_palette_idx; }

const char *lcd_palette_name(int idx) {
    if (idx < 0 || idx >= N_PALETTES) return "?";
    return g_palette_names[idx];
}

void lcd_palette_set(int idx) {
    if (idx < 0 || idx >= N_PALETTES) return;
    g_palette_idx = idx;
    apply_palette(idx);
    lcd_gb_frame_invalidate();
}

// ─── ステータスバー ─────────────────────────────────────────────────────────

void lcd_status_draw_hints(void) {
    lcd.startWrite();
    lcd.fillRect(0, STATUS_BOTTOM_Y, 320, 16, STATUS_BOTTOM_BG);
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(STATUS_HINT_FG, STATUS_BOTTOM_BG);
    lcd.setCursor(2, STATUS_BOTTOM_Y + 4);  // 8px フォントを 16px ストリップ内で縦中央に
    lcd.print(HINTS);
    lcd.endWrite();
}

// 上部 16px ストリップ中央（x=52..281）に F1-F4 キーバインドを描画する
void lcd_status_draw_fkey_hints(void) {
    static const char FHINTS[] = "F1=Reset  F2=Save  F3=Load  F4=Slot";
    lcd.startWrite();
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.fillRect(52, 0, 228, 16, TFT_BLACK);
    lcd.setTextColor(lcd.color888(200, 200, 200), TFT_BLACK);
    lcd.setCursor(52, 4);
    lcd.print(FHINTS);
    lcd.endWrite();
}

void lcd_status_storage_icon(storage_icon_t state) {
    uint32_t color;
    switch (state) {
        case STORAGE_ICON_READ:  color = lcd.color888(255, 255, 255); break;  // 白: 読み込み
        case STORAGE_ICON_SD:    color = lcd.color888(64,  160, 255); break;  // 青: SD 書き込み
        case STORAGE_ICON_FLASH: color = lcd.color888(255, 200,   0); break;  // 黄: Flash 書き込み
        default:                 color = TFT_BLACK;                   break;  // 消灯
    }
    lcd.fillRect(310, 2, 8, 12, color);
}

void lcd_status_top_text(const char *msg) {
    lcd.startWrite();
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    // 8文字 × 6px = 48px の領域をクリアしてから描画
    lcd.fillRect(0, 0, 50, 16, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 4);  // 8px フォントを 16px ストリップ内で縦中央に
    lcd.print(msg);
    lcd.endWrite();
}

// ── メニューオーバーレイ ─────────────────────────────────────────────────────
// パネル: x=30,y=65,w=260,h=185
// タイトル(y=73) / セパレータ(y=89) / 項目(y=93+18*n) / フッタセパレータ(y=236) / フッタ(y=241)

#define MENU_X       30
#define MENU_Y       65
#define MENU_W      260
#define MENU_H      185
#define MENU_ITEM_Y0  93
#define MENU_ITEM_H   18
#define MENU_TEXT_X  (MENU_X + 10)
#define MENU_BG_C    20, 20, 20
#define MENU_SEL_C   60, 60, 60
#define MENU_DIM_C  160,160,160

static void menu_draw_item_inner(const char *label, int idx, bool selected) {
    int y    = MENU_ITEM_Y0 + idx * MENU_ITEM_H;
    uint32_t bg = selected ? lcd.color888(MENU_SEL_C) : lcd.color888(MENU_BG_C);
    uint32_t fg = selected ? (uint32_t)TFT_WHITE      : lcd.color888(MENU_DIM_C);
    lcd.fillRect(MENU_X + 2, y, MENU_W - 4, MENU_ITEM_H - 1, bg);
    lcd.setTextColor(fg, bg);
    lcd.setCursor(MENU_TEXT_X, y + 4);
    lcd.print(label);
}

void lcd_menu_draw(const char *const items[], int n, int cursor) {
    lcd.startWrite();
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);

    lcd.fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 4, lcd.color888(MENU_BG_C));
    lcd.drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 4, TFT_WHITE);

    lcd.setTextColor(TFT_WHITE, lcd.color888(MENU_BG_C));
    lcd.setCursor(MENU_TEXT_X, MENU_Y + 8);
    lcd.print("MENU");
    lcd.drawFastHLine(MENU_X + 2, MENU_Y + 22, MENU_W - 4, lcd.color888(80, 80, 80));

    for (int i = 0; i < n; i++)
        menu_draw_item_inner(items[i], i, i == cursor);

    int fy = MENU_Y + MENU_H - 22;
    lcd.drawFastHLine(MENU_X + 2, fy, MENU_W - 4, lcd.color888(80, 80, 80));
    lcd.setTextColor(lcd.color888(MENU_DIM_C), lcd.color888(MENU_BG_C));
    lcd.setCursor(MENU_TEXT_X, fy + 6);
    lcd.print("A/Ent:OK  B/ESC:Close");

    lcd.endWrite();
}

void lcd_menu_item_redraw(const char *label, int idx, bool selected) {
    lcd.startWrite();
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    menu_draw_item_inner(label, idx, selected);
    lcd.endWrite();
}

void lcd_menu_draw_confirm(void) {
    int x = 60, y = 110, w = 200, h = 100;
    lcd.startWrite();
    lcd.fillRoundRect(x, y, w, h, 4, lcd.color888(60, 0, 0));
    lcd.drawRoundRect(x, y, w, h, 4, TFT_RED);
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, lcd.color888(60, 0, 0));
    lcd.setCursor(x + 8, y + 10);
    lcd.print("Clear all Flash?");
    lcd.setCursor(x + 8, y + 22);
    lcd.print("ROM/SRAM/Settings");
    lcd.setCursor(x + 8, y + 34);
    lcd.print("will be erased.");
    lcd.drawFastHLine(x + 2, y + 50, w - 4, lcd.color888(120, 0, 0));
    lcd.setCursor(x + 8, y + 58);
    lcd.print("A/Ent:Execute");
    lcd.setCursor(x + 8, y + 70);
    lcd.print("B/ESC:Cancel");
    lcd.endWrite();
}

void lcd_menu_draw_sd_confirm(void) {
    int x = 55, y = 105, w = 210, h = 110;
    lcd.startWrite();
    lcd.fillRoundRect(x, y, w, h, 4, lcd.color888(40, 30, 0));
    lcd.drawRoundRect(x, y, w, h, 4, lcd.color888(255, 160, 0));
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, lcd.color888(40, 30, 0));
    lcd.setCursor(x + 8, y + 10);
    lcd.print("Restore from SD?");
    lcd.setCursor(x + 8, y + 22);
    lcd.print("Flash save will be");
    lcd.setCursor(x + 8, y + 34);
    lcd.print("overwritten.");
    lcd.drawFastHLine(x + 2, y + 50, w - 4, lcd.color888(120, 80, 0));
    lcd.setCursor(x + 8, y + 58);
    lcd.print("A/Ent:Execute");
    lcd.setCursor(x + 8, y + 70);
    lcd.print("B/ESC:Cancel");
    lcd.endWrite();
}

void lcd_menu_draw_toast(const char *msg) {
    int x = 55, y = 130, w = 210, h = 60;
    lcd.startWrite();
    lcd.fillRoundRect(x, y, w, h, 4, lcd.color888(0, 40, 20));
    lcd.drawRoundRect(x, y, w, h, 4, lcd.color888(0, 200, 80));
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, lcd.color888(0, 40, 20));
    lcd.setCursor(x + 8, y + 12);
    lcd.print(msg);
    lcd.setTextColor(lcd.color888(MENU_DIM_C), lcd.color888(0, 40, 20));
    lcd.setCursor(x + 8, y + 38);
    lcd.print("Any key: OK");
    lcd.endWrite();
}

// 上部 16px ストリップ右側（ストレージアイコン左隣）にバッテリー残量を表示する。
// pct: 0-100 = 残量%、-1 = 不明
void lcd_status_battery(int pct) {
    static int prev_pct = -2;
    if (pct == prev_pct) return;
    prev_pct = pct;

    char buf[5];
    uint32_t fg;
    if (pct < 0) {
        strcpy(buf, " -- ");
        fg = lcd.color888(128, 128, 128);
    } else {
        snprintf(buf, sizeof(buf), "%3d%%", pct);
        if (pct > 50)      fg = lcd.color888(0, 220, 64);
        else if (pct > 20) fg = lcd.color888(255, 200, 0);
        else               fg = lcd.color888(255, 64, 64);
    }

    // 4文字 × 6px = 24px、ストレージアイコン(x=310)の4px手前
    lcd.startWrite();
    lcd.setFont(&lgfx::fonts::Font0);
    lcd.setTextSize(1);
    lcd.fillRect(282, 0, 24, 16, TFT_BLACK);
    lcd.setTextColor(fg, TFT_BLACK);
    lcd.setCursor(282, 4);
    lcd.print(buf);
    lcd.endWrite();
}
