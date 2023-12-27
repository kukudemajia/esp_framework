#ifndef _COMMON_h

#include "Arduino.h"

/*********************************************************************************************\
 * ESP8266 Support
\*********************************************************************************************/

#ifdef ESP8266

#include <ESP8266Wifi.h>
#include <ESP8266WebServer.h>
#include <flash_hal.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>
#include "sntp.h"

#define ESPHTTPUpdate ESPhttpUpdate
#define WebServer ESP8266WebServer

#define ESP_Restart() ESP.reset()
#define ESP_getChipId() ESP.getChipId()
#define ESP_getSketchSize() ESP.getSketchSize()
#define ESP_getResetReason() ESP.getResetReason()
#define WIFI_setHostname(aHostname) WiFi.hostname(aHostname)

#define DRAM_ATTR

#define PortUdp_write(p, n) PortUdp.write(p, n)

extern "C" uint32_t _EEPROM_start; //See EEPROM.cpp
#define EEPROM_PHYS_ADDR ((uint32_t)(&_EEPROM_start) - 0x40200000)

#endif

/*********************************************************************************************\
 * ESP32 Support
\*********************************************************************************************/
#ifdef ESP32

#include <WebServer.h>
#include <Wifi.h>
#include <esp_spi_flash.h>
#include <nvs.h>
#include <Update.h>
#include <HTTPUpdate.h>
#if ESP_IDF_VERSION_MAJOR > 3      // IDF 4+
  #if CONFIG_IDF_TARGET_ESP32      // ESP32/PICO-D4
    #include "esp32/rom/rtc.h"
  #elif CONFIG_IDF_TARGET_ESP32S2  // ESP32-S2
    #include "esp32s2/rom/rtc.h"
  #elif CONFIG_IDF_TARGET_ESP32C3  // ESP32-C3
    #include "esp32c3/rom/rtc.h"
  #else
    #error Target CONFIG_IDF_TARGET is not supported
  #endif
#else // ESP32 Before IDF 4.0
  #include "rom/rtc.h"
#endif
#include <EEPROM.h>
#include <WiFiUdp.h>

#define ARDUINO_ESP8266_RELEASE ""
#define EEPROM_PHYS_ADDR 0
#define ESPHTTPUpdate httpUpdate

#define ESP_Restart() ESP.restart()
#if CONFIG_IDF_TARGET_ESP32
#define WIFI_setHostname(aHostname)                     \
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); \
    WiFi.setHostname(aHostname)
#else
#define WIFI_setHostname(aHostname) WiFi.setHostname(aHostname)
#endif

#define isFlashInterfacePin(p) 0

bool espconfig_spiflash_init();
bool espconfig_spiflash_erase_sector(size_t sector);
bool espconfig_spiflash_write(size_t dest_addr, const void *src, size_t size);
bool espconfig_spiflash_read(size_t src_addr, void *dest, size_t size);

#define PortUdp_write(p, n) PortUdp.write((const uint8_t *)p, n)

uint32_t sntp_get_current_timestamp();

void DisableBrownout(void);
String ESP32GetResetReason(uint32_t cpu_no);
String ESP_getResetReason(void);
uint32_t ESP_getChipId(void);
uint32_t ESP_getSketchSize(void);

int8_t readUserData(size_t src_offset, void *dst, size_t size);
int8_t writeUserData(size_t dst_offset, const void *src, size_t size);

uint32_t pin2chan(uint32_t pin);
void analogWriteA(uint8_t pin, int val);
extern uint8_t pwm_channel[6];
#endif

uint32_t FlashWriteStartSector(void);
uint32_t FlashWriteMaxSector(void);
uint8_t *FlashDirectAccess(void);

#if defined(CONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH) && defined(USE_UFILESYS)
void CoreDumpToFile();
#endif
#endif
