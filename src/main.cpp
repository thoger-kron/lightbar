#include <Arduino.h>
#include "Adafruit_NeoPixel.h"
#include "EEPROM.h"
// bruger WS2815B

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
// defines

#define pixelCount 96 // hvor mange led jeg har
#define EEPROMADDR 0

// pins
namespace pins
{
  u8 potmeter = A4;
  u8 ledBut = 6;
  u8 but = 7;
  u8 rotDT = 5;
  u8 rotCLK = 2;
  u8 rotSW = 3;
  u8 pxpin = 8;
};

Adafruit_NeoPixel strip(pixelCount, pins::pxpin, NEO_GRB + NEO_KHZ800);

struct SaveData
{
  u16 brightness = 100; // 0 - 255
  u8 red = 200;         // 0-255
  u8 blue = 70;         // 0-255
  u8 green = 100;       // 0-255
  u8 currentColor = 0;  // 0 = red 1 = green 2 = blue
};
SaveData saveData;

struct Colors
{
  u8 r;
  u8 g;
  u8 b;
};

const Colors presets[] = {
    // Varme farver
    {255, 147, 41},  // Warm White (stearinlys)
    {255, 180, 100}, // Soft Amber
    {255, 120, 30},  // Deep Orange (hygge)
    {255, 60, 0},    // Campfire Orange

    // Hvide nuancer
    {255, 255, 255}, // Pure White
    {255, 244, 229}, // Warm White
    {201, 226, 255}, // Cool White (dagslys)

    // Farverige
    {255, 0, 0},   // Rød
    {0, 255, 0},   // Grøn
    {0, 0, 255},   // Blå
    {255, 0, 255}, // Magenta/Pink
    {0, 255, 255}, // Cyan
    {255, 255, 0}, // Gul

    // Stemningsfulde
    {138, 43, 226},  // Lilla (BlueViolet)
    {255, 20, 147},  // Hot Pink
    {255, 105, 180}, // Pink
    {64, 224, 208},  // Turkis
    {0, 255, 127},   // Spring Green

    // Naturlige
    {255, 140, 0},  // Orange (solnedgang)
    {30, 144, 255}, // Blå (himmellys)
    {50, 205, 50},  // Lime Green
    {255, 69, 0},   // Red-Orange

    // Dæmpede/rolige
    {100, 100, 150}, // Soft Blue
    {150, 100, 100}, // Soft Red
    {100, 150, 100}, // Soft Green
    {80, 50, 120},   // Deep Purple (nattelys)
};

// to get avg of brightness and cus im lazy
u8 values[5];

// varables
bool on = false;
bool lastOn = false;
u32 lastChange = 0;
u32 lastRotRotation = 0;
u8 presetNumber = 0;
u32 lastColorChange = 0;
bool normalColor = false;
u8 lastBrightness = 0;
// rotery.
volatile bool rotButIsPressed = false;
volatile int rotVal = 0;
volatile bool colorChange = false;

// functions
void lysStartup(u16 vent);
void lysSlut(u16 vent);
void rotISR();
void updateInputs();
void rotSWISR();
void showBrightness();
void printData();
void handleColorChange();
void handlePowerToggle();
void saveEEPROM();
void loadEEPROM();
void loadPreset(u8 num);
void showCurrentEditColor();
Colors getCurrentColor();
void updateShowCurrentEditColor();
void updateAvgBrightness(u16 potVal);

void setup()
{
  // put your setup code here, to run once:
  pinMode(pins::but, INPUT_PULLUP);
  pinMode(pins::ledBut, OUTPUT);
  pinMode(pins::rotCLK, INPUT_PULLUP);
  pinMode(pins::rotDT, INPUT_PULLUP);
  pinMode(pins::rotSW, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(pins::rotCLK), rotISR, CHANGE);   // set the interrupt
  attachInterrupt(digitalPinToInterrupt(pins::rotSW), rotSWISR, FALLING); // set the rotery button interrupt

  updateInputs();

  strip.begin();
  strip.show(); // sluk alt
  loadEEPROM();
  memset(values, saveData.brightness, sizeof(values));
}

