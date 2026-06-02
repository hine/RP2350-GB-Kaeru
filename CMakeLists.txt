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
    src/app/main.c
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
    lib/peanut-gb/examples/sdl2/minigb_apu/minigb_apu.c
    src/drivers/storage/hw_config.c
)

target_include_directories(picocalc_gb_kaeru PRIVATE
    src
    lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI/include
    lib/peanut-gb
    lib/peanut-gb/examples/sdl2/minigb_apu
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

# ── AMOLED タッチ確認テスト (Phase 2a) ─────────────────────────────────────────
# ターゲット: Waveshare RP2350-Touch-AMOLED-1.8
# printf → USB CDC (接続中の USB ケーブルそのまま使用)
add_executable(amoled_touch_test
    src/app/main_touch_test.c
    src/drivers/display/qspi_pio.c
    src/drivers/display/amoled_1in8.c
    src/drivers/input/ft3168_touch.c
)

pico_generate_pio_header(amoled_touch_test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/display/qspi.pio
)

target_include_directories(amoled_touch_test PRIVATE
    src
)

target_link_libraries(amoled_touch_test
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_pio
    hardware_dma
    hardware_clocks
)

pico_add_extra_outputs(amoled_touch_test)
pico_enable_stdio_usb(amoled_touch_test 1)
pico_enable_stdio_uart(amoled_touch_test 0)

# ── AMOLED GB エミュレータ (Phase 3) ────────────────────────────────────────────
# ターゲット: Waveshare RP2350-Touch-AMOLED-1.8
# ROM は PicoCalc ビルドで Flash 書き込み済みの前提（SD カード不使用）
add_executable(amoled_gb_kaeru
    src/app/main_amoled.c
    src/drivers/display/qspi_pio.c
    src/drivers/display/amoled_1in8.c
    src/drivers/input/ft3168_touch.c
    src/emu/gb/gb_core.c
    lib/peanut-gb/examples/sdl2/minigb_apu/minigb_apu.c
    src/boards/waveshare_touch_amoled_1_8/storage_stubs.c
    src/storage/flash_meta.c
)

pico_generate_pio_header(amoled_gb_kaeru
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/display/qspi.pio
)

target_include_directories(amoled_gb_kaeru PRIVATE
    src
    lib/peanut-gb
    lib/peanut-gb/examples/sdl2/minigb_apu
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
)

target_compile_definitions(amoled_gb_kaeru PRIVATE
    MINIGB_APU_AUDIO_FORMAT_S16SYS
)

pico_add_extra_outputs(amoled_gb_kaeru)
pico_enable_stdio_usb(amoled_gb_kaeru 1)
pico_enable_stdio_uart(amoled_gb_kaeru 0)
