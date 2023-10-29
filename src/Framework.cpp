#include "Framework.h"
#include "Module.h"
#include "Rtc.h"
#include "Http.h"
#include "Util.h"

uint8_t Framework::sleepTime = 50;
uint16_t Framework::rebootCount = 0;
uint32_t Framework::loopLoadAvg = 160;
bool Framework::sleepNormal = false;

#ifndef DISABLE_MQTT
#ifndef USE_ASYNC_MQTT_CLIENT
WiFiClient wifiClient;
#endif
void Framework::callback(char *topic, byte *payload, unsigned int length)
{
    Log::Info(PSTR("Subscribe: %s payload: %.*s"), topic, length, payload);

    char *cmnd = strrchr(topic, '/');
    if (cmnd == nullptr)
    {
        return;
    }
    cmnd++;
    payload[length] = 0;

    if (strcmp(cmnd, "ota") == 0)
    {
        String str = String((char *)payload);
        Http::OTA(str.endsWith(F(".bin")) ? str : OTA_URL);
    }
    else if (strcmp(cmnd, "restart") == 0)
    {
        ESP_Restart();
    }
    else if (module)
    {
        Module *ptr = module;
        while (ptr != nullptr)
        {
            ptr->mqttCallback(topic, (char *)payload, cmnd);
            ptr = ptr->next;
        }
    }

    Led::led(200);
}

void Framework::connectedCallback()
{
    Mqtt::subscribe(Mqtt::getCmndTopic(F("#")));
    Mqtt::subscribe(Mqtt::getForceOtaTopic(F("#")));
    Led::blinkLED(40, 8);

    callModule(FUNC_MQTT_CONNECTED);

    Module *ptr = module;
    while (ptr != nullptr)
    {
        ptr->mqttConnected();
        ptr = ptr->next;
    }
}
#endif

void Framework::tickerPerSecondDo()
{
    perSecond++;
    if (perSecond == 30)
    {
        Rtc::rtcReboot.fast_reboot_count = 0;
        Rtc::rtcRebootSave();
    }
    if (rebootCount >= 3)
    {
        return;
    }
    Rtc::addSecond();
    bitSet(Config::operationFlag, 0);
}

void Framework::one(unsigned long baud)
{
#ifdef ESP32
    DisableBrownout();
#endif

    Rtc::rtcRebootLoad();
    Rtc::rtcReboot.fast_reboot_count++;
    Rtc::rtcRebootSave();
    rebootCount = Rtc::rtcReboot.fast_reboot_count > BOOT_LOOP_OFFSET ? Rtc::rtcReboot.fast_reboot_count - BOOT_LOOP_OFFSET : 0;

    Serial.begin(baud);
    globalConfig.debug.type = 5;
    globalConfig.debug.seriallog_level = LOG_LEVEL_INFO;
    globalConfig.debug.weblog_level = LOG_LEVEL_INFO;

    addModule(WifiMgr::callModule);
    addModule(Http::callModule);

    if (rebootCount < 3)
    {
        addModule(Led::callModule);
        addModule(Rtc::callModule);
        addModule(Config::callModule);
#ifndef DISABLE_MQTT
        addModule(Mqtt::callModule);
#endif
    }
}

void Framework::setup()
{
    Log::Error(PSTR("---------------------  v%s  %s  %d-------------------"), module->getModuleVersion().c_str(), Rtc::GetBuildDateAndTime().c_str(), rebootCount);
#ifdef USE_UFILESYS
    FileSystem::init();
#endif
    if (rebootCount == 1 || rebootCount == 2)
    {
        Config::readConfig();
        Module *ptr = module;
        while (ptr != nullptr)
        {
            ptr->resetConfig();
            ptr = ptr->next;
        }
    }
    else if (rebootCount == 3)
    {
        Config::resetConfig();
    }
    else
    {
        Config::readConfig();
        if ((8 & globalConfig.debug.type) == 8)
        {
            Serial1.begin(115200);
        }
    }
    if (globalConfig.uid[0] != '\0')
    {
        strcpy(UID, globalConfig.uid);
    }
    else
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        sprintf(UID, "%s_%02x%02x%02x", module->getModuleName().c_str(), mac[3], mac[4], mac[5]);
    }
    Util::strlowr(UID);
    Log::Info(PSTR("UID: %s"), UID);

    WifiMgr::connectWifi();
    if (rebootCount >= 3)
    {
        module = NULL;
    }
    else
    {
#ifndef DISABLE_MQTT
#if !defined USE_ASYNC_MQTT_CLIENT and !defined USE_ESP32_MQTT
        wifiClient.setTimeout(200);
        Mqtt::setClient(wifiClient);
#endif
        Mqtt::mqttSetConnectedCallback(connectedCallback);
        Mqtt::mqttSetLoopCallback(callback);
#endif
        Module *ptr = module;
        while (ptr != nullptr)
        {
            ptr->init();
            ptr = ptr->next;
        }
        Rtc::init();
    }
    Http::init();
    if (module)
    {
        callModule(FUNC_INIT);
    }

    tickerPerSecond = new Ticker();
    tickerPerSecond->attach(1, tickerPerSecondDo);

#if defined(CONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH) && defined(USE_UFILESYS)
    CoreDumpToFile();
#endif
}

void Framework::sleepDelay(uint32_t mseconds)
{
    if (mseconds)
    {
        uint32_t wait = millis() + mseconds;
        while (!Util::timeReached(wait) && !Serial.available())
        {
            delay(1);
        }
    }
    else
    {
        delay(0);
    }
}

void Framework::loop()
{
    uint32_t my_sleep = millis();

    if (rebootCount >= 3)
    {
        WifiMgr::loop();
        Http::loop();
        return;
    }

    callModule(FUNC_LOOP);
    Module *ptr = module;
    while (ptr != nullptr)
    {
        ptr->loop();
        ptr = ptr->next;
    }

    static uint32_t state_50msecond = 0; // State 50msecond timer
    if (Util::timeReached(state_50msecond))
    {
        Util::setNextTimeInterval(state_50msecond, 50);
        callModule(FUNC_EVERY_50_MSECOND);
    }

    if (bitRead(Config::operationFlag, 0))
    {
        bitClear(Config::operationFlag, 0);
        callModule(FUNC_EVERY_SECOND);
        Module *ptr = module;
        while (ptr != nullptr)
        {
            ptr->perSecondDo();
            ptr = ptr->next;
        }
        // Log::Info("loopLoadAvg:%d", loopLoadAvg);
    }

    uint32_t my_activity = millis() - my_sleep;
    if (sleepNormal)
    {
        sleepDelay(sleepTime);
    }
    else
    {
        if (my_activity < (uint32_t)sleepTime)
        {
            sleepDelay((uint32_t)sleepTime - my_activity); // Provide time for background tasks like wifi
        }
        else
        {
            if (!bitRead(Config::statusFlag, 0) && !bitRead(Config::statusFlag, 2))
            {
                sleepDelay(my_activity / 2); // If wifi down and my_activity > setoption36 then force loop delay to 1/3 of my_activity period
            }
        }
    }

    if (!my_activity)
    {
        my_activity++;
    }
    uint32_t loop_delay = sleepTime;
    if (!loop_delay)
    {
        loop_delay++;
    }
    uint32_t loops_per_second = 1000 / loop_delay; // We need to keep track of this many loops per second
    uint32_t this_cycle_ratio = 100 * my_activity / loop_delay;
    loopLoadAvg = loopLoadAvg - (loopLoadAvg / loops_per_second) + (this_cycle_ratio / loops_per_second); // Take away one loop average away and add the new one
}
