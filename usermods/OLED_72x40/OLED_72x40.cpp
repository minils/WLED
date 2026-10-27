#include "wled.h"
#include <U8g2lib.h>

/*
 * OLED_72x40 usermod
 *
 * Minimal status display for the tiny 0.42" SSD1306 OLED (72x40 pixels) found on
 * "ESP32-C3 0.42 OLED" boards. The panel sits at column offset 28 of the SSD1306's
 * 128 column GDDRAM, which is handled by U8g2's dedicated 72x40 device.
 *
 * Layout (72x40):
 *   effect name      (6x10 font, scrolls if too long)
 *   palette name     (5x7 font, scrolls if too long)
 *   brightness bar + percent
 *   speed / intensity
 *   IP address (or AP SSID)
 *
 * When the light is off, an optional clock is shown (requires NTP), otherwise the
 * display is switched off. After a configurable idle time the display goes to sleep
 * and wakes up again on the next state change.
 *
 * Uses the global I2C pins (Config > Usermods > global I2C GPIOs).
 */

#define OLED72_WIDTH      72
#define OLED72_HEIGHT     40
#define OLED72_REFRESH_MS 1000  // periodic refresh (IP/clock)
#define OLED72_SCROLL_MS  120   // scroll step interval
#define OLED72_MAX_CHARS  48

class Oled72x40Usermod : public Usermod {

  private:
    U8G2 *u8g2 = nullptr;

    // config
    bool    enabled       = true;
    bool    flip          = false;
    uint8_t contrast      = 128;
    uint8_t i2cAddress    = 0x3C;
    uint8_t sleepMinutes  = 5;     // 0 = never
    bool    clockWhenOff  = true;

    // state
    bool          initDone     = false;
    bool          displayOn    = true;
    unsigned long lastRedraw   = 0;
    unsigned long lastScroll   = 0;
    unsigned long lastActivity = 0;
    int16_t       scrollPos[2] = {0, 0};
    bool          needScroll[2] = {false, false};

    uint8_t knownBri       = 0;
    uint8_t knownMode      = 255;
    uint8_t knownPalette   = 255;
    uint8_t knownSpeed     = 0;
    uint8_t knownIntensity = 0;
    bool    knownOn        = false;

    char effectName[OLED72_MAX_CHARS]  = "";
    char paletteName[OLED72_MAX_CHARS] = "";

    static const char _name[];
    static const char _enabled[];

    void readNames() {
      extractModeName(effectCurrent, JSON_mode_names, effectName, OLED72_MAX_CHARS-1);
      // strip audio-reactive prefix (" ♪" / " ♫" = space + 3 byte UTF-8 + space)
      if (effectName[0] == ' ' && (uint8_t)effectName[1] > 127) memmove(effectName, effectName+5, strlen(effectName+5)+1);
      extractModeName(effectPalette, JSON_palette_names, paletteName, OLED72_MAX_CHARS-1);
      // strip "* " from dynamic palettes
      if (paletteName[0] == '*' && paletteName[1] == ' ') memmove(paletteName, paletteName+2, strlen(paletteName+2)+1);
      scrollPos[0] = scrollPos[1] = 0;
      u8g2->setFont(u8g2_font_6x10_tr);
      needScroll[0] = u8g2->getStrWidth(effectName) > OLED72_WIDTH;
      u8g2->setFont(u8g2_font_5x7_tr);
      needScroll[1] = u8g2->getStrWidth(paletteName) > OLED72_WIDTH;
    }

    // draws text, scrolling it horizontally if it doesn't fit (current font must be set)
    void drawScrolling(const char *text, int16_t y, uint8_t idx) {
      if (!needScroll[idx]) {
        u8g2->drawStr(0, y, text);
        return;
      }
      int16_t period = u8g2->getStrWidth(text) + 16;  // text width + gap
      int16_t x = -scrollPos[idx];
      u8g2->drawStr(x, y, text);
      u8g2->drawStr(x + period, y, text);  // wrap-around copy
      scrollPos[idx] = (scrollPos[idx] + 2) % period;
    }

