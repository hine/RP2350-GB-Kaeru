#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// PicoCalc LCD: ILI9488 (または互換 ST7365P), SPI1 (GP10-15), 320×320
struct LGFX_PicoCalc : public lgfx::LGFX_Device {
private:
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI       _bus;

public:
    LGFX_PicoCalc(void) {
        {   // SPI1 バス設定
            auto cfg = _bus.config();
            cfg.spi_host   = 1;          // SPI1
            cfg.freq_write = 40000000;   // 40MHz (PicoCalc は 25〜75MHz 動作報告あり)
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = 10;  // GP10
            cfg.pin_mosi   = 11;  // GP11
            cfg.pin_miso   = 12;  // GP12
            cfg.pin_dc     = 14;  // GP14
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // ILI9488 パネル設定
            auto cfg = _panel.config();
            cfg.pin_cs          = 13;   // GP13
            cfg.pin_rst         = 15;   // GP15
            cfg.pin_busy        = -1;
            cfg.memory_width    = 320;
            cfg.memory_height   = 480;  // 物理パネルは 480 行
            cfg.panel_width     = 320;
            cfg.panel_height    = 320;  // PicoCalc は上 320 行のみ表示
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = false; // 書き込み専用: 高速化
            cfg.invert          = true;  // Display Inversion On (ILI9488 パネルの自然反転を補正)
            cfg.rgb_order       = false; // BGR (MADCTL bit3=1): このパネルは BGR 順のため false
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};
