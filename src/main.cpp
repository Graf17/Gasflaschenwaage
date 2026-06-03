#include <Arduino.h>
#include <HX711.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 2
#endif

#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 3
#endif

#ifndef HX711_SCALE
#define HX711_SCALE 1.0f
#endif

#ifndef BOTTLE_EMPTY_KG
#define BOTTLE_EMPTY_KG 0.0f
#endif

#ifndef GAS_NOMINAL_KG
#define GAS_NOMINAL_KG 5.0f
#endif

#ifndef FEATHER_I2C_SDA_PIN
#define FEATHER_I2C_SDA_PIN 19
#endif

#ifndef FEATHER_I2C_SCL_PIN
#define FEATHER_I2C_SCL_PIN 18
#endif

#ifndef FEATHER_I2C_POWER_PIN
#define FEATHER_I2C_POWER_PIN 20
#endif

#ifndef BATTERY_UPDATE_MS
#define BATTERY_UPDATE_MS 10000UL
#endif

static HX711 scale;
static Adafruit_MAX17048 batteryGauge;
static float calibrationFactor = HX711_SCALE;
static unsigned long lastPrintMs = 0;
static unsigned long lastBatteryMs = 0;
static unsigned long lastHxRetryMs = 0;
static unsigned long lastHxWarnMs = 0;
static bool batteryReady = false;
static bool hx711Ready = false;
static int hxDoutPin = HX711_DOUT_PIN;
static int hxSckPin = HX711_SCK_PIN;
static bool hxPinsSwapped = false;
static int hxNotReadyStreak = 0;
static float batteryVoltage = NAN;
static float batteryPercent = NAN;

float clampf(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

String readLineFromSerial() {
  static String line;

  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      String out = line;
      line = "";
      out.trim();
      return out;
    }
    line += c;
  }

  return "";
}

void printHelp() {
  Serial.println();
  Serial.println("Befehle:");
  Serial.println("  help                -> Hilfe anzeigen");
  Serial.println("  tare                -> Nullpunkt setzen");
  Serial.println("  raw                 -> Rohwert einmal ausgeben");
  Serial.println("  batt                -> Batterie einmal ausgeben");
  Serial.println("  cal <kg>            -> Mit bekanntem Gewicht kalibrieren");
  Serial.println("  scale <faktor>      -> Kalibrierfaktor manuell setzen");
  Serial.println();
}

void updateBatteryIfNeeded(bool force) {
  unsigned long now = millis();
  if (!force && (now - lastBatteryMs) < BATTERY_UPDATE_MS) {
    return;
  }
  lastBatteryMs = now;

  if (!batteryReady) {
    batteryVoltage = NAN;
    batteryPercent = NAN;
    return;
  }

  batteryVoltage = batteryGauge.cellVoltage();
  batteryPercent = clampf(batteryGauge.cellPercent(), 0.0f, 100.0f);
}

bool tryInitHx711(int doutPin, int sckPin) {
  scale.begin(doutPin, sckPin);
  delay(20);

  if (!scale.wait_ready_timeout(500)) {
    return false;
  }

  calibrationFactor = HX711_SCALE;
  scale.set_scale(calibrationFactor);
  scale.tare(15);

  hxDoutPin = doutPin;
  hxSckPin = sckPin;
  hx711Ready = true;
  hxNotReadyStreak = 0;
  return true;
}

void tryRecoverHx711IfNeeded(unsigned long now) {
  if (hx711Ready) {
    return;
  }
  if (now - lastHxRetryMs < 3000UL) {
    return;
  }
  lastHxRetryMs = now;

  if (tryInitHx711(HX711_DOUT_PIN, HX711_SCK_PIN)) {
    hxPinsSwapped = false;
    Serial.printf("HX711 wieder erreichbar auf DOUT=%d, SCK=%d\n", hxDoutPin, hxSckPin);
    return;
  }

  if (tryInitHx711(HX711_SCK_PIN, HX711_DOUT_PIN)) {
    hxPinsSwapped = true;
    Serial.printf("HX711 erkannt mit vertauschten Pins: DOUT=%d, SCK=%d\n", hxDoutPin, hxSckPin);
    return;
  }
}

