#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "drivers/input/input_keyboard.h"

static uint8_t kbd_inited = 0;

void kbd_wait_power(void) {
    uint8_t msg = 0x09;
    for (;;) {
        // タイムアウト後の I2C stuck bus を毎回リセットする
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        if (i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 100000) >= 0)
            break;
        sleep_ms(200);
    }
}

void kbd_init(void) {
    i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
    gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
    gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_KBD_SCL);
    gpio_pull_up(I2C_KBD_SDA);
    kbd_inited = 1;
}

// Core 1 から呼ぶこと。KB コントローラの応答を待ってから kbd_inited を立てる。
// Core 0（GB エミュレータ）と並走するため、どれだけ待っても画面描画には影響しない。
void kbd_wait_ready(void) {
    uint8_t msg = 0x09;
    for (;;) {
        // タイムアウト後の I2C 内部状態をリセットしてから再試行する
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        if (i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 100000) >= 0)
            break;
        sleep_ms(500);
    }
    kbd_inited = 1;
}

int kbd_read(void) {
    if (!kbd_inited) return -1;

    uint16_t buff = 0;
    uint8_t msg = 0x09;
    static int ctrl_held = 0;

    int ret = i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 500000);
    if (ret < 0) return -1;

    sleep_ms(2);

    ret = i2c_read_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, (uint8_t *)&buff, 2, false, 500000);
    if (ret < 0) return -1;

    if (buff == 0) return -1;

    if (buff == 0x7E03) { ctrl_held = 0; return -1; }
    if (buff == 0x7E02) { ctrl_held = 1; return -1; }

    if ((buff & 0xFF) == 1) {
        int c = buff >> 8;
        if (c >= 'a' && c <= 'z' && ctrl_held) c = c - 'a' + 1;
        return c;
    }

    return -1;
}

int kbd_read_event(int *pressed) {
    if (!kbd_inited) return -1;

    uint16_t buff = 0;
    uint8_t msg = 0x09;
    static int ctrl_held = 0;

    int ret = i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 500000);
    if (ret < 0) return -1;

    sleep_ms(2);

    ret = i2c_read_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, (uint8_t *)&buff, 2, false, 500000);
    if (ret < 0) return -1;

    if (buff == 0) return -1;

    if (buff == 0x7E03) { ctrl_held = 0; return -1; }
    if (buff == 0x7E02) { ctrl_held = 1; return -1; }

    uint8_t state = buff & 0xFF;
    if (state == 1 || state == 3) {
        int c = buff >> 8;
        if (c >= 'a' && c <= 'z' && ctrl_held) c = c - 'a' + 1;
        *pressed = (state == 1) ? 1 : 0;
        return c;
    }

    return -1;
}

int kbd_read_battery(void) {
    if (!kbd_inited) return -1;

    uint16_t buff = 0;
    uint8_t msg = 0x0B;

    int ret = i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 500000);
    if (ret < 0) {
        // NACK: レジスタ 0x0B がファームウェア未実装。
        // バスをリセットして次のキーボード読み取りへの影響を防ぐ。
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        return -1;
    }

    sleep_ms(16);

    ret = i2c_read_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, (uint8_t *)&buff, 2, false, 500000);
    if (ret < 0) return -1;

    return (int)buff;  // 0 = データなし、正値 = 有効値、-1 = I2C エラー
}

int kbd_set_backlight(uint8_t val) {
    if (!kbd_inited) return -1;

    uint8_t msg[2] = { 0x0A | 0x80, val };

    int ret = i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, msg, 2, false, 500000);
    if (ret < 0) {
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        return -1;
    }
    // STM32 は write 後に read フェーズを期待する。省略するとバスがスタックする。
    uint16_t buff = 0;
    sleep_ms(16);
    ret = i2c_read_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, (uint8_t *)&buff, 2, false, 500000);
    if (ret < 0) {
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        return -1;
    }
    return 0;
}
