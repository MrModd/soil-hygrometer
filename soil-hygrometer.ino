#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include "esp32-hal-log.h"

#define NO_SLEEP 0 // Just for debug purpose, replace the deep sleep with a delay
#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for micro seconds to seconds
#define SLEEP_TIME  1800 // Sleep for 30m

#define ZIGBEE_ENDPOINT 10
#define ANALOG_SAMPLES 16 // An average is computed
#define BOOT_DELAY_S 10 // How long to wait for the BOOT button press to force a factory reset
#define CONNECT_DELAY_S 10 // How long to wait after the connection to let HA finish its discovery

const uint8_t led = LED_BUILTIN;
const uint8_t button = BOOT_PIN;
const uint8_t vbatt_pin = 5; // A5
const uint8_t sensor_pin = A0;

const int max_vbatt_mvolt = 3800;
const int min_vbatt_mvolt = 3100;
// Replace these values from the readings of the soil sensor
const int air_value = 2168;
const int water_value = 926;

inline void led_off(void) {
  digitalWrite(led, HIGH);
}

inline void led_on(void) {
  digitalWrite(led, LOW);
}

inline void toggle_led(bool *state) {
  if (*state) {
    led_off();
    *state = false;
  } else {
    led_on();
    *state = true;
  }
}

ZigbeeTempSensor zbSensor = ZigbeeTempSensor(ZIGBEE_ENDPOINT);

uint32_t get_vbatt_mvolt() {
  uint32_t vbatt = 0;
  for (int i = 0; i < ANALOG_SAMPLES; ++i) {
    vbatt += analogReadMilliVolts(vbatt_pin); // Read and accumulate ADC voltage
  }
  vbatt /= ANALOG_SAMPLES; // Ignoring the decimal part
  vbatt *= 2; // Adjust for 1:2 divider
  log_d("Raw value from battery status %d", vbatt);
  return vbatt;
}

uint32_t get_humidity_raw(void) {
  uint32_t raw_value = 0;
  for (int i = 0; i < ANALOG_SAMPLES; ++i) {
    raw_value += analogRead(sensor_pin);
  }
  raw_value /= ANALOG_SAMPLES; // Ignoring the decimal part
  log_d("Raw value from humidity sensor: %d", raw_value);
  return raw_value;
}

uint8_t raw_value_to_percent(uint32_t raw_value, const int min_range, const int max_range) {
  uint8_t percent;
  if (raw_value < min_range) {
    percent = 0;
  } else if (raw_value > max_range) {
    percent = 100;
  } else {
    percent = (uint8_t)map(raw_value, min_range, max_range, 0, 100);
  }
  log_i("Percent value %d%%", percent);
  return percent;
}

void wait_for_factory_reset(void) {
  log_i("Hold BOOT button for 3 seconds to factory reset");

  for (uint32_t s = 0; s < BOOT_DELAY_S; s += 1) {
    log_i("%d", BOOT_DELAY_S - s);
    for (uint32_t ms = 0; ms < 10; ++ms) {
      // Checking button for factory reset
      if (digitalRead(button) == LOW) {  // Push button pressed
        // Key debounce handling
        delay(100);
        int startTime = millis();
        bool led_state = true;
        while (digitalRead(button) == LOW) {
          delay(100);
          toggle_led(&led_state);
          if ((millis() - startTime) > 3000) {
            // If key pressed for more than 3secs, factory reset Zigbee and reboot
            log_i("Resetting Zigbee to factory and rebooting in 1s.");
            delay(1000);
            Zigbee.factoryReset();
          }
        }
        led_on();
      }
      delay(100);
    }
  }
}

void go_to_sleep(void) {
  if (NO_SLEEP) {
    log_d("Busy wait");
    for (int i = 0; i < SLEEP_TIME; ++i) {
      delay(1000);
    }
  } else {
    // Configure the wake up source and set to wake up every SLEEP_TIME seconds
    esp_sleep_enable_timer_wakeup(SLEEP_TIME * uS_TO_S_FACTOR);
    log_d("Going to sleep now");
    esp_deep_sleep_start();
    log_e("This should not be printed");
    // At the wake up event, the execution starts from the beginning
  }
}

bool get_wakeup_reason() {
  /* true: caused by IRQ
     false: not caused by IRQ */
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:     log_d("Wakeup caused by external signal using RTC_IO"); return true;
    case ESP_SLEEP_WAKEUP_EXT1:     log_d("Wakeup caused by external signal using RTC_CNTL"); return true;
    case ESP_SLEEP_WAKEUP_TIMER:    log_d("Wakeup caused by timer"); return true;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: log_d("Wakeup caused by touchpad"); return true;
    case ESP_SLEEP_WAKEUP_ULP:      log_d("Wakeup caused by ULP program"); return true;
    default:                        log_d("Wakeup was not caused by deep sleep: %d", wakeup_reason); return false;
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(115200);
  log_i("Hello world!");

  // Init LED and turn it ON
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  // VBatt sensing
  pinMode(vbatt_pin, ANALOG);
  // Init button for factory reset
  pinMode(button, INPUT_PULLUP);
  // Moisture sensor
  pinMode(sensor_pin, ANALOG);
  
  bool first_reset = !get_wakeup_reason();

  //Zigbee initial setup
  zbSensor.setManufacturerAndModel("Seed Studio", "XIAO-ESP32-C6");
  zbSensor.setPowerSource(ZB_POWER_SOURCE_BATTERY, raw_value_to_percent(get_vbatt_mvolt(), min_vbatt_mvolt, max_vbatt_mvolt));

  zbSensor.addHumiditySensor(0, 100, 1);

  //Add endpoint to Zigbee Core
  log_i("Adding Zigbee endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbSensor);

  // Create a custom Zigbee configuration for End Device with keep alive 10s to avoid interference with reporting data
  esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
  zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = 10000;

  // When all EPs are registered, start Zigbee. By default acts as ZIGBEE_END_DEVICE
  if (!Zigbee.begin(&zigbeeConfig, false)) {
    log_e("Zigbee failed to start!");
    log_e("Rebooting...");
    ESP.restart();
  }

  if (first_reset) {
    wait_for_factory_reset();
  }

  // Using Serial here because I want to not print \n
  Serial.println("Connecting to network");
  bool led_state = true;
  while (!Zigbee.connected()) {
    toggle_led(&led_state);
    Serial.print(".");
    delay(200);
  }
  led_on();
  Serial.println();
  Serial.println("Connected");

  if (first_reset) {
    led_state = true;
    log_i("Waiting for a stable connection");
    for (uint32_t i = 0; i < CONNECT_DELAY_S; ++i) {
      toggle_led(&led_state);
      delay(1000);
    }
  }

  zbSensor.setHumidityReporting(SLEEP_TIME, SLEEP_TIME, 100);
}

void loop() {
  // Need to inverse the percentage because the analog value is inverted
  float humidity_percent = (float)(100 - raw_value_to_percent(get_humidity_raw(), water_value, air_value));
  log_d("Actual humidity: %.2f%%", humidity_percent);
  zbSensor.setHumidity(humidity_percent);
  zbSensor.reportHumidity();
  zbSensor.setBatteryPercentage(raw_value_to_percent(get_vbatt_mvolt(), min_vbatt_mvolt, max_vbatt_mvolt));
  zbSensor.reportBatteryPercentage();

  delay(100); // Let the data to be sent

  led_off();
  go_to_sleep();
  led_on();
}
