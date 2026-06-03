#include "drivers/display/qspi_pio.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

pio_qspi_t qspi = {
    .pio       = pio0,
    .sm        = 0,
    .sm_4wire  = 0,
    .sm_1wire  = 1,
    .pin_cs    = DISP_CS_PIN,
    .pin_sclk  = DISP_SCLK_PIN,
    .pin_dio0  = DISP_DIO0_PIN,
    .pin_dio1  = DISP_DIO1_PIN,
    .pin_dio2  = DISP_DIO2_PIN,
    .pin_dio3  = DISP_DIO3_PIN,
    .pin_pwr_en = DISP_PWR_EN_PIN,
    .pin_rst   = DISP_RST_PIN,
};

void QSPI_GPIO_Init(pio_qspi_t qspi) {
    gpio_init(qspi.pin_cs);
    gpio_pull_down(qspi.pin_cs);
    gpio_set_dir(qspi.pin_cs, GPIO_OUT);
    gpio_put(qspi.pin_cs, 1);

    gpio_init(qspi.pin_pwr_en);
    gpio_set_dir(qspi.pin_pwr_en, GPIO_OUT);
    gpio_put(qspi.pin_pwr_en, 1);

    gpio_init(qspi.pin_rst);
    gpio_set_dir(qspi.pin_rst, GPIO_OUT);
}

void QSPI_Select(pio_qspi_t qspi)   { gpio_put(qspi.pin_cs, 0); }
void QSPI_Deselect(pio_qspi_t qspi) { gpio_put(qspi.pin_cs, 1); }

void QSPI_PIO_Init(pio_qspi_t qspi) {
    uint offset = pio_add_program(qspi.pio, &qspi_4wire_data_program);
    qspi_4wire_data_program_init(qspi.pio, qspi.sm_4wire, offset,
                                 DISP_SCLK_PIN, DISP_DIO0_PIN, 4);

    // QSPI SCLK を ~75MHz に固定する。
    // PIO プログラムは 2命令/SCLK周期（out + nop）なので:
    //   div = sys_clk / (75MHz × 2)
    // 150MHz 時は div=1.0（変化なし）、200MHz 時は div≈1.33（SCLK≈75MHz）
    float qspi_div = (float)clock_get_hz(clk_sys) / (75.0f * 1000000.0f * 2.0f);
    pio_sm_set_clkdiv(qspi.pio, qspi.sm_4wire, qspi_div);
    pio_sm_set_clkdiv(qspi.pio, qspi.sm_1wire, qspi_div);

    pio_sm_set_enabled(qspi.pio, qspi.sm_4wire, false);
    pio_sm_set_enabled(qspi.pio, qspi.sm_1wire, false);
}

void QSPI_1Wrie_Mode(pio_qspi_t *qspi) {
    pio_sm_set_enabled(qspi->pio, qspi->sm_4wire, false);
    pio_sm_set_enabled(qspi->pio, qspi->sm_1wire, true);
    qspi->sm = qspi->sm_1wire;
}

void QSPI_4Wrie_Mode(pio_qspi_t *qspi) {
    pio_sm_set_enabled(qspi->pio, qspi->sm_4wire, true);
    pio_sm_set_enabled(qspi->pio, qspi->sm_1wire, false);
    qspi->sm = qspi->sm_4wire;
}

static void QSPI_PIO_Write(pio_qspi_t qspi, uint32_t val) {
    pio_sm_put_blocking(qspi.pio, qspi.sm, val << 24);
}

void QSPI_DATA_Write(pio_qspi_t qspi, uint32_t val) {
    uint8_t cmd_buf[4];
    for (int i = 0; i < 4; i++) {
        uint8_t bit1 = (val & (1u << (2 * i)))     ? 1 : 0;
        uint8_t bit2 = (val & (1u << (2 * i + 1))) ? 1 : 0;
        cmd_buf[3 - i] = bit1 | (uint8_t)(bit2 << 4);
    }
    for (int i = 0; i < 4; i++) QSPI_PIO_Write(qspi, cmd_buf[i]);
}

void QSPI_REGISTER_Write(pio_qspi_t qspi, uint32_t addr) {
    QSPI_DATA_Write(qspi, 0x02);  // SPI write cmd
    QSPI_DATA_Write(qspi, 0x00);
    QSPI_DATA_Write(qspi, addr);
    QSPI_DATA_Write(qspi, 0x00);
}

void QSPI_Pixel_Write(pio_qspi_t qspi, uint32_t addr) {
    QSPI_DATA_Write(qspi, 0x32);  // pixel write cmd
    QSPI_DATA_Write(qspi, 0x00);
    QSPI_DATA_Write(qspi, addr);
    QSPI_DATA_Write(qspi, 0x00);
}
