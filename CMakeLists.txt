cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picocalc_gb C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# RP2350 では hardware_rtc が廃止されているため、FatFs_SPI が要求する空スタブを提供する
if(NOT TARGET hardware_rtc)
    add_library(hardware_rtc INTERFACE)
endif()

add_subdirectory(lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI)

# ── LovyanGFX (pico_sdk_init の二重呼び出しを避けるため手動統合) ──────────────
set(LGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/LovyanGFX/src)
file(GLOB LGFX_SRCS
    ${LGFX_DIR}/lgfx/utility/*.c
    ${LGFX_DIR}/lgfx/v1/*.cpp
    ${LGFX_DIR}/lgfx/v1/misc/*.cpp
    ${LGFX_DIR}/lgfx/v1/panel/*.cpp
    ${LGFX_DIR}/lgfx/v1/platforms/rp2040/*.cpp
    ${LGFX_DIR}/lgfx/v1/touch/*.cpp
)
add_library(LovyanGFX STATIC ${LGFX_SRCS})
target_include_directories(LovyanGFX PUBLIC ${LGFX_DIR})
target_compile_definitions(LovyanGFX PUBLIC USE_PICO_SDK)
target_link_libraries(LovyanGFX PUBLIC
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_gpio
    hardware_pwm
)

add_executable(picocalc_gb_kaeru
    src/app/main_picocalc.c
    src/drivers/display/video_lgfx.cpp
    src/drivers/input/input_keyboard.c
    src/drivers/storage/storage_sd.c
    src/storage/rom_flash.c
    src/storage/save_flash.c
    src/storage/flash_meta.c
    src/storage/save_sram.c
    src/storage/save_state.c
    src/drivers/audio/audio_pwm.c
    src/ui/menu/menu.c
    src/emu/gb/gb_core.c
    src/audio/minigb_apu.c
    src/boards/picocalc/hw_config.c
)

target_include_directories(picocalc_gb_kaeru PRIVATE
    src
    src/audio
    lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/include
    lib/peanut-gb
    compat   # hardware/rtc.h stub for RP2350 (removed in SDK)
)

target_link_libraries(picocalc_gb_kaeru
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_flash
    hardware_sync
    hardware_pwm
    hardware_dma
    hardware_watchdog
    FatFs_SPI
    LovyanGFX
)

target_compile_definitions(picocalc_gb_kaeru PRIVATE
    MINIGB_APU_AUDIO_FORMAT_S16SYS
)

pico_add_extra_outputs(picocalc_gb_kaeru)

# USB stdio を無効化: TinyUSB が SPI0/DMA リソースと競合し SD マウントが失敗するため。
# GB エミュレーターとして LCD が唯一の出力デバイスなので USB serial は不要。
# デバッグ時は UART (GP0/GP1) を使うか、LCD 出力で確認する。
pico_enable_stdio_usb(picocalc_gb_kaeru 0)
pico_enable_stdio_uart(picocalc_gb_kaeru 0)

# ── AMOLED GB エミュレータ ──────────────────────────────────────────────────────
# ターゲット: Waveshare RP2350-Touch-AMOLED-1.8
# SD カード(SPI1 GP25-28)からの ROM 書き込み対応
add_executable(amoled_gb_kaeru
    src/app/main_amoled.c
    src/drivers/display/qspi_pio.c
    src/drivers/display/amoled_1in8.c
    src/drivers/input/ft3168_touch.c
    src/drivers/audio/es8311.c
    src/drivers/audio/audio_i2s.c
    src/emu/gb/gb_core.c
    src/audio/minigb_apu.c
    src/storage/rom_flash.c
    src/storage/flash_meta.c
    src/storage/save_flash.c
    src/storage/save_sram.c
    src/drivers/storage/storage_sd.c
    src/boards/waveshare_touch_amoled_1_8/hw_config.c
    src/boards/waveshare_touch_amoled_1_8/display_stubs.c
)

pico_generate_pio_header(amoled_gb_kaeru
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/display/qspi.pio
)

target_include_directories(amoled_gb_kaeru PRIVATE
    src
    src/audio
    lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/include
    lib/peanut-gb
    compat
)

target_link_libraries(amoled_gb_kaeru
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_gpio
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_flash
    hardware_sync
    hardware_spi
    hardware_watchdog
    FatFs_SPI
)

target_compile_definitions(amoled_gb_kaeru PRIVATE
    MINIGB_APU_AUDIO_FORMAT_S16SYS
    # ES8311 I2S @ 32000 Hz: AUDIO_SAMPLES=535/frame, Bresenham で正確な平均レートを維持
    AUDIO_SAMPLE_RATE=32000
    GB_AUDIO_SAMPLES=535
)

pico_add_extra_outputs(amoled_gb_kaeru)
pico_enable_stdio_usb(amoled_gb_kaeru 0)
pico_enable_stdio_uart(amoled_gb_kaeru 0)
