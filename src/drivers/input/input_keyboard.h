#ifndef INPUT_KEYBOARD_H
#define INPUT_KEYBOARD_H

#include <stdint.h>

#define I2C_KBD_MOD   i2c1
#define I2C_KBD_SDA   6
#define I2C_KBD_SCL   7
#define I2C_KBD_SPEED 10000
#define I2C_KBD_ADDR  0x1F

// 特殊キーコード（STM32 firmware keyboard.h より）
#define KEY_BACKSPACE  0x08
#define KEY_TAB        0x09
#define KEY_ENTER      0x0A
#define KEY_ESC        0xB1
#define KEY_UP         0xB5
#define KEY_DOWN       0xB6
#define KEY_LEFT       0xB4
#define KEY_RIGHT      0xB7
#define KEY_CAPS_LOCK  0xC1
#define KEY_BREAK      0xD0
#define KEY_INSERT     0xD1
#define KEY_HOME       0xD2
#define KEY_DEL        0xD4
#define KEY_END        0xD5
#define KEY_PAGE_UP    0xD6
#define KEY_PAGE_DOWN  0xD7
#define KEY_F1         0x81
#define KEY_F2         0x82
#define KEY_F3         0x83
#define KEY_F4         0x84
#define KEY_F5         0x85
#define KEY_F6         0x86
#define KEY_F7         0x87
#define KEY_F8         0x88
#define KEY_F9         0x89
#define KEY_F10        0x90
#define KEY_POWER      0x91

// モディファイアキー（CFG_REPORT_MODS が有効な場合に届く）
#define KEY_MOD_ALT    0xA1
#define KEY_MOD_SHL    0xA2  // Left Shift
#define KEY_MOD_SHR    0xA3  // Right Shift
#define KEY_MOD_SYM    0xA4
#define KEY_MOD_CTRL   0xA5

// PicoCalc メイン電源投入を待つ（キーボードコントローラの I2C 応答で検出）
// USB 単独起動時など、SD カード電源が RP2350 と独立している場合に使用する。
// kbd_init() より前に呼ぶこと。
void kbd_wait_power(void);

void kbd_init(void);
// KB コントローラが応答するまで I2C 再初期化しながら待機（Core 1 から呼ぶこと）
void kbd_wait_ready(void);
int  kbd_read(void);          // 押下キーの ASCII を返す。なければ -1
// press/release イベントを返す。戻り値=キーコード(>0)、*pressed=1押下/0離し。イベントなし/-1
int  kbd_read_event(int *pressed);
int  kbd_read_battery(void);  // バッテリー残量
int  kbd_set_backlight(uint8_t val);

#endif // INPUT_KEYBOARD_H
