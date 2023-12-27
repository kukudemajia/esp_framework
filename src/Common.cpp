#include "Common.h"
#include "Log.h"
#ifdef ESP8266
uint32_t FlashWriteStartSector(void)
{
    return (ESP.getSketchSize() / SPI_FLASH_SEC_SIZE) + 2; // Stay on the safe side
}

uint32_t FlashWriteMaxSector(void)
{
    return (((uint32_t)&_FS_start - 0x40200000) / SPI_FLASH_SEC_SIZE) - 2;
}

uint8_t *FlashDirectAccess(void)
{
    return (uint8_t *)(0x40200000 + (FlashWriteStartSector() * SPI_FLASH_SEC_SIZE));
}
#endif

#ifdef ESP32
extern "C"
{
#include "esp_ota_ops.h"
#include "esp_image_format.h"
}

uint32_t EspFlashBaseAddress(void)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition)
    {
        return 0;
    }
    return partition->address; // For tasmota 0x00010000 or 0x00200000
}

uint32_t EspFlashBaseEndAddress(void)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition)
    {
        return 0;
    }
    return partition->address + partition->size; // For tasmota 0x00200000 or 0x003F0000
}

uint8_t *EspFlashMmap(uint32_t address)
{
    static spi_flash_mmap_handle_t handle = 0;

    if (handle)
    {
        spi_flash_munmap(handle);
        handle = 0;
    }

    const uint8_t *data;
    spi_flash_mmap(address, 8 * SPI_FLASH_MMU_PAGE_SIZE, SPI_FLASH_MMAP_DATA, (const void **)&data, &handle);
    return (uint8_t *)data;
}

uint32_t FlashWriteStartSector(void)
{
    // Needs to be on SPI_FLASH_MMU_PAGE_SIZE (= 0x10000) alignment for mmap usage
    uint32_t aligned_address = ((EspFlashBaseAddress() + (2 * SPI_FLASH_MMU_PAGE_SIZE)) / SPI_FLASH_MMU_PAGE_SIZE) * SPI_FLASH_MMU_PAGE_SIZE;
    return aligned_address / SPI_FLASH_SEC_SIZE;
}

uint32_t FlashWriteMaxSector(void)
{
    // Needs to be on SPI_FLASH_MMU_PAGE_SIZE (= 0x10000) alignment for mmap usage
    uint32_t aligned_end_address = (EspFlashBaseEndAddress() / SPI_FLASH_MMU_PAGE_SIZE) * SPI_FLASH_MMU_PAGE_SIZE;
    return aligned_end_address / SPI_FLASH_SEC_SIZE;
}

uint8_t *FlashDirectAccess(void)
{
    uint32_t address = FlashWriteStartSector() * SPI_FLASH_SEC_SIZE;
    uint8_t *data = EspFlashMmap(address);
    return data;
}

// EEPROMClass EEPROMconfig("espconfig", SPI_FLASH_SEC_SIZE);
EEPROMClass EEPROMconfig("espconfig");
bool isIniteeprom = false;
bool espconfig_spiflash_init()
{
    if (isIniteeprom)
    {
        return true;
    }
    if (!EEPROMconfig.begin(SPI_FLASH_SEC_SIZE))
    {
        Log::Error(PSTR("Failed to initialise EEPROMconfig"));
        delay(1000);
        ESP.restart();
        return false;
    }
    isIniteeprom = true;
    return true;
}

bool espconfig_spiflash_erase_sector(size_t sector)
{
    espconfig_spiflash_init();
    for (int i = 0; i < SPI_FLASH_SEC_SIZE; i++)
    {
        EEPROMconfig.write(i, 0xFF);
    }
    return EEPROMconfig.commit();
}

bool espconfig_spiflash_write(size_t dest_addr, const void *src, size_t size)
{
    espconfig_spiflash_init();
    if (EEPROMconfig.writeBytes(dest_addr, src, size) == 0)
    {
        return false;
    }
    return EEPROMconfig.commit();
}

