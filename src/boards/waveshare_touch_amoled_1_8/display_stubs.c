#include "hal/display.h"
#include <stdio.h>

// AMOLED ビルド向け display HAL スタブ。
// rom_flash.c が lcd_print_string() を進捗表示に使うため、
// printf に転送する最小実装を提供する。

void lcd_print_string(const char *s) { printf("%s", s); }

// 以下は rom_flash.c 以外から参照されないが、
// リンカが要求する場合に備えた空スタブ。
void lcd_init(void) {}
void lcd_clear(void) {}