void handleCommand(const String &cmdLine) {
  if (cmdLine.length() == 0) {
    return;
  }

  if (cmdLine.equalsIgnoreCase("help")) {
    printHelp();
    return;
  }

  if (cmdLine.equalsIgnoreCase("tare")) {
    if (!hx711Ready) {
      Serial.println("HX711 nicht bereit. Tara nicht moeglich.");
      return;
    }
    if (!scale.wait_ready_timeout(400)) {
      Serial.println("HX711 antwortet gerade nicht. Tara spaeter erneut versuchen.");
      return;
    }
    Serial.println("Tariere... bitte Last entfernen.");
    scale.tare(15);
    Serial.println("Tara abgeschlossen.");
    return;
  }

  if (cmdLine.equalsIgnoreCase("raw")) {
    if (!hx711Ready) {
      Serial.println("HX711 nicht bereit. Rohwert nicht verfuegbar.");
      return;
    }
    if (!scale.wait_ready_timeout(400)) {
      Serial.println("HX711 antwortet gerade nicht. Rohwert spaeter erneut abfragen.");
      return;
    }
    long raw = scale.read_average(10);
    long value = scale.get_value(10);
    Serial.printf("raw_average=%ld, offset_korrigiert=%ld\n", raw, value);
    return;
  }

  if (cmdLine.equalsIgnoreCase("batt")) {
    updateBatteryIfNeeded(true);
    if (!batteryReady || isnan(batteryVoltage) || isnan(batteryPercent)) {
      Serial.println("Batterie: nicht verfuegbar (MAX17048 nicht erreichbar).\n");
      return;
    }
    Serial.printf("battery_v=%.2f, battery_pct=%.2f\n", batteryVoltage, batteryPercent);
    Serial.println("Hinweis: Bei USB-Strom kann der Akku geladen werden, dadurch steigen Werte waehrend des Ladens.");
    return;
  }

  if (cmdLine.startsWith("cal ")) {
    if (!hx711Ready) {
      Serial.println("HX711 nicht bereit. Kalibrierung nicht moeglich.");
      return;
    }
    if (!scale.wait_ready_timeout(500)) {
      Serial.println("HX711 antwortet gerade nicht. Kalibrierung spaeter erneut versuchen.");
      return;
    }
    float knownKg = cmdLine.substring(4).toFloat();
    if (knownKg <= 0.0f) {
      Serial.println("Fehler: Bitte positives Gewicht in kg angeben, z.B. 'cal 5.0'.");
      return;
    }

    long value = scale.get_value(20);
    float newFactor = static_cast<float>(value) / knownKg;

    if (fabsf(newFactor) < 0.000001f) {
      Serial.println("Fehler: Kalibrierfaktor zu klein.");
      return;
    }

    calibrationFactor = newFactor;
    scale.set_scale(calibrationFactor);

    Serial.printf("Neuer Kalibrierfaktor: %.6f\n", calibrationFactor);
    Serial.println("Hinweis: Trage den Wert als HX711_SCALE in platformio.ini ein.");
    return;
  }

  if (cmdLine.startsWith("scale ")) {
    float factor = cmdLine.substring(6).toFloat();
    if (fabsf(factor) < 0.000001f) {
      Serial.println("Fehler: Ungueltiger Faktor.");
      return;
    }

    calibrationFactor = factor;
    scale.set_scale(calibrationFactor);
    Serial.printf("Kalibrierfaktor gesetzt: %.6f\n", calibrationFactor);
    return;
  }

  Serial.printf("Unbekannter Befehl: %s\n", cmdLine.c_str());
  printHelp();
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println("Gasflaschenwaage - PlatformIO Test");
  Serial.printf("HX711 DOUT=%d, SCK=%d\n", HX711_DOUT_PIN, HX711_SCK_PIN);

  // Feather: GPIO20 schaltet die I2C/STEMMA-Versorgung.
  pinMode(FEATHER_I2C_POWER_PIN, OUTPUT);
  digitalWrite(FEATHER_I2C_POWER_PIN, HIGH);
  delay(20);

  Wire.begin(FEATHER_I2C_SDA_PIN, FEATHER_I2C_SCL_PIN);
  Wire.setClock(100000);

  batteryReady = batteryGauge.begin(&Wire);
  if (batteryReady) {
    updateBatteryIfNeeded(true);
    Serial.printf("MAX17048 erkannt: battery_v=%.2f, battery_pct=%.2f\n", batteryVoltage, batteryPercent);
  } else {
    Serial.println("WARNUNG: MAX17048 nicht erreichbar (Akkuwerte nicht verfuegbar).");
  }

  if (tryInitHx711(HX711_DOUT_PIN, HX711_SCK_PIN)) {
    hxPinsSwapped = false;
    Serial.printf("HX711 erkannt auf DOUT=%d, SCK=%d\n", hxDoutPin, hxSckPin);
  } else if (tryInitHx711(HX711_SCK_PIN, HX711_DOUT_PIN)) {
    hxPinsSwapped = true;
    Serial.printf("HX711 erkannt mit vertauschten Pins: DOUT=%d, SCK=%d\n", hxDoutPin, hxSckPin);
  } else {
    Serial.printf("WARNUNG: HX711 nicht erreichbar auf DOUT=%d, SCK=%d oder vertauscht.\n", HX711_DOUT_PIN, HX711_SCK_PIN);
    Serial.println("Tara beim Start uebersprungen, weil HX711 nicht bereit.");
  }

  Serial.printf("Start Kalibrierfaktor: %.6f\n", calibrationFactor);
  Serial.printf("Flasche leer (Tara): %.3f kg\n", BOTTLE_EMPTY_KG);
  Serial.printf("Gas Nennfuellung: %.3f kg\n", GAS_NOMINAL_KG);
  Serial.println("Jede Sekunde werden Rohwert, Gesamtgewicht, Gasgewicht, Fuellstand und Batterie ausgegeben.");
  Serial.println("Hinweis: USB versorgt das Board und kann den Akku gleichzeitig laden. Die Verbindung zum Akku bleibt bestehen.");
  if (hxPinsSwapped) {
    Serial.println("Hinweis: HX711-Pins sind im Betrieb vertauscht erkannt worden.");
  }
  printHelp();
}