bool espconfig_spiflash_read(size_t src_addr, void *dest, size_t size)
{
    espconfig_spiflash_init();
    if (EEPROMconfig.readBytes(src_addr, dest, size) == 0)
    {
        return false;
    }
    return true;
}

uint32_t sntp_get_current_timestamp()
{
    time_t now;
    time(&now);
    return now;
}

inline bool Ticker::active()
{
    return _timer;
}

uint32_t ESP_getChipId(void)
{
    uint32_t id = 0;
    for (uint32_t i = 0; i < 17; i = i + 8)
    {
        id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    return id;
}

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

void DisableBrownout(void)
{
    // https://github.com/espressif/arduino-esp32/issues/863#issuecomment-347179737
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
}

String ESP32GetResetReason(uint32_t cpu_no)
{
    // tools\sdk\include\esp32\rom\rtc.h
    // tools\sdk\esp32\include\esp_rom\include\esp32c3\rom\rtc.h
    // tools\sdk\esp32\include\esp_rom\include\esp32s2\rom\rtc.h
    switch (rtc_get_reset_reason(cpu_no))
    { //     ESP32             ESP32-S / ESP32-C
    case 1:
        return F("Vbat power on reset"); // 1   POWERON_RESET     POWERON_RESET
    case 3:
        return F("Software reset digital core"); // 3   SW_RESET          RTC_SW_SYS_RESET
    case 4:
        return F("Legacy watch dog reset digital core"); // 4   OWDT_RESET        -
    case 5:
        return F("Deep Sleep reset digital core"); // 5   DEEPSLEEP_RESET   DEEPSLEEP_RESET
    case 6:
        return F("Reset by SLC module, reset digital core"); // 6   SDIO_RESET
    case 7:
        return F("Timer Group0 Watch dog reset digital core"); // 7   TG0WDT_SYS_RESET
    case 8:
        return F("Timer Group1 Watch dog reset digital core"); // 8   TG1WDT_SYS_RESET
    case 9:
        return F("RTC Watch dog Reset digital core"); // 9   RTCWDT_SYS_RESET
    case 10:
        return F("Instrusion tested to reset CPU"); // 10  INTRUSION_RESET
    case 11:
        return F("Time Group0 reset CPU"); // 11  TGWDT_CPU_RESET   TG0WDT_CPU_RESET
    case 12:
        return F("Software reset CPU"); // 12  SW_CPU_RESET      RTC_SW_CPU_RESET
    case 13:
        return F("RTC Watch dog Reset CPU"); // 13  RTCWDT_CPU_RESET
    case 14:
        return F("or APP CPU, reseted by PRO CPU"); // 14  EXT_CPU_RESET     -
    case 15:
        return F("Reset when the vdd voltage is not stable"); // 15  RTCWDT_BROWN_OUT_RESET
    case 16:
        return F("RTC Watch dog reset digital core and rtc module"); // 16  RTCWDT_RTC_RESET
    case 17:
        return F("Time Group1 reset CPU"); // 17  -                 TG1WDT_CPU_RESET
    case 18:
        return F("Super watchdog reset digital core and rtc module"); // 18  -                 SUPER_WDT_RESET
    case 19:
        return F("Glitch reset digital core and rtc module"); // 19  -                 GLITCH_RTC_RESET
    default:
        return F("No meaning"); // 0 and undefined
    }
}

String ESP_getResetReason(void)
{
    return ESP32GetResetReason(0); // CPU 0
}

uint32_t ESP_getSketchSize(void)
{
    static uint32_t sketchsize = 0;

    if (!sketchsize)
    {
        sketchsize = ESP.getSketchSize(); // This takes almost 2 seconds on an ESP32
    }
    return sketchsize;
}

uint8_t pwm_channel[6] = {99, 99, 99, 99, 99, 99};
uint8_t pin2chan(uint8_t pin)
{
    for (uint8_t cnt = 0; cnt < 6; cnt++)
    {
        if (pwm_channel[cnt] == 99)
        {
            pwm_channel[cnt] = pin;
            uint8_t ch = cnt;
            ledcSetup(ch, 1000, 8);
            ledcAttachPin(pin, ch);
            return cnt;
        }
        if ((pwm_channel[cnt] < 99) && (pwm_channel[cnt] == pin))
        {
            uint8_t ch = cnt;
            ledcSetup(ch, 1000, 8);
            ledcAttachPin(pin, ch);
            return cnt;
        }
    }
    return 0;
}

void analogWriteA(uint8_t pin, int val)
{
    uint8_t channel = pin2chan(pin);
    ledcWrite(channel, val);
}

int8_t readUserData(size_t src_offset, void *dst, size_t size)
{
    const esp_partition_t *find_partition = esp_partition_find_first((esp_partition_type_t)0x40, (esp_partition_subtype_t)0x0, NULL);
    if (find_partition == NULL)
    {
        Serial.print("No partition found!");
        return -1;
    }

    if (esp_partition_read(find_partition, src_offset, dst, size) != ESP_OK)
    {
        Serial.print("Read partition data error");
        return -2;
    }
    return 0;
}

int8_t writeUserData(size_t dst_offset, const void *src, size_t size)
{
    const esp_partition_t *find_partition = esp_partition_find_first((esp_partition_type_t)0x40, (esp_partition_subtype_t)0x0, NULL);
    if (find_partition == NULL)
    {
        Serial.print("No partition found!");
        return -1;
    }

    if (dst_offset + size > find_partition->size)
    {
        Serial.print("error size");
        return -1;
    }

    uint8_t sector = (dst_offset / SPI_FLASH_SEC_SIZE);
    uint8_t copy[SPI_FLASH_SEC_SIZE];

    if (esp_partition_read(find_partition, sector * SPI_FLASH_SEC_SIZE, copy, SPI_FLASH_SEC_SIZE) != ESP_OK)
    {
        Serial.print("Read partition data error");
        return -2;
    }

    if (esp_partition_erase_range(find_partition, sector * SPI_FLASH_SEC_SIZE, SPI_FLASH_SEC_SIZE) != ESP_OK)
    {
        Serial.print("Erase partition error");
        return -3;
    }

    memcpy(&copy[dst_offset % SPI_FLASH_SEC_SIZE], src, size);

    if (esp_partition_write(find_partition, sector * SPI_FLASH_SEC_SIZE, copy, SPI_FLASH_SEC_SIZE) != ESP_OK)
    {
        Serial.print("Write partition data error");
        return -4;
    }

    return 0;
}

#if defined(CONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH) && defined(USE_UFILESYS)
extern "C"
{
#include "esp_core_dump.h"
}
void CoreDumpToFile()
{
    size_t size = 0;
    size_t address = 0;
    if (esp_core_dump_image_get(&address, &size) != ESP_OK)
    {
        return;
    }
    const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");
    if (!pt)
    {
        return;
    }

    char fileName[13] = {0}; // /coredump_01
    uint8_t index = 0;
    for (index = 1; index <= 20; index++)
    {
        sprintf(fileName, PSTR("/coredump_%02d"), index);
        if (!LittleFS.exists(fileName))
        {
            break;
        }
    }
    esp_err_t err;
    File file = LittleFS.open(fileName, "w");
    if (file)
    {
        uint8_t bf[256];
        int16_t toRead;
        for (int16_t i = 0; i < (size / 256) + 1; i++)
        {
            toRead = (size - i * 256) > 256 ? 256 : (size - i * 256);
            err = esp_partition_read(pt, i * 256, bf, toRead);
            if (err != ESP_OK)
            {
                // Serial.printf("FAIL [%x]\n", er);
                break;
            }
            file.write(bf, toRead);
        }
        file.close();
    }
    if (index >= 20)
    {
        sprintf(fileName, PSTR("/coredump_%02d"), 1);
    }
    else
    {
        sprintf(fileName, PSTR("/coredump_%02d"), index + 1);
    }
    LittleFS.remove(fileName);

    err = esp_partition_erase_range(pt, 0, pt->size);
    if (err == ESP_OK)
    {
        const uint32_t invalid_size = 0xFFFFFFFF;
        err = esp_partition_write(pt, 0, &invalid_size, sizeof(invalid_size));
    }
}
#endif
#endif
