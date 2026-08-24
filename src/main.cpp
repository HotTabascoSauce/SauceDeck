#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>

// Assumed ESP32-S3-DevKitC-1 wiring. Keep this block aligned with the PCB.
constexpr uint8_t keyPins[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr uint8_t encoderPins[][3] = {
  {16, 17, 18}, {21, 35, 36}
};
constexpr uint8_t displaySck = 43;
constexpr uint8_t displayMosi = 44;
constexpr uint8_t displayCs = 1;
constexpr uint8_t displayDc = 2;
constexpr uint8_t displayRst = 3;
// The attached wiring diagram shows the 240x320 ST7789 display following this order:
// VCC, GND, DIN, CLK, CS, DC, RST, BL. The corresponding ESP32-S3 SPI pins are:
// MOSI=44, SCK=43, CS=1, DC=2, RST=3. Backlight is not actively driven here.

constexpr uint32_t debounceMs = 25;
constexpr uint32_t screenRefreshMs = 250;

USBHIDKeyboard keyboard;
USBHIDConsumerControl consumer;
Adafruit_ST7789 screen(&SPI, displayCs, displayDc, displayRst);

struct KeyState {
  bool stable = HIGH;
  bool lastReading = HIGH;
  uint32_t changedAt = 0;
};

struct EncoderState {
  uint8_t lastAB = 0;
  KeyState button;
  bool muted = false;
  uint8_t level = 60;
};

KeyState keys[12];
EncoderState encoders[2];
int8_t encoderDelta[2] = {};
uint32_t lastScreenRefresh = 0;

void sendShortcut(uint8_t modifier, uint8_t key) {
  keyboard.press(modifier);
  keyboard.press(key);
  delay(12);
  keyboard.releaseAll();
}

void runMacro(uint8_t index) {
  // Replace entries here with the shortcuts used by the host OS or launcher.
  switch (index) {
    case 0: sendShortcut(KEY_LEFT_CTRL, 'c'); break;
    case 1: sendShortcut(KEY_LEFT_CTRL, 'v'); break;
    case 2: sendShortcut(KEY_LEFT_CTRL, 'z'); break;
    case 3: sendShortcut(KEY_LEFT_CTRL, 's'); break;
    case 4: sendShortcut(KEY_LEFT_ALT, KEY_TAB); break;
    case 5: sendShortcut(KEY_LEFT_GUI, 'e'); break;
    case 6: sendShortcut(KEY_LEFT_GUI, 'r'); break;
    case 7: sendShortcut(KEY_LEFT_GUI, 'l'); break;
    case 8: sendShortcut(KEY_LEFT_CTRL, 't'); break;
    case 9: sendShortcut(KEY_LEFT_CTRL, 'w'); break;
    case 10: sendShortcut(KEY_LEFT_GUI, 'd'); break;
    case 11: sendShortcut(KEY_LEFT_ALT, KEY_F4); break;
  }
}

bool debouncedPressed(KeyState &state, bool reading) {
  uint32_t now = millis();
  if (reading != state.lastReading) {
    state.lastReading = reading;
    state.changedAt = now;
  }
  if (now - state.changedAt >= debounceMs && reading != state.stable) {
    state.stable = reading;
    return reading == LOW;
  }
  return false;
}

void handleKeys() {
  for (uint8_t index = 0; index < 12; ++index) {
    if (debouncedPressed(keys[index], digitalRead(keyPins[index]))) {
      runMacro(index);
    }
  }
}

void handleEncoders() {
  static const int8_t transitionTable[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
  };

  for (uint8_t index = 0; index < 2; ++index) {
    uint8_t a = digitalRead(encoderPins[index][0]);
    uint8_t b = digitalRead(encoderPins[index][1]);
    uint8_t currentAB = (a << 1) | b;
    uint8_t transition = (encoders[index].lastAB << 2) | currentAB;
    int8_t direction = transitionTable[transition];
    encoders[index].lastAB = currentAB;
    encoderDelta[index] += direction;

    if (encoderDelta[index] >= 4) {
      if (index == 0) {
        if (encoders[index].muted) {
          encoders[index].muted = false;
        }
        encoders[index].level = constrain(encoders[index].level + 5, 0, 100);
        consumer.press(CONSUMER_CONTROL_VOLUME_INCREMENT);
        consumer.release();
      } else {
        encoders[index].muted = false;
        encoders[index].level = constrain(encoders[index].level + 5, 0, 100);
      }
      encoderDelta[index] = 0;
    } else if (encoderDelta[index] <= -4) {
      if (index == 0) {
        encoders[index].level = constrain(encoders[index].level - 5, 0, 100);
        consumer.press(CONSUMER_CONTROL_VOLUME_DECREMENT);
        consumer.release();
      } else {
        encoders[index].muted = false;
        encoders[index].level = constrain(encoders[index].level - 5, 0, 100);
      }
      encoderDelta[index] = 0;
    }

    if (debouncedPressed(encoders[index].button,
                         digitalRead(encoderPins[index][2]))) {
      encoders[index].muted = !encoders[index].muted;
      if (encoders[index].muted) {
        encoders[index].level = 0;
      } else if (index == 0) {
        encoders[index].level = static_cast<uint8_t>(max((int)encoders[index].level, 20));
      }
      if (index == 0) {
        consumer.press(CONSUMER_CONTROL_MUTE);
        consumer.release();
      }
    }
  }
}

void drawSpeakerIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  screen.drawTriangle(x + 8, y + 2, x + 8, y + 24, x + 22, y + 13, color);
  screen.fillRect(x + 20, y + 7, 6, 14, color);
  screen.drawLine(x + 28, y + 7, x + 34, y + 2, color);
  screen.drawLine(x + 28, y + 20, x + 34, y + 25, color);
  if (muted) {
    screen.drawLine(x, y, x + 40, y + 28, ST77XX_RED);
    screen.drawLine(x + 40, y, x, y + 28, ST77XX_RED);
  }
}

void drawMicIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  screen.drawCircle(x + 18, y + 10, 10, color);
  screen.drawLine(x + 18, y + 18, x + 18, y + 28, color);
  screen.drawLine(x + 10, y + 24, x + 26, y + 24, color);
  screen.drawLine(x + 14, y + 28, x + 22, y + 28, color);
  if (muted) {
    screen.drawLine(x, y, x + 36, y + 30, ST77XX_RED);
    screen.drawLine(x + 36, y, x, y + 30, ST77XX_RED);
  }
}

void drawVolumeBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint8_t value, bool muted, uint16_t color) {
  screen.drawRoundRect(x, y, w, h, 8, ST77XX_WHITE);
  uint16_t fillHeight = map(value, 0, 100, 0, h - 12);
  uint16_t barY = y + h - 6 - fillHeight;
  if (!muted && value > 0) {
    screen.fillRoundRect(x + 6, barY, w - 12, fillHeight, 6, color);
  }
  if (muted) {
    screen.drawLine(x + 4, y + 4, x + w - 4, y + h - 4, ST77XX_RED);
    screen.drawLine(x + w - 4, y + 4, x + 4, y + h - 4, ST77XX_RED);
  }
}

void refreshScreen() {
  if (millis() - lastScreenRefresh < screenRefreshMs) return;
  lastScreenRefresh = millis();

  screen.fillScreen(ST77XX_BLACK);
  screen.setTextColor(ST77XX_CYAN);
  screen.setTextSize(2);
  screen.setCursor(58, 8);
  screen.print("AUDIO");

  drawVolumeBar(28, 56, 54, 190, encoders[0].level, encoders[0].muted, ST77XX_GREEN);
  drawVolumeBar(158, 56, 54, 190, encoders[1].level, encoders[1].muted, ST77XX_BLUE);

  drawSpeakerIcon(26, 256, ST77XX_WHITE, encoders[0].muted);
  drawMicIcon(156, 256, ST77XX_WHITE, encoders[1].muted);

  screen.setTextColor(ST77XX_WHITE);
  screen.setTextSize(1);
  screen.setCursor(28, 248);
  screen.print("SPKR");
  screen.setCursor(162, 248);
  screen.print("MIC");
}

void setup() {
  for (uint8_t pin : keyPins) pinMode(pin, INPUT_PULLUP);
  for (auto &encoder : encoderPins) {
    pinMode(encoder[0], INPUT_PULLUP);
    pinMode(encoder[1], INPUT_PULLUP);
    pinMode(encoder[2], INPUT_PULLUP);
  }
  SPI.begin(displaySck, -1, displayMosi, displayCs);
  screen.init(240, 320);
  screen.setRotation(1);
  keyboard.begin();
  consumer.begin();
  USB.begin();
  refreshScreen();
}

void loop() {
  handleKeys();
  handleEncoders();
  refreshScreen();
  delay(1);
}