void loop()
{
  updateInputs();

  // Handle power toggle
  if (lastOn != on)
  {
    handlePowerToggle();
    return; // Exit early - lad fade-animationen køre færdig
  }

  // Hvis lyset er slukket, skip resten
  if (!on)
    return;

  // Handle rotary encoder ændringer
  if (colorChange)
  {
    handleColorChange();
  }

  updateShowCurrentEditColor();

  // Handle brightness changes
  if (lastBrightness != saveData.brightness)
  {
    showBrightness();
  }
  lastBrightness = saveData.brightness;
}

void handlePowerToggle()
{
  digitalWrite(pins::ledBut, on);
  lastOn = on;

  if (on)
  {
    lysStartup(5);
  }
  else
  {
    lysSlut(5);
    saveEEPROM();
  }
}

void handleColorChange()
{
  // Opdater den korrekte farvekanal
  switch (saveData.currentColor)
  {
  case 0:
    saveData.red = rotVal;
    break;
  case 1:
    saveData.green = rotVal;
    break;
  case 2:
    saveData.blue = rotVal;
    break;
  }

  // Opdater strip med den nye farve
  strip.fill(strip.Color(saveData.red, saveData.green, saveData.blue));
  showCurrentEditColor();
  strip.show();

  // Reset state
  colorChange = false;
  lastChange = millis();
  normalColor = false;

  Serial.print("Color: R");
  Serial.print(saveData.red);
  Serial.print(" G");
  Serial.print(saveData.green);
  Serial.print(" B");
  Serial.println(saveData.blue);
  Serial.print("brightness : ");
  Serial.println(saveData.brightness);
  lastColorChange = millis();
}

void updateInputs()
{
  static u32 startPress = 0;
  static u32 lastBlinktime = 0;
  static bool beforePressed = false;

  if (digitalRead(pins::but) == 0)
  { // hvis low / trykket
    beforePressed = true;

    if (startPress == 0)
    {
      startPress = millis();
    }

    u32 totalPressTime = millis() - startPress;

    if (totalPressTime > 500 && on) // Holdt i mere end 500ms?
    {
      // Skift preset hver 500ms
      if (millis() - lastBlinktime > 500)
      {
        // BLINK når vi tæller op
        digitalWrite(pins::ledBut, HIGH);
        delay(100);
        digitalWrite(pins::ledBut, LOW);

        Serial.print("blink... preset: ");
        Serial.println(presetNumber);

        lastBlinktime = millis();
        presetNumber++;
        beforePressed = true;
      }
      else
      {
        // Vent - hold LED slukket
        digitalWrite(pins::ledBut, LOW);
      }
    }
    else
    {
      // Holdes nede, men ikke længe nok endnu
      digitalWrite(pins::ledBut, LOW);
    }
  }
  else
  {
    if (beforePressed == true)
    {
      u32 totalPressTime = millis() - startPress;

      if (totalPressTime < 500)
      {
        // Så var det bare et kort tryk
        on = !on;
        Serial.println("on skift");
      }
      else
      {
        loadPreset(presetNumber);
        Serial.print("loaded preset: ");
        Serial.println(presetNumber);
      }

      // Reset alt
      presetNumber = 0;
      lastBlinktime = 0;
      beforePressed = false;
      startPress = 0;
      digitalWrite(pins::ledBut, on ? HIGH : LOW);
    }
  }

  // Potentiometer
  updateAvgBrightness(analogRead(pins::potmeter));
}

// fader op
void lysStartup(u16 vent)
{
  Serial.println("lys startop--------------------------------------------");

  for (int i = 0; i <= saveData.brightness; i++)
  {
    strip.fill(strip.Color(saveData.red, saveData.green, saveData.blue));
    strip.setBrightness(i);
    strip.show();
    delay(vent);
  }
}

// slutter lyset ved at fade ned
void lysSlut(u16 vent)
{
  Serial.println("slutter lys -----------------------------");
  for (int i = saveData.brightness; i >= 0; i--)
  {
    strip.fill(strip.Color(saveData.red, saveData.green, saveData.blue));
    strip.setBrightness(i);
    strip.show();
    delay(vent);
  }
}

