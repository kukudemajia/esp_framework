#include "Arduino.h"
#include "Module.h"

Module *module;
char UID[16];
char tmpData[LOG_SIZE] = {0};
char mqttData[700] = {0};
uint32_t perSecond;
Ticker *tickerPerSecond;
GlobalConfigMessage globalConfig;
bool (*module_func_ptr[10])(uint8_t) = {0};
uint8_t module_func_present = 0;

uint16_t Config::nowCrc;
uint8_t Config::countdown = 60;
bool Config::isDelay = false;
uint8_t Config::operationFlag = 0; // 0每秒 1保存重启
uint8_t Config::statusFlag = 0;

const uint16_t crcTalbe[] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};

void addModule(bool (*call)(uint8_t))
{
    module_func_ptr[module_func_present++] = call;
}

bool callModule(uint8_t function)
{
    bool result = false;
    for (uint32_t x = 0; x < module_func_present; x++)
    {
        yield();
        result = module_func_ptr[x](function);
        if (result && ((FUNC_COMMAND == function)))
        {
            break;
        }
    }
    return result;
}

/**
 * 计算Crc16
 */
uint16_t Config::crc16(uint8_t *ptr, uint16_t len)
{
    uint16_t crc = 0xffff;
    for (uint16_t i = 0; i < len; i++)
    {
        crc = crcTalbe[(ptr[i] ^ crc) & 15] ^ (crc >> 4);
        crc = crcTalbe[((ptr[i] >> 4) ^ crc) & 15] ^ (crc >> 4);
    }
    return crc;
}

void Config::resetConfig()
{
    Log::Info(PSTR("resetConfig . . . OK"));
    memset(&globalConfig, 0, sizeof(GlobalConfigMessage));

#ifdef WIFI_SSID
    strcpy(globalConfig.wifi.ssid, WIFI_SSID);
#endif
#ifdef WIFI_PASS
    strcpy(globalConfig.wifi.pass, WIFI_PASS);
#endif
#ifdef MQTT_SERVER
    strcpy(globalConfig.mqtt.server, MQTT_SERVER);
#endif
#ifdef MQTT_PORT
    globalConfig.mqtt.port = MQTT_PORT;
#endif
#ifdef MQTT_USER
    strcpy(globalConfig.mqtt.user, MQTT_USER);
#endif
#ifdef MQTT_PASS
    strcpy(globalConfig.mqtt.pass, MQTT_PASS);
#endif
    globalConfig.mqtt.discovery = true;//默认开启homeassistant自动发现功能
    strcpy(globalConfig.mqtt.discovery_prefix, "homeassistant");
#ifdef MQTT_FULLTOPIC
    strcpy(globalConfig.mqtt.topic, MQTT_FULLTOPIC);
#endif
#ifdef MQTT_FORCE_OTA_TOPIC
    strcpy(globalConfig.mqtt.force_ota_topic, MQTT_FORCE_OTA_TOPIC);
#endif
#ifdef HTTP_PORT
    globalConfig.http.port = HTTP_PORT;
#else
    globalConfig.http.port = 80;
#endif
#ifdef HTTP_USER
    strcpy(globalConfig.http.user, HTTP_USER);
#endif
#ifdef HTTP_PASS
    strcpy(globalConfig.http.pass, HTTP_PASS);
#endif
#ifdef OTA_URL
    strcpy(globalConfig.http.ota_url, OTA_URL);
#endif
    globalConfig.debug.type = 5;
    globalConfig.debug.seriallog_level = LOG_LEVEL_INFO;
    globalConfig.debug.weblog_level = LOG_LEVEL_INFO;
    globalConfig.debug.syslog_level = LOG_LEVEL_INFO;
    globalConfig.wifi.is_restart = true;

    Module *ptr = module;
    while (ptr != nullptr)
    {
        ptr->resetConfig();
        ptr = ptr->next;
    }
}

