#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <SPI.h>

// Assumed ESP32-S3-DevKitC-1 wiring. Keep this block aligned with the PCB.
constexpr uint8_t keyPins[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr uint8_t encoderPins[][3] = {
  {16, 17, 18}, {37, 36, 35}
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
constexpr uint32_t volumeScreenDurationMs = 5000;

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
uint8_t lastSpeakerLevel = 255;
uint8_t lastMicLevel = 255;
bool lastSpeakerMuted = true;
bool lastMicMuted = true;
uint32_t volumeScreenUntil = 0;
bool lastRenderedVolumeScreen = false;

void sendShortcut(uint8_t modifier, uint8_t key) {
  keyboard.press(modifier);
  keyboard.press(key);
  delay(12);
  keyboard.releaseAll();
}

void openRunCommand(const char *command) {
  sendShortcut(KEY_LEFT_GUI, 'r');
  delay(300);
  keyboard.print(command);
  delay(100);
  keyboard.press(KEY_RETURN);
  delay(20);
  keyboard.releaseAll();
}



void openChromeURL(const char *url) {
  String command = String("chrome.exe ") + url;
  openRunCommand(command.c_str());
}

void showVolumeScreen() {
  volumeScreenUntil = millis() + volumeScreenDurationMs;
}

void runMacro(uint8_t index) {
  // Replace entries here with the shortcuts used by the host OS or launcher.
  switch (index) {
    case 0: openRunCommand("calc"); break;
    case 1:  openRunCommand("steam://"); break;
    case 2: openRunCommand("code"); break;
    case 3: openRunCommand("C:\\Program Files\\Bambu Studio\\bambu-studio.exe"); break;
    case 4: openChromeURL("https://www.linkedin.com"); break;
    case 5: openRunCommand("ms-copilot:"); break;
    case 6: openRunCommand("%USERPROFILE%\\AppData\\Local\\Programs\\signal-desktop\\Signal.exe"); break;
    case 7: openRunCommand("msteams:"); break;
    case 8: openChromeURL("https://www.office.com/launch/powerpoint"); break;
    case 9: openChromeURL("https://www.office.com/launch/word"); break;
    case 10: openChromeURL("https://www.office.com/launch/excel"); break;
    case 11: openChromeURL("https://outlook.office.com"); break;
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
      showVolumeScreen();
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
      showVolumeScreen();
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

constexpr uint16_t meterOutline = 0x0188;
constexpr uint16_t muteRed = 0xF800;

void drawMuteCross(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  for (int8_t offset = -3; offset <= 3; ++offset) {
    screen.drawLine(x + offset, y, x + width + offset, y + height, muteRed);
    screen.drawLine(x + width + offset, y, x + offset, y + height, muteRed);
  }
}

void drawMeter(uint16_t x, uint8_t level, uint16_t color) {
  constexpr uint16_t top = 15;
  constexpr uint16_t width = 26;
  constexpr uint16_t height = 104;

  screen.drawRect(x, top, width, height, meterOutline);
  uint16_t fillHeight = (height - 2) * level / 100;
  if (fillHeight > 0) {
    screen.fillRect(x + 1, top + height - 1 - fillHeight,
                    width - 2, fillHeight, color);
  }
}

void drawSpeakerIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  screen.fillRect(x, y + 20, 18, 28, color);
  screen.fillTriangle(x + 18, y + 20, x + 18, y + 48, x + 50, y + 4, color);
  screen.drawCircle(x + 38, y + 34, 19, color);
  screen.drawCircle(x + 38, y + 34, 12, ST77XX_BLACK);
  screen.drawCircle(x + 38, y + 34, 26, color);
  screen.drawCircle(x + 38, y + 34, 20, ST77XX_BLACK);
  if (muted) {
    drawMuteCross(x + 4, y - 2, 60, 60);
  }
}

void drawMicIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  screen.fillRoundRect(x + 12, y, 26, 58, 12, color);
  screen.fillRect(x + 2, y + 42, 10, 20, color);
  screen.fillRect(x + 38, y + 42, 10, 20, color);
  screen.fillRect(x + 2, y + 52, 46, 10, color);
  screen.fillRect(x + 22, y + 58, 6, 25, color);
  screen.fillRect(x + 8, y + 83, 34, 7, color);
  if (muted) {
    drawMuteCross(x - 2, y - 2, 54, 92);
  }
}

void drawBootMessage() {
  constexpr char message[] = "Hello World";
  int16_t textX;
  int16_t textY;
  uint16_t textWidth;
  uint16_t textHeight;

  screen.fillScreen(ST77XX_BLACK);
  screen.setTextColor(ST77XX_WHITE);
  screen.setTextSize(3);
  screen.setTextWrap(false);
  screen.getTextBounds(message, 0, 0, &textX, &textY, &textWidth, &textHeight);
  screen.setCursor((screen.width() - textWidth) / 2,
                   (screen.height() - textHeight) / 2);
  screen.print(message);
  delay(1500);
}

void refreshScreen() {
  uint8_t speakerLevel = encoders[0].level;
  uint8_t micLevel = encoders[1].level;
  bool speakerMuted = encoders[0].muted;
  bool micMuted = encoders[1].muted;
  uint32_t now = millis();
  bool showVolume = static_cast<int32_t>(volumeScreenUntil - now) > 0;

  if (!showVolume) {
    if (lastRenderedVolumeScreen) {
      screen.fillScreen(ST77XX_BLACK);
      lastRenderedVolumeScreen = false;
    }
    return;
  }

  if (lastRenderedVolumeScreen &&
      speakerLevel == lastSpeakerLevel && micLevel == lastMicLevel &&
      speakerMuted == lastSpeakerMuted && micMuted == lastMicMuted) {
    return;
  }

  lastSpeakerLevel = speakerLevel;
  lastMicLevel = micLevel;
  lastSpeakerMuted = speakerMuted;
  lastMicMuted = micMuted;
  lastRenderedVolumeScreen = true;

  screen.fillScreen(ST77XX_BLACK);

  drawMeter(74, speakerMuted ? 0 : speakerLevel, ST77XX_CYAN);
  drawMeter(218, micMuted ? 0 : micLevel, 0x87E0);
  drawSpeakerIcon(44, 146, ST77XX_WHITE, speakerMuted);
  drawMicIcon(208, 138, ST77XX_WHITE, micMuted);
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
  drawBootMessage();
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