void rotISR()
{
  // Debounce check - ignorer hvis for hurtigt efter sidste interrupt
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime < 5)
  { // 5ms debounce
    return;
  }

  // Læs pins EN gang for at undgå race conditions
  bool clkState = digitalRead(pins::rotCLK);
  bool dtState = digitalRead(pins::rotDT);

  // Beregn om det er hurtig rotation (mellem 5-100ms = hurtig)
  unsigned long timeSinceLastRotation = interruptTime - lastRotRotation;
  bool shouldMore = (timeSinceLastRotation > 5 && timeSinceLastRotation < 100);

  // Bestem retning
  if (clkState == dtState)
  {
    rotVal++;
    if (shouldMore)
      rotVal += 5; // Tilføj ekstra hvis hurtig rotation
  }
  else
  {
    rotVal--;
    if (shouldMore)
      rotVal -= 5;
  }

  // Begræns mellem 0-255
  rotVal = constrain(rotVal, 0, 255);

  // Opdater flags
  colorChange = true;
  lastRotRotation = interruptTime;
  lastInterruptTime = interruptTime;
}

void rotSWISR()
{
  saveData.currentColor = (saveData.currentColor + 1) % 3;
  rotVal = saveData.currentColor;

  switch (saveData.currentColor)
  {
  case 0:
    rotVal = saveData.red;
    break;
  case 1:
    rotVal = saveData.green;
    break;
  case 2:
    rotVal = saveData.blue;
    break;
  }
}

void showBrightness()
{
  strip.fill(strip.Color(saveData.red, saveData.green, saveData.blue));
  updateShowCurrentEditColor();
  strip.setBrightness(saveData.brightness);
  strip.show();
}

void printData()
{
  Serial.print("R: ");
  Serial.println(saveData.red);
  Serial.print("G: ");
  Serial.println(saveData.green);
  Serial.print("B: ");
  Serial.println(saveData.blue);
  Serial.println();
  Serial.print("Rot value: ");
  Serial.println(rotVal);
  Serial.print("current color: ");
  Serial.println(saveData.currentColor);
  Serial.print("current brightness: ");
  Serial.println(saveData.brightness);
}

void saveEEPROM()
{
  EEPROM.put(EEPROMADDR, saveData);
}

void loadEEPROM()
{
  EEPROM.get(EEPROMADDR, saveData);
}

void loadPreset(u8 num)
{
  saveData.red = presets[num].r;
  saveData.green = presets[num].g;
  saveData.blue = presets[num].b;
  strip.fill(presets[num].r, presets[num].g, presets[num].b);
  while (!strip.canShow())
  {
  }
  strip.show();
  showBrightness(); // virker tror jeg ?
}

void updateShowCurrentEditColor()
{
  static bool haveReset = false;
  if (millis() - lastColorChange < 2000)
  { // hvis det er mindre end 3 sek siden man ændre farve
    showCurrentEditColor();
    haveReset = false;
  }
  else if (!haveReset)
  {
    // så skal den bare reset
    strip.fill(saveData.red, saveData.green, saveData.blue);
    strip.show();
    haveReset = true;
  }
}

void showCurrentEditColor()
{
  strip.setPixelColor(0, strip.Color(getCurrentColor().r, getCurrentColor().g, getCurrentColor().b));
  strip.show();
}

Colors getCurrentColor()
{
  Colors color;
  color.r = 0;
  color.g = 0;
  color.b = 0;
  switch (saveData.currentColor)
  {
  case 0:
    color.r = 255;
    break;
  case 1:
    color.g = 255;
    break;
  case 2:
    color.b = 255;
    break;
  }
  return color;
}

void updateAvgBrightness(u16 potVal)
{
  u16 VALUES_LEN = sizeof(values) / sizeof(values[0]);
  // shift
  for (u8 i = 0; i < VALUES_LEN - 1; i++)
  {
    values[i] = values[i + 1];
  }

  // map korrekt
  u16 mapped = map(constrain(potVal, 0, 900), 0, 900, 0, 255);
  values[VALUES_LEN - 1] = mapped;

  // average
  u32 total = 0;
  for (u8 i = 0; i < VALUES_LEN; i++)
  {
    total += values[i];
  }

  saveData.brightness = total / VALUES_LEN;
}