bool Config::doConfig(uint8_t *buf, uint8_t *data, uint16_t len, const char *name)
{
    bool status = false;
    if (len > 0 && (buf[0] << 8 | buf[1]) == GLOBAL_CFG_VERSION)
    {
        len = (buf[2] << 8 | buf[3]);
        nowCrc = (buf[4] << 8 | buf[5]);

        if (len > GlobalConfigMessage_size)
        {
            len = GlobalConfigMessage_size;
        }
        //Log::Info(PSTR("readConfig . . . %s Len: %d Crc: %d"), name, len, nowCrc);

        uint16_t crc = crc16(data, len);
        if (crc == nowCrc)
        {
            memset(&globalConfig, 0, sizeof(GlobalConfigMessage));
            pb_istream_t stream = pb_istream_from_buffer(data, len);
            status = pb_decode(&stream, GlobalConfigMessage_fields, &globalConfig);
            if (globalConfig.http.port == 0)
            {
                globalConfig.http.port = 80;
            }
            if (globalConfig.debug.seriallog_level == LOG_LEVEL_NONE && globalConfig.debug.weblog_level == LOG_LEVEL_NONE && globalConfig.debug.syslog_level == LOG_LEVEL_NONE)
            {
                globalConfig.debug.seriallog_level = LOG_LEVEL_INFO;
                globalConfig.debug.weblog_level = LOG_LEVEL_INFO;
                globalConfig.debug.syslog_level = LOG_LEVEL_INFO;
            }
            if (globalConfig.wifi.ssid[0] == '\0')
            {
#ifdef WIFI_SSID
                strcpy(globalConfig.wifi.ssid, WIFI_SSID);
#endif
#ifdef WIFI_PASS
                strcpy(globalConfig.wifi.pass, WIFI_PASS);
#endif
            }
        }
        else
        {
            Log::Error(PSTR("readConfig . . . %s Error Crc: %d Crc: %d"), name, crc, nowCrc);
        }
    }

    if (!status)
    {
        globalConfig.debug.type = 1;
        Log::Error(PSTR("resetConfig . . . %s."), name);
        resetConfig();
    }
    else
    {
        Log::Info(PSTR("readConfig . . . %s OK Len: %d"), name, len);
        Module *ptr = module;
        while (ptr != nullptr)
        {
            ptr->readConfig();
            ptr = ptr->next;
        }
        return true;
    }
    return false;
}

#ifdef USE_UFILESYS
bool Config::readFSConfig()
{
    if (!FileSystem::getFs() || !FileSystem::exists("/config"))
    {
        return false;
    }

    File file = FileSystem::getFs()->open("/config", "r");
    if (!file)
    {
        return false;
    }
    uint8_t buf[6] = {0};
    file.read(buf, 6);

    if ((buf[0] << 8 | buf[1]) == GLOBAL_CFG_VERSION)
    {
        uint16_t len = (buf[2] << 8 | buf[3]);
        uint8_t data[len];
        memset(data, 0, len);
        file.read(data, len);
        file.close();
        return doConfig(buf, data, len, PSTR("FS"));
    }
    else
    {
        file.close();
        return false;
    }
    return true;
}
#endif

void Config::readConfig()
{
#ifdef USE_UFILESYS
    if (readFSConfig())
    {
        return;
    }
#endif

    uint8_t buf[6] = {0};
#ifdef ESP8266
    if (spi_flash_read(EEPROM_PHYS_ADDR, (uint32 *)buf, 6) != SPI_FLASH_RESULT_OK)
#else
    if (!espconfig_spiflash_read(EEPROM_PHYS_ADDR, (uint32_t *)buf, 6))
#endif
    {
    }

    if ((buf[0] << 8 | buf[1]) == GLOBAL_CFG_VERSION)
    {
        uint16_t len = (buf[2] << 8 | buf[3]);
        uint8_t data[len];
        memset(data, 0, len);
#ifdef ESP8266
        if (spi_flash_read(EEPROM_PHYS_ADDR + 6, (uint32 *)data, len) == SPI_FLASH_RESULT_OK)
#else
        if (espconfig_spiflash_read(EEPROM_PHYS_ADDR + 6, (uint32_t *)data, len))
#endif
        {
            doConfig(buf, data, len, PSTR("EEPROM"));
        }
        else
        {
            doConfig(buf, nullptr, 0, PSTR("Default"));
        }
    }
    else
    {
        doConfig(buf, nullptr, 0, PSTR("Default"));
    }
}

