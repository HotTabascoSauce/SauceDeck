#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>

// Assumed ESP32-S3-DevKitC-1 wiring. Keep this block aligned with the PCB.
constexpr uint8_t keyPins[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr uint8_t encoderPins[][3] = {
  {16, 17, 18}, {21, 35, 36}, {37, 38, 39}, {40, 41, 42}
};
constexpr uint8_t displaySck = 43;
constexpr uint8_t displayMosi = 44;
constexpr uint8_t displayCs = 1;
constexpr uint8_t displayDc = 2;
constexpr uint8_t displayRst = 3;

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
};

KeyState keys[12];
EncoderState encoders[4];
int8_t encoderDelta[4] = {};
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
  for (uint8_t index = 0; index < 4; ++index) {
    uint8_t a = digitalRead(encoderPins[index][0]);
    uint8_t b = digitalRead(encoderPins[index][1]);
    uint8_t currentAB = (a << 1) | b;
    uint8_t transition = (encoders[index].lastAB << 2) | currentAB;
    int8_t direction = transitionTable[transition];
    encoders[index].lastAB = currentAB;
    encoderDelta[index] += direction;
    if (encoderDelta[index] >= 4) {
      consumer.write(CONSUMER_VOLUME_INCREMENT);
      encoderDelta[index] = 0;
    } else if (encoderDelta[index] <= -4) {
      consumer.write(CONSUMER_VOLUME_DECREMENT);
      encoderDelta[index] = 0;
    }
    if (debouncedPressed(encoders[index].button,
                         digitalRead(encoderPins[index][2]))) {
      encoders[index].muted = !encoders[index].muted;
      consumer.write(CONSUMER_MUTE);
    }
  }
}

void refreshScreen() {
  if (millis() - lastScreenRefresh < screenRefreshMs) return;
  lastScreenRefresh = millis();
  screen.fillScreen(ST77XX_BLACK);
  screen.setTextColor(ST77XX_CYAN);
  screen.setTextSize(2);
  screen.setCursor(8, 8);
  screen.print("STREAM DECK");
  screen.setTextColor(ST77XX_WHITE);
  screen.setTextSize(1);
  screen.setCursor(8, 38);
  screen.print("12 MACROS   4 VOLUME CHANNELS");
  for (uint8_t index = 0; index < 4; ++index) {
    uint16_t x = 8 + (index % 2) * 116;
    uint16_t y = 70 + (index / 2) * 70;
    screen.drawRect(x, y, 104, 52, ST77XX_BLUE);
    screen.setCursor(x + 8, y + 8);
    screen.print("ENC ");
    screen.print(index + 1);
    screen.setCursor(x + 8, y + 28);
    screen.print(encoders[index].muted ? "MUTED" : "ACTIVE");
  }
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
