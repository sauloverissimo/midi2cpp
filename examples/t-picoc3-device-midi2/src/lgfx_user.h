/*
 * lgfx_user.h, LovyanGFX panel definition for the LilyGO T-PicoC3
 * on-board ST7789V (240 x 135, IPS).
 *
 * Bus: RP2040 SPI0 @ 40 MHz on GP2 (SCLK) + GP3 (MOSI).
 * Pins: CS=GP5, DC=GP1, RST=GP0, BL=GP4.
 * Power: GP22 (PWR_ON) drives the panel rail; this header configures
 * the SPI/Panel/Light only: the showcase main pulls GP22 HIGH before
 * calling scene_display::init().
 */
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_TPicoC3 : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus;
    lgfx::Panel_ST7789  _panel;
    lgfx::Light_PWM     _light;

public:
    LGFX_TPicoC3() {
        {
            auto cfg = _bus.config();
            cfg.spi_host   = 0;          // RP2040 SPI0
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = 2;          // GP2
            cfg.pin_mosi   = 3;          // GP3
            cfg.pin_miso   = -1;
            cfg.pin_dc     = 1;          // GP1
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = 5;     // GP5
            cfg.pin_rst          = 0;     // GP0
            cfg.pin_busy         = -1;
            cfg.panel_width      = 135;
            cfg.panel_height     = 240;
            cfg.offset_x         = 52;
            cfg.offset_y         = 40;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 16;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;  // IPS panel
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 4;          // GP4
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};