    void drawStatus() {
      char buf[24];
      u8g2->clearBuffer();

      u8g2->setFont(u8g2_font_6x10_tr);
      drawScrolling(effectName, 8, 0);

      u8g2->setFont(u8g2_font_5x7_tr);
      drawScrolling(paletteName, 17, 1);

      // brightness bar
      unsigned pct = (bri * 100 + 127) / 255;
      u8g2->drawFrame(0, 20, 50, 7);
      u8g2->drawBox(1, 21, (48 * bri) / 255, 5);
      snprintf_P(buf, sizeof(buf), PSTR("%u%%"), pct);
      u8g2->drawStr(OLED72_WIDTH - u8g2->getStrWidth(buf), 27, buf);

      u8g2->setFont(u8g2_font_4x6_tr);
      snprintf_P(buf, sizeof(buf), PSTR("Sx%3u  Ix%3u"), effectSpeed, effectIntensity);
      u8g2->drawStr(0, 33, buf);

      if (apActive) snprintf_P(buf, sizeof(buf), PSTR("AP %s"), apSSID);
      else if (Network.isConnected()) {
        IPAddress ip = Network.localIP();
        snprintf_P(buf, sizeof(buf), PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
      } else strcpy_P(buf, PSTR("no network"));
      u8g2->drawStr(0, 39, buf);

      u8g2->sendBuffer();
      lastScroll = millis();
    }

    void drawOff() {
      char buf[8];
      u8g2->clearBuffer();
      if (clockWhenOff && ntpEnabled && localTime > 0) {
        uint8_t h = hour(localTime);
        if (useAMPM) { h %= 12; if (h == 0) h = 12; }
        snprintf_P(buf, sizeof(buf), PSTR("%02u:%02u"), h, minute(localTime));
        u8g2->setFont(u8g2_font_logisoso16_tn);
        u8g2->drawStr((OLED72_WIDTH - u8g2->getStrWidth(buf)) / 2, 28, buf);
      } else {
        u8g2->setFont(u8g2_font_6x10_tr);
        strcpy_P(buf, PSTR("OFF"));
        u8g2->drawStr((OLED72_WIDTH - u8g2->getStrWidth(buf)) / 2, 24, buf);
      }
      u8g2->sendBuffer();
    }

    void setDisplayOn(bool on) {
      if (on == displayOn) return;
      displayOn = on;
      u8g2->setPowerSave(!on);
    }

    void applySettings() {
      u8g2->setContrast(contrast);
      u8g2->setFlipMode(flip);
    }

    bool stateChanged() {
      bool on = bri > 0;
      if (on == knownOn && bri == knownBri && effectCurrent == knownMode && effectPalette == knownPalette &&
          effectSpeed == knownSpeed && effectIntensity == knownIntensity) return false;
      if (effectCurrent != knownMode || effectPalette != knownPalette) {
        knownMode    = effectCurrent;
        knownPalette = effectPalette;
        readNames();
      }
      knownOn        = on;
      knownBri       = bri;
      knownSpeed     = effectSpeed;
      knownIntensity = effectIntensity;
      return true;
    }

  public:

    void setup() override {
      if (i2c_scl < 0 || i2c_sda < 0) {
        DEBUG_PRINTLN(F("OLED72x40: global I2C pins not set, disabling."));
        enabled = false;
        return;
      }
      // U8g2 uses the global Wire instance which WLED already started on the global I2C pins
      u8g2 = new U8G2_SSD1306_72X40_ER_F_HW_I2C(U8G2_R0);
      if (!u8g2) { enabled = false; return; }
      u8g2->setI2CAddress(i2cAddress << 1);
      u8g2->setBusClock(400000);
      u8g2->begin();
      applySettings();
      u8g2->setPowerSave(!enabled);
      displayOn    = enabled;
      lastActivity = millis();
      initDone     = true;
    }

    void loop() override {
      if (!enabled || !initDone) return;
      unsigned long now = millis();
      if (strip.isUpdating() && now - lastRedraw < 200) return;  // don't starve LED output

      bool changed = stateChanged();
      if (changed) {
        lastActivity = now;
        setDisplayOn(true);
      }

      if (sleepMinutes && now - lastActivity > sleepMinutes * 60000UL) {
        // sleep, but keep the clock visible while the light is off
        if (!(knownOn == false && clockWhenOff && ntpEnabled)) { setDisplayOn(false); return; }
      }
      if (!displayOn) return;

      if (!knownOn) {
        if (!clockWhenOff) { setDisplayOn(false); return; }
        if (changed || now - lastRedraw > OLED72_REFRESH_MS) { drawOff(); lastRedraw = now; }
        return;
      }

      bool scrolling = needScroll[0] || needScroll[1];
      if (changed || now - lastRedraw > OLED72_REFRESH_MS || (scrolling && now - lastScroll > OLED72_SCROLL_MS)) {
        drawStatus();
        lastRedraw = now;
      }
    }

    void addToJsonInfo(JsonObject& root) override {
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");
      JsonArray arr = user.createNestedArray(FPSTR(_name));
      if (!enabled)       arr.add(F("disabled"));
      else if (!initDone) arr.add(F("no I2C pins"));
      else                arr.add(displayOn ? F("on") : F("sleeping"));
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)]  = enabled;
      top[F("flip")]        = flip;
      top[F("contrast")]    = contrast;
      top[F("i2cAddress")]  = i2cAddress;
      top[F("sleepMin")]    = sleepMinutes;
      top[F("clockOff")]    = clockWhenOff;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();

      bool    oldEnabled = enabled;
      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, true);
      configComplete &= getJsonValue(top[F("flip")],       flip, false);
      configComplete &= getJsonValue(top[F("contrast")],   contrast, (uint8_t)128);
      configComplete &= getJsonValue(top[F("i2cAddress")], i2cAddress, (uint8_t)0x3C);
      configComplete &= getJsonValue(top[F("sleepMin")],   sleepMinutes, (uint8_t)5);
      configComplete &= getJsonValue(top[F("clockOff")],   clockWhenOff, true);

      if (initDone) {
        u8g2->setI2CAddress(i2cAddress << 1);
        applySettings();
        if (oldEnabled != enabled) { displayOn = !enabled; setDisplayOn(enabled); }
        lastActivity = millis();
        knownMode = 255;  // force full redraw
      }
      return configComplete;
    }

    void appendConfigData(Print& s) override {
      s.print(F("addInfo('OLED_72x40:contrast',1,'0-255');"));
      s.print(F("addInfo('OLED_72x40:i2cAddress',1,'decimal, 60 = 0x3C');"));
      s.print(F("addInfo('OLED_72x40:sleepMin',1,'minutes, 0 = never');"));
      s.print(F("addInfo('OLED_72x40:clockOff',1,'show clock when light is off (NTP)');"));
      s.print(F("addInfo('OLED_72x40:enabled',1,'uses global I2C pins');"));
    }
};

const char Oled72x40Usermod::_name[]    PROGMEM = "OLED_72x40";
const char Oled72x40Usermod::_enabled[] PROGMEM = "enabled";

static Oled72x40Usermod oled72x40_usermod;
REGISTER_USERMOD(oled72x40_usermod);
