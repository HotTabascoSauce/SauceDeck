#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <SPI.h>

// Assumed ESP32-S3-DevKitC-1 wiring. Keep this block aligned with the PCB.
constexpr uint8_t keyPins[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 38, 14, 15};
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

USBHIDKeyboard keyboard;
USBHIDConsumerControl consumer;
Adafruit_ST7789 screen(&SPI, displayCs, displayDc, displayRst);

extern "C" {
  extern const uint8_t Mic_map[];
  extern const uint8_t MicMute_map[];
  extern const uint8_t Speaker_map[];
  extern const uint8_t SpeakerMute_map[];
}

struct KeyState {
  bool stable = HIGH;
  bool lastReading = HIGH;
  uint32_t changedAt = 0;
};

struct EncoderState {
  uint8_t lastAB = 0;
  KeyState button;
  bool muted = false;
  uint16_t level = 20;
};

KeyState keys[12];
EncoderState encoders[2];
int8_t encoderDelta[2] = {};
uint16_t lastSpeakerLevel = UINT16_MAX;
uint16_t lastMicLevel = UINT16_MAX;
bool lastSpeakerMuted = true;
bool lastMicMuted = true;
bool lastRenderedVolumeScreen = false;
bool bothEncoderButtonsHandled = false;

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
  bool buttonPressed[2] = {};

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
        encoders[index].level = constrain(encoders[index].level + 4, 0, 200);
        consumer.press(CONSUMER_CONTROL_VOLUME_INCREMENT);
        consumer.release();
      } else {
        encoders[index].muted = false;
        encoders[index].level = constrain(encoders[index].level + 4, 0, 200);
      }
      encoderDelta[index] = 0;
    } else if (encoderDelta[index] <= -4) {
      if (index == 0) {
        encoders[index].level = constrain(encoders[index].level - 4, 0, 200);
        consumer.press(CONSUMER_CONTROL_VOLUME_DECREMENT);
        consumer.release();
      } else {
        encoders[index].muted = false;
        encoders[index].level = constrain(encoders[index].level - 4, 0, 200);
      }
      encoderDelta[index] = 0;
    }

    buttonPressed[index] = debouncedPressed(
        encoders[index].button, digitalRead(encoderPins[index][2]));
  }

  bool bothButtonsHeld = encoders[0].button.stable == LOW &&
                         encoders[1].button.stable == LOW;
  if (!bothButtonsHeld) {
    bothEncoderButtonsHandled = false;
  } else if (!bothEncoderButtonsHandled) {
    encoders[0].level = 0;
    encoders[1].level = 0;
    bothEncoderButtonsHandled = true;
    return;
  }

  for (uint8_t index = 0; index < 2; ++index) {
    bool otherButtonIsDown = digitalRead(encoderPins[index == 0 ? 1 : 0][2]) == LOW;
    if (buttonPressed[index] && !otherButtonIsDown) {
      encoders[index].muted = !encoders[index].muted;
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
  level = constrain(level, 0, 100);

  screen.drawRect(x, top, width, height, meterOutline);
  uint16_t fillHeight = (height - 2) * level / 100;
  if (fillHeight > 0) {
    screen.fillRect(x + 1, top + height - 1 - fillHeight,
                    width - 2, fillHeight, color);
  }

  char levelText[4];
  snprintf(levelText, sizeof(levelText), "%u", level);
  screen.setTextColor(ST77XX_WHITE);
  screen.setTextSize(2);
  screen.setTextWrap(false);
  int16_t textX;
  int16_t textY;
  uint16_t textWidth;
  uint16_t textHeight;
  screen.getTextBounds(levelText, 0, 0, &textX, &textY,
                       &textWidth, &textHeight);
  screen.setCursor(x + (width - textWidth) / 2, top + height + 2);
  screen.print(levelText);
}



const uint8_t speakerRgb565[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 
	0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xfc, 0xfc, 0xfe, 0xff, 0xff, 
	0xff, 0xff, 0x00, 0x00, 0xf0, 0x00, 0x0c, 0xf8, 0x03, 0x0e, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 
	0x3f, 0x7f, 0x00, 0x00, 0x01, 0x00, 0x06, 0x03, 0x18, 0x0e, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t microphoneRgb565[] PROGMEM = {
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,
  0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};

void drawRgb565Icon(const uint8_t *bitmap, uint16_t width, uint16_t height,
                    uint16_t x, uint16_t y) {
  for (uint16_t row = 0; row < height; ++row) {
    for (uint16_t column = 0; column < width; ++column) {
      uint16_t offset = (row * width + column) * 2;
      uint16_t pixel = pgm_read_byte(&bitmap[offset]) |
               (pgm_read_byte(&bitmap[offset + 1]) << 8);
      if (pixel != ST77XX_BLACK) {
        screen.drawPixel(x + column, y + row, pixel);
      }
    }
  }
}

void drawSpeakerIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  drawRgb565Icon(muted ? SpeakerMute_map : Speaker_map, 76, 76, x, y);
}

void drawMicIcon(uint16_t x, uint16_t y, uint16_t color, bool muted) {
  drawRgb565Icon(muted ? MicMute_map : Mic_map, muted ? 85 : 69, 85, x, y);
}

void drawBootMessage() {
  constexpr char topLine[] = "SauceDeck V1.1";
  constexpr char bottomLine[] = "by HotTabascoSauce";
  int16_t textX;
  int16_t textY;
  uint16_t topWidth;
  uint16_t topHeight;
  uint16_t bottomWidth;
  uint16_t bottomHeight;

  screen.fillScreen(ST77XX_BLACK);
  screen.setTextColor(ST77XX_WHITE);
  screen.setTextSize(2);
  screen.setTextWrap(false);
  screen.getTextBounds(topLine, 0, 0, &textX, &textY, &topWidth, &topHeight);
  screen.getTextBounds(bottomLine, 0, 0, &textX, &textY,
                       &bottomWidth, &bottomHeight);
  int16_t firstLineY = (screen.height() - topHeight - bottomHeight) / 2;
  screen.setCursor((screen.width() - topWidth) / 2, firstLineY);
  screen.print(topLine);
  screen.setCursor((screen.width() - bottomWidth) / 2,
                   firstLineY + topHeight + 4);
  screen.print(bottomLine);
  delay(5000);
}

void refreshScreen() {
  uint8_t speakerLevel = static_cast<uint8_t>((encoders[0].level + 1) / 2);
  uint8_t micLevel = static_cast<uint8_t>((encoders[1].level + 1) / 2);
  bool speakerMuted = encoders[0].muted;
  bool micMuted = encoders[1].muted;

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

  drawMeter(72, speakerMuted ? 0 : speakerLevel, ST77XX_CYAN);
  drawMeter(230, micMuted ? 0 : micLevel, 0x87E0);
  drawSpeakerIcon(44, 146, ST77XX_WHITE, speakerMuted);
  drawMicIcon(208, 138, ST77XX_WHITE, micMuted);
}

void setup() {
  encoders[0].level = 20;
  encoders[1].level = 20;
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