bool Config::saveConfig(bool isEverySecond)
{
    countdown = 60;
    Module *ptr = module;
    while (ptr != nullptr)
    {
        ptr->saveConfig(isEverySecond);
        ptr = ptr->next;
    }

    uint8_t buffer[GlobalConfigMessage_size + 6];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer + 6, sizeof(buffer));
    bool status = pb_encode(&stream, GlobalConfigMessage_fields, &globalConfig);
    size_t len = stream.bytes_written;
    if (!status)
    {
        Log::Error(PSTR("saveConfig . . . Error"));
        return false;
    }
    else
    {
        uint16_t crc = crc16(buffer + 6, len);
        if (crc == nowCrc)
        {
            //Log::Info(PSTR("Check Config CRC . . . Same"));
            return true;
        }
        else
        {
            nowCrc = crc;
        }
    }

    // 拷贝数据
    buffer[0] = GLOBAL_CFG_VERSION >> 8;
    buffer[1] = GLOBAL_CFG_VERSION;

    buffer[2] = len >> 8;
    buffer[3] = len;

    buffer[4] = nowCrc >> 8;
    buffer[5] = nowCrc;

#ifdef USE_UFILESYS
    if (FileSystem::save("/config", buffer, len + 6))
    {
        Log::Info(PSTR("saveConfig . . . FS OK Len: %d Crc: %d"), len, nowCrc);
    }
    else
    {
        Log::Info(PSTR("saveConfig . . . FS Error"));
    }
#endif

    // 读取原来数据
    //uint8_t data[SPI_FLASH_SEC_SIZE] = {0};
    uint8_t *data = (uint8_t *)malloc(SPI_FLASH_SEC_SIZE);
