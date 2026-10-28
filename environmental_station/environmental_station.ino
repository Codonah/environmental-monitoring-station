// Outdoor Environmental Monitoring Station
// Board: Arduino Nano Every
// Sensors: MS5611 (I2C) pressure/temp, DS18B20 (OneWire) probe, DS3231 (I2C) RTC
// Storage: MicroSD (SPI), CSV log

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <MS5611.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------- Configuration ----------
const int SD_CS_PIN = 10;
const int ONE_WIRE_PIN = 2;
const unsigned long LOG_INTERVAL_MS = 2000;

// Empirical correction applied to the MS5611 library's pressure value on the
// Nano Every. TODO: verify against a reference reading or the datasheet math.
const float PRESSURE_SCALE = 2.0;

// ---------- Data ----------
struct Reading {
  char timestamp[20];
  float pressure_hPa;
  float baroTemp_C;
  float probeTemp_C;   // NAN when the probe could not be read
};

// ---------- SensorReader: owns every sensor and the clock ----------
class SensorReader {
public:
  SensorReader() : _ms5611(0x77), _oneWire(ONE_WIRE_PIN), _probe(&_oneWire) {}

  bool begin() {
    bool ok = true;

    if (!_rtc.begin()) {
      Serial.println(F("ERROR: Couldn't find RTC!"));
      ok = false;
    } else {
      Serial.println(F("SUCCESS: RTC ready."));
      if (_rtc.lostPower()) {
        _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }
    }

    if (!_ms5611.begin()) {
      Serial.println(F("ERROR: MS5611 sensor not found!"));
      ok = false;
    } else {
      _ms5611.setOversampling(OSR_ULTRA_HIGH);
      Serial.println(F("SUCCESS: MS5611 pressure sensor ready."));
    }

    _probe.begin();
    for (int i = 0; i < 5 && _probe.getDeviceCount() == 0; i++) {
      delay(200);
      _probe.begin();   // rescan the bus
    }
    Serial.print(F("DS18B20 devices found: "));
    Serial.println(_probe.getDeviceCount());
    if (_probe.getDeviceCount() == 0) ok = false;

    return ok;
  }

  Reading read() {
    Reading r;

    DateTime now = _rtc.now();
    snprintf(r.timestamp, sizeof(r.timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    _ms5611.read();
    r.pressure_hPa = _ms5611.getPressure() * PRESSURE_SCALE;
    r.baroTemp_C = _ms5611.getTemperature();
    r.probeTemp_C = readProbe();
    return r;
  }

private:
  // Returns NAN if the probe does not answer after a few tries
  float readProbe() {
    for (int i = 0; i < 3; i++) {
      _probe.requestTemperatures();
      float t = _probe.getTempCByIndex(0);
      if (t != DEVICE_DISCONNECTED_C) return t;
      delay(200);
    }
    return NAN;
  }

  RTC_DS3231 _rtc;
  MS5611 _ms5611;
  OneWire _oneWire;          
  DallasTemperature _probe;
};

// ---------- CsvLogger: owns the SD card and the log file ----------
class CsvLogger {
public:
  explicit CsvLogger(const char* filename) : _filename(filename) {}

  bool begin(int csPin) {
    if (!SD.begin(csPin)) {
      Serial.println(F("ERROR: SD Card initialization failed!"));
      return false;
    }
    Serial.println(F("SUCCESS: SD Card ready."));

    File f = SD.open(_filename, FILE_WRITE);
    if (!f) return false;
    if (f.size() == 0) {
      f.println(F("Timestamp,Pressure_hPa,Baro_Temp_C,Probe_Temp_C"));
    }
    f.close();
    return true;
  }

  bool append(const Reading& r) {
    File f = SD.open(_filename, FILE_WRITE);
    if (!f) return false;

    f.print(r.timestamp);    f.print(',');
    f.print(r.pressure_hPa); f.print(',');
    f.print(r.baroTemp_C);   f.print(',');
    if (!isnan(r.probeTemp_C)) f.print(r.probeTemp_C);   // empty field if missing
    f.println();
    f.close();
    return true;
  }

private:
  const char* _filename;     // SD library needs 8.3 names
};

// ---------- Globals ----------
SensorReader sensors;
CsvLogger logger("env_data.csv");
unsigned long lastLogMs = 0;

void printReading(const Reading& r) {
  Serial.print(r.timestamp);
  Serial.print(F(" | Press: ")); Serial.print(r.pressure_hPa); Serial.print(F(" hPa"));
  Serial.print(F(" | BaroT: ")); Serial.print(r.baroTemp_C);  Serial.print(F(" C"));
  Serial.print(F(" | ProbeT: ")); Serial.print(r.probeTemp_C); Serial.println(F(" C"));
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(9600);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) {}   // wait up to 2 s for USB, then carry on

  Serial.println(F("--- Environmental Station Initializing ---"));

  sensors.begin();
  logger.begin(SD_CS_PIN);

  Serial.println(F("--- Setup Complete. Logging started! ---\n"));
}

void loop() {
  // Timer instead of delay(): keeps the loop free for a display and encoder later.
  if (millis() - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = millis();

    Reading r = sensors.read();
    printReading(r);

    if (!logger.append(r)) {
      Serial.println(F("Error writing to log file!"));
    }
  }
}
