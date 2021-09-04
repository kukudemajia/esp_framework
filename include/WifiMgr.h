// Wifi.h

#ifndef _WIFIMGR_h
#define _WIFIMGR_h

#include <WiFiClient.h>
#include <DNSServer.h>
#include "Arduino.h"

//#define WIFI_CONNECT_TIMEOUT 300
#ifndef WIFI_PORTAL_TIMEOUT
#define WIFI_PORTAL_TIMEOUT 300
#endif
#define MinimumWifiSignalQuality 8

class WifiMgr
{
private:
    static bool connect;

    static String _ssid;
    static String _pass;

    static DNSServer *dnsServer;
#ifdef WIFI_CONNECT_TIMEOUT
    static unsigned long connectStart;
#endif

public:
    static unsigned long configPortalStart;
    static unsigned long disconnectTime;
    static bool isDHCP;
#ifdef ESP8266
    static WiFiEventHandler STAGotIP;
//static WiFiEventHandler STADisconnected;
#else
    static void wiFiEvent(WiFiEvent_t event);
#endif
    static void connectWifi();
    static void setupWifi();
    static void setupWifiManager(bool resetSettings);
    static bool isIp(String str);

    static uint8_t waitForConnectResult();
    static void tryConnect(String ssid, String pass);

    static void perSecondDo();
    static void loop();
    static bool callModule(uint8_t function);
    static IPAddress networkAddress();
};

#endif