#ifdef ESP8266
    if (spi_flash_read(EEPROM_PHYS_ADDR, (uint32 *)data, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
#else
    if (!espconfig_spiflash_read(EEPROM_PHYS_ADDR, (uint32_t *)data, SPI_FLASH_SEC_SIZE))
#endif
    {
        free(data);
        Log::Error(PSTR("saveConfig . . . EEPROM Read Data Error"));
        return false;
    }

    memcpy(&data[0], buffer, len + 6);

    // 擦写扇区
#ifdef ESP8266
    if (spi_flash_erase_sector(EEPROM_PHYS_ADDR / SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
#else
    if (!espconfig_spiflash_erase_sector(EEPROM_PHYS_ADDR / SPI_FLASH_SEC_SIZE))
#endif
    {
        free(data);
        Log::Error(PSTR("saveConfig . . . EEPROM Erase Sector Error"));
        return false;
    }

    // 写入数据
#ifdef ESP8266
    if (spi_flash_write(EEPROM_PHYS_ADDR, (uint32 *)data, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
#else
    if (!espconfig_spiflash_write(EEPROM_PHYS_ADDR, (uint32_t *)data, SPI_FLASH_SEC_SIZE))
#endif
    {
        free(data);
        Log::Error(PSTR("saveConfig . . . EEPROM Write Data Error"));
        return false;
    }
    free(data);

    Log::Info(PSTR("saveConfig . . . EEPROM OK Len: %d Crc: %d"), len, nowCrc);
    return true;
}

void Config::perSecondDo()
{
    countdown--;
    if (countdown == 0)
    {
        saveConfig(Config::isDelay ? false : true);
        Config::isDelay = false;
    }
    if (bitRead(Config::operationFlag, 1))
    {
        saveConfig(true);
        delay(200);
        Led::blinkLED(400, 4);
        ESP_Restart();
    }
}

bool Config::callModule(uint8_t function)
{
    switch (function)
    {
    case FUNC_EVERY_SECOND:
        perSecondDo();
        break;
    }
    return false;
}

void Config::delaySaveConfig(uint8_t second)
{
    Config::isDelay = true;
    if (countdown > second)
    {
        countdown = second;
    }
}

void Config::moduleReadConfig(uint16_t version, uint16_t size, const pb_field_t fields[], void *dest_struct)
{
    if (globalConfig.module_cfg.size == 0                                                                         // 没有数据
        || globalConfig.cfg_version != version                                                                    // 版本不一致
        || globalConfig.module_crc != Config::crc16(globalConfig.module_cfg.bytes, globalConfig.module_cfg.size)) // crc错误
    {
        Log::Error(PSTR("moduleReadConfig . . . Error %d %d %d"), globalConfig.cfg_version, version, globalConfig.module_cfg.size);
        if (module)
        {
            module->resetConfig();
        }
        return;
    }
    memset(dest_struct, 0, size);
    pb_istream_t stream = pb_istream_from_buffer(globalConfig.module_cfg.bytes, globalConfig.module_cfg.size);
    bool status = pb_decode(&stream, fields, dest_struct);
    if (!status) // 解密失败
    {
        if (module)
        {
            module->resetConfig();
        }
    }
    else
    {
        Log::Info(PSTR("moduleReadConfig . . . OK Len: %d"), globalConfig.module_cfg.size);
    }
}

bool Config::moduleSaveConfig(uint16_t version, uint16_t size, const pb_field_t fields[], const void *src_struct)
{
    uint8_t buffer[size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    bool status = pb_encode(&stream, fields, src_struct);
    if (status)
    {
        size_t len = stream.bytes_written;
        uint16_t crc = Config::crc16(buffer, len);
        if (crc != globalConfig.module_crc)
        {
            globalConfig.cfg_version = version;
            globalConfig.module_crc = crc;
            globalConfig.module_cfg.size = len;
            memcpy(globalConfig.module_cfg.bytes, buffer, len);
            //Log::Info(PSTR("moduleSaveConfig . . . OK Len: %d"), len);
        }
    }
    return status;
}

#ifdef USE_UFILESYS
bool Config::FSReadConfig(const char *fileName, uint16_t version, uint16_t size, const pb_field_t fields[], void *dest_struct, uint16_t &crc)
{
    if (!FileSystem::getFs())
    {
        Log::Error(PSTR("ReadConfig [%s] . . . FS No Support"), fileName + 1);
        return false;
    }
    if (!FileSystem::exists(fileName))
    {
        Log::Error(PSTR("ReadConfig [%s] . . . Not Found"), fileName + 1);
        return false;
    }

    File file = FileSystem::getFs()->open(fileName, "r");
    if (!file)
    {
        Log::Error(PSTR("ReadConfig [%s] . . . File Error"), fileName + 1);
        return false;
    }
    uint8_t buf[6] = {0};
    file.read(buf, 6);

    if ((buf[0] << 8 | buf[1]) != version)
    {
        file.close();
        Log::Error(PSTR("ReadConfig [%s] . . . Version Error"), fileName + 1);
        return false;
    }

    uint16_t len = (buf[2] << 8 | buf[3]);
    uint16_t nowCrc = (buf[4] << 8 | buf[5]);
    uint8_t data[len];
    memset(data, 0, len);
    file.read(data, len);
    file.close();

    crc = crc16(data, len);
    if (crc != nowCrc)
    {
        crc = 0;
        Log::Error(PSTR("ReadConfig [%s] . . . Error Crc: %d Crc: %d"), fileName + 1, crc, nowCrc);
        return false;
    }
    memset(dest_struct, 0, size);
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    bool status = pb_decode(&stream, fields, dest_struct);
    if (!status)
    {
        Log::Error(PSTR("ReadConfig [%s] . . . PB Error"), fileName + 1);
        return false;
    }
    Log::Info(PSTR("ReadConfig [%s] . . . OK Len: %d"), fileName + 1, len);
    return true;
}

bool Config::FSSaveConfig(const char *fileName, uint16_t version, uint16_t size, const pb_field_t fields[], const void *src_struct, uint16_t &crc)
{
    if (!FileSystem::getFs())
    {
        Log::Error(PSTR("ReadConfig [%s] . . . FS No Support"), fileName + 1);
        return false;
    }

    uint8_t buffer[size + 6];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer + 6, size);
    bool status = pb_encode(&stream, fields, src_struct);
    if (!status)
    {
        Log::Error(PSTR("SaveConfig [%s] . . . PB Error"), fileName + 1);
        return false;
    }

    size_t len = stream.bytes_written;
    uint16_t nowCrc = Config::crc16(buffer + 6, len);
    if (nowCrc != crc)
    {
        // 拷贝数据
        buffer[0] = version >> 8;
        buffer[1] = version;
        buffer[2] = len >> 8;
        buffer[3] = len;
        buffer[4] = nowCrc >> 8;
        buffer[5] = nowCrc;

        if (FileSystem::save(fileName, buffer, len + 6))
        {
            crc = nowCrc;
            Log::Info(PSTR("SaveConfig [%s] . . . OK Len: %d Crc: %d"), fileName + 1, len, nowCrc);
            return true;
        }
        else
        {
            Log::Error(PSTR("SaveConfig [%s] . . . FS Error"), fileName + 1);
            return false;
        }
    }
    return true;
}
#endif