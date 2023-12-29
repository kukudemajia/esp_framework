#include <WiFiClient.h>
#include <DNSServer.h>
#include <Ticker.h>
#include "Common.h"
#include "WifiMgr.h"
#include "Log.h"
#include "Http.h"

#ifdef ESP8266
WiFiEventHandler WifiMgr::STAGotIP;
//WiFiEventHandler WifiMgr::STADisconnected;
#endif
bool WifiMgr::isDHCP = true;

unsigned long WifiMgr::configPortalStart = 0;
unsigned long WifiMgr::disconnectTime = 0;
#ifdef WIFI_CONNECT_TIMEOUT
unsigned long WifiMgr::connectStart = 0;
#endif
bool WifiMgr::connect = false;
String WifiMgr::_ssid = "";
String WifiMgr::_pass = "";
DNSServer *WifiMgr::dnsServer;

#ifdef ESP32
#include "ETH.h"
void WifiMgr::wiFiEvent(WiFiEvent_t event)
{
    //Log::Info(PSTR("[WiFi-event] event: %d"), event);
    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
#ifdef WIFI_CONNECT_TIMEOUT
        connectStart = 0;
#endif
        bitSet(Config::statusFlag, 0);
        Log::Info(PSTR("WiFi connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        if (WiFi.getMode() == WIFI_MODE_STA && globalConfig.wifi.is_static && String(globalConfig.wifi.ip).equals(WiFi.localIP().toString()))
        {
            isDHCP = false;
        }
        break;
    case SYSTEM_EVENT_ETH_START:
        Log::Info(PSTR("ETH Started"));
        char hostx[30];
        sprintf(hostx, "%s_eth", UID);
        ETH.setHostname(hostx);
        break;
    case SYSTEM_EVENT_ETH_CONNECTED:
        Log::Info(PSTR("ETH Connected"));
        if (globalConfig.wifi.is_static)
        {
            isDHCP = false;
            IPAddress static_ip;
            IPAddress static_sn;
            IPAddress static_gw;
            static_ip.fromString(globalConfig.wifi.ip);
            static_sn.fromString(globalConfig.wifi.sn);
            static_gw.fromString(globalConfig.wifi.gw);
            Log::Info(PSTR("Custom STA IP/GW/Subnet: %s %s %s"), globalConfig.wifi.ip, globalConfig.wifi.sn, globalConfig.wifi.gw);
            ETH.config(static_ip, static_gw, static_sn, IPAddress(223, 5, 5, 5), IPAddress(114, 114, 114, 114));
        }
        break;
    case SYSTEM_EVENT_ETH_GOT_IP:
        Log::Info(PSTR("ETH MAC: %s, IPv4: %s, %dMbps"), ETH.macAddress().c_str(), ETH.localIP().toString().c_str(), ETH.linkSpeed());
        bitSet(Config::statusFlag, 2);
        WiFi.disconnect(true);
        WiFi.setAutoConnect(false);
        WiFi.mode(WIFI_OFF);
        break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
        Log::Info(PSTR("ETH Disconnected"));
        bitClear(Config::statusFlag, 2);
        break;
    case SYSTEM_EVENT_ETH_STOP:
        Log::Info(PSTR("ETH Stopped"));
        bitClear(Config::statusFlag, 2);
        break;
    default:
        break;
    }
}
#endif

void WifiMgr::connectWifi()
{
#ifdef FORCE_WIFI_SSID
    strcpy(globalConfig.wifi.ssid, FORCE_WIFI_SSID);
#endif
#ifdef FORCE_WIFI_PASS
    strcpy(globalConfig.wifi.pass, FORCE_WIFI_PASS);
#endif
    delay(50);
#ifdef ESP32
    WiFi.onEvent(WifiMgr::wiFiEvent);
#endif
    if (globalConfig.wifi.ssid[0] != '\0')
    {
        setupWifi();
    }
    else
    {
        setupWifiManager(true);
    }
}

void WifiMgr::setupWifi()
{
    WiFi.persistent(false); // Solve possible wifi init errors (re-add at 6.2.1.16 #4044, #4083)
    WiFi.disconnect(true);  // Delete SDK wifi config
    delay(200);
    WIFI_setHostname(UID);
    WiFi.mode(WIFI_STA);
    delay(100);
    if (!WiFi.getAutoConnect())
    {
        WiFi.setAutoConnect(true);
    }
    Log::Info(PSTR("Connecting to %s Wifi"), globalConfig.wifi.ssid);
#ifdef ESP8266
    STAGotIP = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
#ifdef WIFI_CONNECT_TIMEOUT
        connectStart = 0;
#endif
        if (!WiFi.localIP().isSet())
        {
            ESP_Restart();
        }
        bitSet(Config::statusFlag, 0);
        Log::Info(PSTR("WiFi1 connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        if (globalConfig.wifi.is_static && String(globalConfig.wifi.ip).equals(WiFi.localIP().toString()))
        {
            isDHCP = false;
        }
        /*
        STADisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
            if (connectStart == 0)
            {
                connectStart = millis() + WIFI_CONNECT_TIMEOUT * 1000;
            }
            Log::Info(PSTR("onStationModeDisconnected"));
            STADisconnected = NULL;
        });
        */
    });
#else
    WiFi.setSleep(false);
#endif
    if (globalConfig.wifi.is_static)
    {
        isDHCP = false;
        IPAddress static_ip;
        IPAddress static_sn;
        IPAddress static_gw;
        static_ip.fromString(globalConfig.wifi.ip);
        static_sn.fromString(globalConfig.wifi.sn);
        static_gw.fromString(globalConfig.wifi.gw);
        Log::Info(PSTR("Custom STA IP/GW/Subnet: %s %s %s"), globalConfig.wifi.ip, globalConfig.wifi.sn, globalConfig.wifi.gw);
        WiFi.config(static_ip, static_gw, static_sn);
    }

#ifdef WIFI_CONNECT_TIMEOUT
    connectStart = millis();
#endif
    WiFi.begin(globalConfig.wifi.ssid, globalConfig.wifi.pass);
}

void WifiMgr::setupWifiManager(bool resetSettings)
{
    if (bitRead(Config::statusFlag, 2))
    {
        return;
    }
    if (resetSettings)
    {
        //Log::Info(PSTR("WifiManager ResetSettings"));
        //Config::resetConfig();
        WiFi.disconnect(true);
    }
    //WiFi.setAutoConnect(true);
    //WiFi.setAutoReconnect(true);
    WIFI_setHostname(UID);

#ifdef ESP8266
    STAGotIP = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
        Log::Info(PSTR("WiFi2 connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    });
#endif

    configPortalStart = millis();
    if (WiFi.isConnected())
    {
        WiFi.mode(WIFI_AP_STA);
        Log::Info(PSTR("SET AP STA Mode"));
    }
    else
    {
        WiFi.persistent(false);
        WiFi.disconnect();
        WiFi.mode(WIFI_AP_STA);
        WiFi.persistent(true);
        Log::Info(PSTR("SET AP Mode"));
    }

    connect = false;
    WiFi.softAP(UID);
    delay(500);
    Log::Info(PSTR("AP IP address: %s"), WiFi.softAPIP().toString().c_str());

    dnsServer = new DNSServer();
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(53, "*", WiFi.softAPIP());
}

void WifiMgr::tryConnect(String ssid, String pass)
{
    _ssid = ssid;
    _pass = pass;
    connect = true;
}

void WifiMgr::perSecondDo()
{
    if (perSecond % 60 == 0)
    {
#ifdef ESP32
        if (ETH.localIP())
        {
            bitSet(Config::statusFlag, 2);
            if (bitRead(Config::statusFlag, 0))
            {
                WiFi.disconnect(true);
                WiFi.setAutoConnect(false);
                bitClear(Config::statusFlag, 0);
            }
            return;
        }
#endif
        if (WiFi.isConnected()) // 如果连接了。重置未连接时间
        {
            bitSet(Config::statusFlag, 0);
            disconnectTime = 0;
        }
        else
        {
            bitClear(Config::statusFlag, 0);
            if (configPortalStart == 0 && globalConfig.wifi.ssid[0] != '\0')
            {
                if (disconnectTime == 0)
                {
                    Log::Info(PSTR("Wifi disconnect"));
                }
                disconnectTime++;
                if (disconnectTime >= 10 && globalConfig.wifi.is_restart) // 10分钟未连上wifi则重启
                {
                    Log::Info(PSTR("Wifi reconnect TimeOut"));
                    ESP_Restart();
                }
                WiFi.begin(globalConfig.wifi.ssid, globalConfig.wifi.pass);
            }
        }
    }
}

void WifiMgr::loop()
{
#ifdef WIFI_CONNECT_TIMEOUT
    if (connectStart > 0 && millis() > connectStart + (WIFI_CONNECT_TIMEOUT * 1000))
    {
        connectStart = 0;
        if (!WiFi.isConnected())
        {
            setupWifiManager(false);
            return;
        }
    }
#endif
    if (configPortalStart == 0)
    {
        return;
    }
    else if (configPortalStart == 1)
    {
        ESP_Restart();
        return;
    }
    if (bitRead(Config::statusFlag, 2))
    {
        WiFi.disconnect(true);
        WiFi.setAutoConnect(false);
        WiFi.mode(WIFI_OFF);
        configPortalStart = 0;
        return;
    }
    dnsServer->processNextRequest();

    if (connect)
    {
        connect = false;
        Log::Info(PSTR("Connecting to new AP"));

        WiFi.begin(_ssid.c_str(), _pass.c_str());
        Log::Info(PSTR("Waiting for connection result with time out"));
    }

    if (_ssid.length() > 0 && WiFi.isConnected())
    {
        strcpy(globalConfig.wifi.ssid, _ssid.c_str());
        strcpy(globalConfig.wifi.pass, _pass.c_str());
        Config::saveConfig();

        //	为了使WEB获取到IP 2秒后才关闭AP
        Ticker *ticker = new Ticker();
        ticker->attach(3, []() {
#ifdef ESP8266
            WiFi.mode(WIFI_STA);
            Log::Info(PSTR("SET STA Mode"));
            ESP_Restart();
#else
                configPortalStart = 1;
#endif
        });

        Log::Info(PSTR("WiFi connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

        dnsServer->stop();
        configPortalStart = 0;
        _ssid = "";
        _pass = "";
        return;
    }

    // 检查是否超时
    if (globalConfig.wifi.is_restart && millis() > configPortalStart + (WIFI_PORTAL_TIMEOUT * 1000))
    {
        dnsServer->stop();
        configPortalStart = 0;
        _ssid = "";
        _pass = "";
        Log::Info(PSTR("startConfigPortal TimeOut"));
        if (WiFi.isConnected())
        {
            //	为了使WEB获取到IP 2秒后才关闭AP
            Ticker *ticker = new Ticker();
            ticker->attach(3, []() {
#ifdef ESP8266
                WiFi.mode(WIFI_STA);
                Log::Info(PSTR("SET STA Mode"));
                ESP_Restart();
#else
                    configPortalStart = 1;
#endif
            });
        }
        else
        {
            Log::Info(PSTR("Wifi failed to connect and hit timeout. Rebooting..."));
            delay(3000);
            ESP_Restart(); // 重置，可能进入深度睡眠状态
            delay(5000);
        }
    }
}

bool WifiMgr::callModule(uint8_t function)
{
    switch (function)
    {
    case FUNC_EVERY_SECOND:
        perSecondDo();
        break;
    case FUNC_LOOP:
        loop();
        break;
    }
    return false;
}

IPAddress WifiMgr::networkAddress()
{
#ifdef ESP32
    if (bitRead(Config::statusFlag, 2))
    {
        return ETH.localIP();
    }
#endif
    return WiFi.localIP();
}

bool WifiMgr::isIp(String str)
{
    int a, b, c, d;
    if ((sscanf(str.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4) && (a >= 0 && a <= 255) && (b >= 0 && b <= 255) && (c >= 0 && c <= 255) && (d >= 0 && d <= 255))
    {
        return true;
    }
    return false;
}