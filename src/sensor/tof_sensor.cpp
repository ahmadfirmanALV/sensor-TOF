#include "sensor/tof_sensor.h"
#include "config.h"
#include "wdt/wdt_service.h"
#include <Wire.h>
#include <VL53L0X.h>

struct SensorState {
  uint16_t samples[3] = {0, 0, 0};
  uint8_t sampleCount = 0;
  uint8_t nextSample = 0;
  uint8_t enterCount = 0;
  uint8_t clearCount = 0;
  uint8_t timeoutCount = 0;
  uint16_t lastRangeMm = 0;
  bool lastRangeWasTimeout = false;
  bool active = false;
  bool online = false;
  uint32_t retryAt = 0;
};

static VL53L0X sensorA;
static VL53L0X sensorB;
static SensorState stateA;
static SensorState stateB;

static bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

static uint16_t medianOfThree(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) {
    const uint16_t tmp = a;
    a = b;
    b = tmp;
  }
  if (b > c) {
    const uint16_t tmp = b;
    b = c;
    c = tmp;
  }
  if (a > b) {
    b = a;
  }
  return b;
}

static uint16_t filteredRange(const SensorState &sensor) {
  if (sensor.sampleCount == 0) {
    return UINT16_MAX;
  }
  if (sensor.sampleCount == 1) {
    return sensor.samples[0];
  }
  if (sensor.sampleCount == 2) {
    return static_cast<uint16_t>((static_cast<uint32_t>(sensor.samples[0]) +
                                  sensor.samples[1]) / 2);
  }
  return medianOfThree(sensor.samples[0], sensor.samples[1], sensor.samples[2]);
}

static void resetFilter(SensorState &state) {
  state.sampleCount = 0;
  state.nextSample = 0;
  state.enterCount = 0;
  state.clearCount = 0;
  state.active = false;
}

static void addMeasurement(SensorState &state, uint16_t rangeMm, bool timedOut,
                           uint16_t triggerMm, uint16_t releaseMm) {
  state.lastRangeMm = rangeMm;
  state.lastRangeWasTimeout = timedOut;

  if (timedOut) {
    if (state.timeoutCount < UINT8_MAX) {
      ++state.timeoutCount;
    }
    if (state.timeoutCount >= TIMEOUTS_BEFORE_REINIT) {
      state.online = false;
      state.retryAt = millis() + SENSOR_RETRY_MS;
      resetFilter(state);
    }
    return;
  }

  state.timeoutCount = 0;
  state.online = true;

  const uint16_t filteredInput =
      (rangeMm > 0 && rangeMm <= MAX_REASONABLE_RANGE_MM) ? rangeMm : UINT16_MAX;
  state.samples[state.nextSample] = filteredInput;
  state.nextSample = static_cast<uint8_t>((state.nextSample + 1) % 3);
  if (state.sampleCount < 3) {
    ++state.sampleCount;
  }

  const uint16_t range = filteredRange(state);
  if (!state.active) {
    state.clearCount = 0;
    if (range <= triggerMm) {
      if (state.enterCount < ENTER_CONFIRM_SAMPLES) {
        ++state.enterCount;
      }
      if (state.enterCount >= ENTER_CONFIRM_SAMPLES) {
        state.active = true;
        state.enterCount = 0;
      }
    } else {
      state.enterCount = 0;
    }
  } else {
    state.enterCount = 0;
    if (range >= releaseMm) {
      if (state.clearCount < CLEAR_CONFIRM_SAMPLES) {
        ++state.clearCount;
      }
      if (state.clearCount >= CLEAR_CONFIRM_SAMPLES) {
        state.active = false;
        state.clearCount = 0;
      }
    } else {
      state.clearCount = 0;
    }
  }
}

