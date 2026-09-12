#include <esp_err.h>

// Arduino-ESP32 2.0.17 initializes generic NVS before setup() and, on certain
// errors, erases the first NVS partition. This offline app has no NVS clients:
// settings live only in RAM / optional SD. Skip that startup initialization so
// an app-only M5Apps installation never formats a launcher's shared NVS.
// Revisit this guard before adding Wi-Fi, Preferences, BLE or other NVS users.
extern "C" esp_err_t __wrap_nvs_flash_init(void) {
    return ESP_OK;
}