void loop() {
  String cmd = readLineFromSerial();
  if (cmd.length() > 0) {
    handleCommand(cmd);
  }

  unsigned long now = millis();
  if (now - lastPrintMs >= 1000) {
    lastPrintMs = now;

    tryRecoverHx711IfNeeded(now);

    updateBatteryIfNeeded(false);

    if (!hx711Ready) {
      if (now - lastHxWarnMs >= 5000UL) {
        lastHxWarnMs = now;
        if (batteryReady && !isnan(batteryVoltage) && !isnan(batteryPercent)) {
          Serial.printf("HX711 nicht bereit (DOUT=%d, SCK=%d). battery_v=%.2f, battery_pct=%.2f\n",
                        hxDoutPin, hxSckPin, batteryVoltage, batteryPercent);
        } else {
          Serial.printf("HX711 nicht bereit (DOUT=%d, SCK=%d). battery=na\n", hxDoutPin, hxSckPin);
        }
      }
      return;
    }

    if (!scale.wait_ready_timeout(300)) {
      hxNotReadyStreak++;
      if (hxNotReadyStreak >= 5) {
        hx711Ready = false;
      }
      if (now - lastHxWarnMs >= 5000UL) {
        lastHxWarnMs = now;
        if (batteryReady && !isnan(batteryVoltage) && !isnan(batteryPercent)) {
          Serial.printf("HX711 timeout (%d/5) auf DOUT=%d, SCK=%d. battery_v=%.2f, battery_pct=%.2f\n",
                        hxNotReadyStreak, hxDoutPin, hxSckPin, batteryVoltage, batteryPercent);
        } else {
          Serial.printf("HX711 timeout (%d/5) auf DOUT=%d, SCK=%d. battery=na\n",
                        hxNotReadyStreak, hxDoutPin, hxSckPin);
        }
      }
      return;
    }

    hxNotReadyStreak = 0;

    long raw = scale.read_average(5);
    float totalKg = scale.get_units(5);
    float gasKg = totalKg - BOTTLE_EMPTY_KG;
    if (gasKg < 0.0f) {
      gasKg = 0.0f;
    }

    float fillPct = 0.0f;
    if (GAS_NOMINAL_KG > 0.0f) {
      fillPct = clampf((gasKg / GAS_NOMINAL_KG) * 100.0f, 0.0f, 100.0f);
    }

    if (batteryReady && !isnan(batteryVoltage) && !isnan(batteryPercent)) {
      Serial.printf("raw=%ld, total_kg=%.3f, gas_kg=%.3f, fill_pct=%.1f, battery_v=%.2f, battery_pct=%.2f\n",
                    raw, totalKg, gasKg, fillPct, batteryVoltage, batteryPercent);
    } else {
      Serial.printf("raw=%ld, total_kg=%.3f, gas_kg=%.3f, fill_pct=%.1f, battery=na\n", raw, totalKg, gasKg, fillPct);
    }
  }
}
