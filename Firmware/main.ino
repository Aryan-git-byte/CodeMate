/**
 * Codemate v1 - Macropad Firmware
 * Target: Seeed XIAO RP2040
 *
 * Hardware:
 *   - 3x3 key matrix (rows: D0/D1/D2, cols: D3/D6/D7), COL2ROW wiring
 *   - Rotary encoder (A: D8, B: D9) -> Volume up/down
 *   - 6x SK6812 MINI-E RGB LEDs chained on D10
 *   - SSD1306 OLED 128x32 via I2C @ 0x3C (optional)
 *
 * Key layout (coding-focused):
 *   [0] Comment (Ctrl+/)       [1] Uncomment (Ctrl+Shift+/) [2] Block Comment (Ctrl+Shift+A)
 *   [3] Run (F5)               [4] Debug (Shift+F5)          [5] Stop (Shift+F5)
 *   [6] git status + Enter     [7] git commit -m ""          [8] git push + Enter
 *
 * Encoder: CW = Vol+,  CCW = Vol-
 *
 * Board package: "Raspberry Pi Pico/RP2040" by Earle Philhower v5.5.0+
 *   Board:     Seeed XIAO RP2040
 *   USB Stack: Adafruit TinyUSB  <- must be set in Tools menu
 *
 * Libraries needed (Arduino Library Manager):
 *   - Adafruit NeoPixel
 *   - Adafruit TinyUSB Library   <- if not already present
 *   - Adafruit SSD1306 + Adafruit GFX Library  (only if using OLED)
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>

// ── Optional OLED ─────────────────────────────────────────────────────────────
#define USE_OLED 1   // set to 0 if OLED not soldered
#if USE_OLED
  #include <Wire.h>
  #include <Adafruit_SSD1306.h>
  #define OLED_ADDR 0x3C
  Adafruit_SSD1306 oled(128, 32, &Wire, -1);
  bool oledReady = false;
#endif

// ── USB HID — keyboard ────────────────────────────────────────────────────────
// Full keyboard HID descriptor
uint8_t const descKbd[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
Adafruit_USBD_HID usbKbd(descKbd, sizeof(descKbd), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

// ── USB HID — consumer (media keys) ──────────────────────────────────────────
uint8_t const descMedia[] = { TUD_HID_REPORT_DESC_CONSUMER() };
Adafruit_USBD_HID usbMedia(descMedia, sizeof(descMedia), HID_ITF_PROTOCOL_NONE, 2, false);

// ── Pins ──────────────────────────────────────────────────────────────────────
const uint8_t ROW_PINS[3] = { D0, D1, D2 };
const uint8_t COL_PINS[3] = { D3, D6, D7 };
const uint8_t ENC_A       = D8;
const uint8_t ENC_B       = D9;

// ── RGB ───────────────────────────────────────────────────────────────────────
#define LED_PIN   D10
#define LED_COUNT 6
// SK6812 MINI-E uses RGB color order (not GRB like WS2812B)
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

// ── Debounce ──────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS 12
bool     keyState[3][3]   = {};
bool     rawState[3][3]   = {};
uint32_t lastChange[3][3] = {};

// ── Encoder — quadrature state machine ───────────────────────────────────────
uint8_t encState = 0;
int8_t  encAccum = 0;
const int8_t ENC_TABLE[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

// ── RGB animation — breathing + keypress pulse ───────────────────────────────
float    breathPhase       = 0.0;      // breathing animation phase
int      activeLED         = -1;       // which LED to pulse (-1 = all breathe)
uint32_t ledPulseStart     = 0;        // when the LED pulse started
uint32_t lastFrameTime     = 0;        // last time we updated LEDs
#define  LED_PULSE_MS      400         // pulse duration (ms)
#define  FRAME_DELAY_MS    30          // ~33fps, prevents flickering
// Warm amber color (no blue - LED #0 has dead blue channel)
#define  BASE_R            255
#define  BASE_G            140
#define  BASE_B            0

// Map key positions to LED indices (customize based on your physical layout)
// Keys 0-8 correspond to your 3x3 grid, LEDs 0-5 are your strip
const int8_t KEY_TO_LED[9] = {
  0,  // Key 0 (Copy)       → LED 0
  1,  // Key 1 (Paste)      → LED 1
  2,  // Key 2 (Undo)       → LED 2
  2,  // Key 3 (Save)       → LED 2
  3,  // Key 4 (Play/Pause) → LED 3
  4,  // Key 5 (Mute)       → LED 4
  4,  // Key 6 (Next)       → LED 4
  5,  // Key 7 (Prev)       → LED 5
  5   // Key 8 (Screenshot) → LED 5
};

// ── OLED ──────────────────────────────────────────────────────────────────────
#if USE_OLED
const char* KEY_LABELS[9] = {
  "Comment", "Uncmmnt", "Block",
  "Run",     "Debug",   "Stop",
  "git st",  "commit",  "push"
};
bool     oledDirty      = false;
int      oledKeyIdx     = -1;
uint32_t oledKeyShownAt = 0;
#define  OLED_KEY_MS    1500

void drawOLED(int keyIdx) {
  if (!oledReady) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("CodeMate v1");
  if (keyIdx >= 0) {
    oled.setTextSize(2);
    oled.setCursor(0, 16);
    oled.print(KEY_LABELS[keyIdx]);
  } else {
    oled.setTextSize(1);
    oled.setCursor(0, 20);
    oled.print("ready");
  }
  oled.display();
}
#endif

// ── HID helpers ───────────────────────────────────────────────────────────────
void sendKey(uint8_t modifier, uint8_t keycode) {
  uint8_t keycodes[6] = { keycode, 0, 0, 0, 0, 0 };
  usbKbd.keyboardReport(0, modifier, keycodes);
  delay(15);
  uint8_t empty[6] = {};
  usbKbd.keyboardReport(0, 0, empty);
  delay(5);
}

void sendConsumer(uint16_t usage) {
  usbMedia.sendReport16(0, usage);
  delay(15);
  usbMedia.sendReport16(0, 0);
  delay(5);
}

void sendString(const char* str) {
  // Type a string character by character
  while (*str) {
    char c = *str++;
    uint8_t key = 0, mod = 0;
    
    // Map ASCII to HID keycodes
    if (c >= 'a' && c <= 'z') {
      key = HID_KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      key = HID_KEY_A + (c - 'A');
      mod = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (c >= '1' && c <= '9') {
      key = HID_KEY_1 + (c - '1');
    } else if (c == '0') {
      key = HID_KEY_0;
    } else {
      switch (c) {
        case ' ':  key = HID_KEY_SPACE;       break;
        case '-':  key = HID_KEY_MINUS;       break;
        case '"':  key = HID_KEY_APOSTROPHE;
                   mod = KEYBOARD_MODIFIER_LEFTSHIFT; break;
        default:   continue;
      }
    }
    sendKey(mod, key);
    delay(5);
  }
}

void ctrlKey(uint8_t keycode) {
  sendKey(KEYBOARD_MODIFIER_LEFTCTRL, keycode);
}

void ctrlShiftKey(uint8_t keycode) {
  sendKey(KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT, keycode);
}

void winShiftKey(uint8_t keycode) {
  sendKey(KEYBOARD_MODIFIER_LEFTGUI | KEYBOARD_MODIFIER_LEFTSHIFT, keycode);
}

// ── Key press dispatcher ──────────────────────────────────────────────────────
void handleKeyPress(uint8_t row, uint8_t col) {
  uint8_t idx = row * 3 + col;

  // Trigger LED pulse for this key's mapped LED
  activeLED      = KEY_TO_LED[idx];
  ledPulseStart  = millis();

#if USE_OLED
  oledKeyIdx     = idx;
  oledKeyShownAt = millis();
  oledDirty      = true;
  drawOLED(idx);
#endif

  switch (idx) {
    case 0: ctrlKey(HID_KEY_SLASH);                      break; // Toggle comment (Ctrl+/)
    case 1: ctrlShiftKey(HID_KEY_SLASH);                 break; // Uncomment (Ctrl+Shift+/)
    case 2: 
      // Block comment: Ctrl+Shift+A (VS Code) or Shift+Alt+A
      ctrlShiftKey(HID_KEY_A);
      break;
    case 3: sendKey(0, HID_KEY_F5);                      break; // Run (F5)
    case 4: sendKey(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_F5); break; // Debug (Shift+F5)
    case 5: sendKey(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_F5); break; // Stop (Shift+F5)
    case 6: 
      // git status: open terminal, wait, type command
      ctrlShiftKey(HID_KEY_GRAVE);  // Ctrl+Shift+` to toggle terminal
      delay(200);                    // wait for terminal to open
      sendString("git status");
      sendKey(0, HID_KEY_ENTER);
      break;
    case 7:
      // git commit: open terminal, wait, type command with cursor inside quotes
      ctrlShiftKey(HID_KEY_GRAVE);  // Ctrl+Shift+` to toggle terminal
      delay(200);
      sendString("git commit -m \"\"");
      sendKey(0, HID_KEY_ARROW_LEFT);
      break;
    case 8:
      // git push: open terminal, wait, type command
      ctrlShiftKey(HID_KEY_GRAVE);  // Ctrl+Shift+` to toggle terminal
      delay(200);
      sendString("git push");
      sendKey(0, HID_KEY_ENTER);
      break;
  }
}

// ── Encoder handler ───────────────────────────────────────────────────────────
void handleEncoder(int dir) {
  if (dir > 0) sendConsumer(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
  else         sendConsumer(HID_USAGE_CONSUMER_VOLUME_DECREMENT);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  // Matrix rows
  for (int r = 0; r < 3; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], HIGH);
  }
  // Matrix cols
  for (int c = 0; c < 3; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  encState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);

  // RGB — breathing + pulse effect
  strip.begin();
  strip.show();

  // USB HID — begin both interfaces
  usbKbd.begin();
  usbMedia.begin();

  // Wait for USB host to enumerate
  while (!TinyUSBDevice.mounted()) delay(1);
  delay(100); // brief settle after mount

#if USE_OLED
  Wire.begin();
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledReady) {
    oled.setRotation(2);   // flip 180 degrees — fixes upside-down mounting
    oled.clearDisplay();
    oled.display();
    drawOLED(-1);
  }
#endif
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── Matrix scan ───────────────────────────────────────────────────────────
  for (int r = 0; r < 3; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < 3; c++) {
      bool pressed = (digitalRead(COL_PINS[c]) == LOW);

      if (pressed != rawState[r][c]) {
        rawState[r][c]   = pressed;
        lastChange[r][c] = now;
      }
      if ((now - lastChange[r][c]) >= DEBOUNCE_MS && pressed != keyState[r][c]) {
        keyState[r][c] = pressed;
        if (pressed) handleKeyPress(r, c);
      }
    }

    digitalWrite(ROW_PINS[r], HIGH);
  }

  // ── Encoder ───────────────────────────────────────────────────────────────
  uint8_t cur = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  if (cur != encState) {
    uint8_t tableIdx = (encState << 2) | cur;
    encAccum += ENC_TABLE[tableIdx];
    encState  = cur;
    if (encAccum >= 4)  { encAccum = 0; handleEncoder( 1); }
    if (encAccum <= -4) { encAccum = 0; handleEncoder(-1); }
  }

  // ── OLED timeout ─────────────────────────────────────────────────────────
#if USE_OLED
  if (oledDirty && (now - oledKeyShownAt) >= OLED_KEY_MS) {
    oledDirty  = false;
    oledKeyIdx = -1;
    drawOLED(-1);
  }
#endif

  // ── RGB breathing + per-key pulse ────────────────────────────────────────
  // Frame rate limiting - only update every FRAME_DELAY_MS to prevent flicker
  if ((now - lastFrameTime) < FRAME_DELAY_MS) {
    return;  // skip this frame
  }
  lastFrameTime = now;
  
  // Update breathing phase
  breathPhase += 0.03;
  float breathLevel = (sin(breathPhase) + 1.0) / 2.0;  // 0.0 to 1.0
  float breathBrightness = 0.1 + (breathLevel * 0.3);  // 10% to 40%
  
  // Check if pulse is still active
  bool pulseActive = (activeLED >= 0) && ((now - ledPulseStart) < LED_PULSE_MS);
  
  // Update each LED
  for (int i = 0; i < LED_COUNT; i++) {
    float brightness;
    
    if (pulseActive && i == activeLED) {
      // This LED is pulsing - flash then fade back to breath
      uint32_t elapsed = now - ledPulseStart;
      float progress = (float)elapsed / LED_PULSE_MS;
      brightness = 1.0 - (progress * (1.0 - breathBrightness));  // fade from 100% to breath
    } else {
      // Normal breathing
      brightness = breathBrightness;
    }
    
    uint8_t r = BASE_R * brightness;
    uint8_t g = BASE_G * brightness;
    uint8_t b = BASE_B * brightness;
    
    strip.setPixelColor(i, strip.gamma32(strip.Color(r, g, b)));
  }
  
  strip.show();
  
  // Clear active LED when pulse completes
  if (!pulseActive && activeLED >= 0) {
    activeLED = -1;
  }
}
