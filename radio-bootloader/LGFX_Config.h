#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ST7789 170x320, 8-bit 8080 parallel, pin map from schematic V1.2
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789   _panel;
  lgfx::Bus_Parallel8  _bus;
  lgfx::Light_PWM      _light;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus.config();
      cfg.port       = 0;
      cfg.freq_write = 10000000;   // 10MHz parallel bus, stable for this board
      cfg.pin_wr     = 8;
      cfg.pin_rd     = 9;
      cfg.pin_rs     = 7;          // DC
      cfg.pin_d0     = 39;
      cfg.pin_d1     = 40;
      cfg.pin_d2     = 41;
      cfg.pin_d3     = 42;
      cfg.pin_d4     = 45;
      cfg.pin_d5     = 46;
      cfg.pin_d6     = 47;
      cfg.pin_d7     = 48;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs          = 6;
      cfg.pin_rst         = 5;
      cfg.pin_busy        = -1;
      // This 1.91" panel is a 240x320 ST7789 die cropped to 170x320.
      // The visible 170 columns start at x=35; without this offset the
      // framebuffer is shifted and text renders as garbled pixels.
      cfg.panel_width     = 170;
      cfg.panel_height    = 320;
      cfg.offset_x        = 35;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable        = true;
      cfg.invert          = true;    // LilyGo official: TFT_INVERSION_ON
      cfg.rgb_order       = false;   // official: TFT_RGB_ORDER TFT_RGB
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl     = 38;
      cfg.invert     = false;
      cfg.freq       = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