static bool startSensor(VL53L0X &sensor, uint8_t xshutPin, uint8_t address) {
  digitalWrite(xshutPin, LOW);
  delay(15);

  sensor.setAddress(I2C_ADDRESS_DEFAULT);
  digitalWrite(xshutPin, HIGH);
  delay(15);

  sensor.setTimeout(SENSOR_TIMEOUT_MS);
  if (!sensor.init()) {
    digitalWrite(xshutPin, LOW);
    return false;
  }

  sensor.setAddress(address);
  if (sensor.last_status != 0) {
    digitalWrite(xshutPin, LOW);
    sensor.setAddress(I2C_ADDRESS_DEFAULT);
    return false;
  }

  sensor.setTimeout(SENSOR_TIMEOUT_MS);
  if (!sensor.setMeasurementTimingBudget(MEASUREMENT_BUDGET_US)) {
    digitalWrite(xshutPin, LOW);
    sensor.setAddress(I2C_ADDRESS_DEFAULT);
    return false;
  }
  return true;
}

static void recoverSensor(VL53L0X &sensor, SensorState &state, uint8_t xshutPin,
                          uint8_t address, const char *label, uint32_t now) {
  if (state.online || !timeReached(now, state.retryAt)) {
    return;
  }

  logLine("[ToF %s] mencoba inisialisasi...", label);
  if (startSensor(sensor, xshutPin, address)) {
    state.online = true;
    state.timeoutCount = 0;
    state.lastRangeMm = 0;
    state.lastRangeWasTimeout = false;
    resetFilter(state);
    logLine("[ToF %s] siap, alamat I2C 0x%02X", label,
            static_cast<unsigned int>(address));
  } else {
    state.online = false;
    state.retryAt = millis() + SENSOR_RETRY_MS;
    logLine("[ToF %s] belum terbaca; periksa jalur kabel dan pin XSHUT", label);
  }
}

static void readOneSensor(VL53L0X &sensor, SensorState &state,
                          uint16_t triggerMm, uint16_t releaseMm) {
  if (!state.online) {
    state.lastRangeWasTimeout = true;
    return;
  }

  const uint16_t rangeMm = sensor.readRangeSingleMillimeters();
  const bool timedOut = sensor.timeoutOccurred();
  addMeasurement(state, rangeMm, timedOut, triggerMm, releaseMm);
}

void initToFSensors() {
  pinMode(PIN_XSHUT_A, OUTPUT);
  pinMode(PIN_XSHUT_B, OUTPUT);
  digitalWrite(PIN_XSHUT_A, LOW);
  digitalWrite(PIN_XSHUT_B, LOW);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  logLine("I2C: SDA GPIO%d, SCL GPIO%d | XSHUT A GPIO%d, B GPIO%d",
          PIN_I2C_SDA, PIN_I2C_SCL, PIN_XSHUT_A, PIN_XSHUT_B);
}

void tofReadTask(void *parameter) {
  (void)parameter;
  wdtRegisterCurrentTask("ToFReadTask");

  stateA.retryAt = millis();
  stateB.retryAt = millis();

  for (;;) {
    wdtFeed();

    uint32_t now = millis();
    recoverSensor(sensorA, stateA, PIN_XSHUT_A, I2C_ADDRESS_A, "A", now);
    recoverSensor(sensorB, stateB, PIN_XSHUT_B, I2C_ADDRESS_B, "B", now);

    wdtFeed();

    readOneSensor(sensorA, stateA, SENSOR_A_TRIGGER_MM, SENSOR_A_RELEASE_MM);
    readOneSensor(sensorB, stateB, SENSOR_B_TRIGGER_MM, SENSOR_B_RELEASE_MM);

    wdtFeed();

    const SensorSnapshot snapshot = {
        filteredRange(stateA),
        filteredRange(stateB),
        stateA.online,
        stateB.online,
        stateA.lastRangeWasTimeout,
        stateB.lastRangeWasTimeout,
        stateA.active,
        stateB.active,
    };

    xQueueSend(sensorQueue, &snapshot, portMAX_DELAY);

    wdtFeed();

    if (!stateA.online && !stateB.online) {
      vTaskDelay(pdMS_TO_TICKS(100));
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

bool startToFTask() {
  return xTaskCreatePinnedToCore(tofReadTask, "ToFReadTask", 6144,
                                 nullptr, 3, nullptr, 1) == pdPASS;
}
