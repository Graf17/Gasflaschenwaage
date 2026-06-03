# Gasflaschenwaage - PlatformIO Test

Dieses Projekt ist ein reines Testprogramm fuer den HX711 am Adafruit ESP32-C6 Feather.
Ziel: Sensorik und Kalibrierung stabil zum Laufen bringen, bevor Home Assistant/ESPHome dazu kommt.

Zusatz: Das Testprogramm liest auch den internen Akku-Fuel-Gauge (MAX17048) des Feather aus.

## Verdrahtung

- HX711 `VCC` -> 3V
- HX711 `GND` -> GND
- HX711 `DOUT` -> TX (GPIO16)
- HX711 `SCK` -> RX (GPIO17)

Wichtig: Je nach Board-Revision und Aufbau musst du ggf. andere GPIOs verwenden.

## Build und Flash

1. In VS Code PlatformIO: `Build`
2. Danach `Upload`
3. `Monitor` mit 115200 Baud

Oder per CLI:

- `pio run`
- `pio run -t upload`
- `pio device monitor -b 115200`

## Serielle Befehle

- `help` Hilfe anzeigen
- `tare` Nullpunkt setzen (ohne Last)
- `raw` Rohwerte anzeigen
- `batt` Akkuwerte (Spannung und Prozent) anzeigen
- `cal 5.0` Kalibrierung mit bekanntem Gewicht in kg
- `scale 12345.6` Faktor direkt setzen

## Parameter in platformio.ini

Diese Werte kannst du direkt in `build_flags` pflegen, statt sie jedes Mal seriell einzugeben:

- `HX711_SCALE`: Kalibrierfaktor aus `cal <kg>`
- `BOTTLE_EMPTY_KG`: Leergewicht der Gasflasche (Tara der Flasche)
- `GAS_NOMINAL_KG`: Nenn-Fuellmenge in kg (z. B. 5 oder 11)
- `FEATHER_I2C_SDA_PIN`, `FEATHER_I2C_SCL_PIN`: I2C-Pins fuer den internen MAX17048
- `FEATHER_I2C_POWER_PIN`: GPIO zum Aktivieren der I2C/STEMMA-Power (Feather: GPIO20)
- `BATTERY_UPDATE_MS`: Intervall fuer Batterieabfrage

Die Laufzeit-Ausgabe enthaelt danach:

- `raw`: Rohzaehlwert des HX711
- `total_kg`: gemessenes Gesamtgewicht nach Auflagenplatten-Tara
- `gas_kg`: berechnetes Gasgewicht (`total_kg - BOTTLE_EMPTY_KG`)
- `fill_pct`: berechneter Fuellstand in Prozent
- `battery_v`: Akkuspannung aus MAX17048
- `battery_pct`: Akkustand aus MAX17048

Hinweis zu USB-Strom:

- Bei angeschlossenem USB wird der Akku geladen. Dadurch koennen `battery_v` und `battery_pct` steigen.
- Die Akku-Verbindung bleibt bestehen; der MAX17048 misst weiterhin die Zellwerte.

## Typischer Ablauf

1. Ohne Last starten und `tare` ausfuehren.
2. Bekanntes Gewicht auflegen (z. B. 5.0 kg).
3. `cal 5.0` senden.
4. Den ausgegebenen Kalibrierfaktor in `platformio.ini` bei `HX711_SCALE` eintragen.
5. `BOTTLE_EMPTY_KG` auf das Leergewicht deiner Flasche setzen.
6. `GAS_NOMINAL_KG` auf 5 oder 11 (je nach Flasche) setzen.
7. Neu bauen/flashen und Messwerte pruefen.

## ESPHome Uebergang

Im Ordner `esphome/` liegt eine startfertige Konfiguration:

- `esphome/gasflaschenwaage.yaml`

Vorgehen:

1. In `gasflaschenwaage.yaml` die Substitutions pruefen:
	- `bottle_empty_kg`
	- `gas_nominal_kg`
	- `calib_raw_no_load`, `calib_raw_known`
2. In ESPHome kompilieren/flashen.
3. In Home Assistant die Sensoren nutzen:
	- Gesamtgewicht
	- Gasgewicht
	- Fuellstand in %
	- Gas niedrig / kritisch

## ESPHome Zigbee (ESP32-C6)

Die Datei `esphome/gasflaschenwaage.yaml` ist auf Zigbee End Device ausgelegt.

Wichtige Punkte:

- Kein WiFi/API in der aktuellen Variante (Zigbee-only)
- Pairing ueber ZHA oder Zigbee2MQTT
- Bei Konfigurationsaenderungen in ESPHome: Geraet im Zigbee-Netz entfernen und neu anlernen
- In Zigbee2MQTT danach re-interview ausfuehren
- Low-Power-Betrieb ueber Deep Sleep ist aktiviert (typisch: kurzer Wake-Zyklus, dann lange Schlafphase)

Akku/Batterie in der Konfiguration:

- `i2c_sda_pin`: I2C SDA (Feather default GPIO19)
- `i2c_scl_pin`: I2C SCL (Feather default GPIO18)
- `battery_low_pct`: Schwellwert fuer "Akku niedrig"
- `battery_critical_pct`: Schwellwert fuer "Akku kritisch"
- `awake_max_duration`: maximale Awake-Zeit pro Zyklus
- `sleep_duration`: Schlafdauer pro Zyklus

Die Messung erfolgt beim Adafruit ESP32-C6 Feather ueber das interne MAX17048 Fuel Gauge (I2C), nicht ueber einen direkten VBAT-ADC-Pin.

Exponierte Zusatzwerte:

- Akkuspannung (V)
- Akkustand (%)
- Akku niedrig (binary sensor)
- Akku kritisch (binary sensor)

Hinweis: Auf ESP32-C6 kann Zigbee funktionieren, aber je nach Board/Funkumgebung schwanken Stabilitaet und Reichweite.
