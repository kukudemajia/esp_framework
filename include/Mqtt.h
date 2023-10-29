// Mqtt.h
#ifndef DISABLE_MQTT

#ifndef _MQTT_h
#define _MQTT_h

#include "Arduino.h"

#if CONFIG_IDF_TARGET_ESP32
#define USE_ESP32_MQTT
#endif

#ifdef USE_ASYNC_MQTT_CLIENT
#include <AsyncMqttClient.h>
#define PubSubClient AsyncMqttClient
#define MQTT_CALLBACK_SIGNATURE std::function<void(char *, uint8_t *, unsigned int)> callback
#elif defined USE_ESP32_MQTT
#include <functional>
#include "mqtt_client.h"
#define PubSubClient esp_mqtt_client_handle_t 
#define MQTT_CALLBACK_SIGNATURE std::function<void(char *, uint8_t *, unsigned int)> callback
#else
#include <PubSubClient.h>
#if CONFIG_IDF_TARGET_ESP32 && (!defined USE_TASK_MQTT_CONNECT)
#define USE_TASK_MQTT_CONNECT 1
#endif

#endif

#define MQTT_CONNECTED_CALLBACK_SIGNATURE std::function<void()> connectedcallback

class Mqtt
{
protected:
    static String getTopic(uint8_t prefix, String subtopic, String devType = "");
    static uint8_t operationFlag;

public:
    static PubSubClient mqttClient;
    static MQTT_CONNECTED_CALLBACK_SIGNATURE;
#ifdef USE_ASYNC_MQTT_CLIENT
    static MQTT_CALLBACK_SIGNATURE;
    static char mqttwill[80];
#elif defined USE_ESP32_MQTT
    static MQTT_CALLBACK_SIGNATURE;
#endif

    static uint32_t disconnectCounter; // 重连次数
    static uint32_t lastReconnectAttempt; // 最后尝试重连时间
    static uint32_t kMqttReconnectTime;   // 重新连接尝试之间的延迟（秒）

    static bool mqttConnect();
    static void availability();
    static void doReportInfo();
    static void mqttSetLoopCallback(MQTT_CALLBACK_SIGNATURE);
    static void mqttSetConnectedCallback(MQTT_CONNECTED_CALLBACK_SIGNATURE);

    static String getCmndTopic(String topic, String devType = "");
    static String getStatTopic(String topic, String devType = "");
    static String getTeleTopic(String topic, String devType = "");
    static String getForceOtaTopic(String topic, String devType = "");

#if !defined USE_ASYNC_MQTT_CLIENT and !defined USE_ESP32_MQTT
    static PubSubClient &setClient(Client &client);
#endif

    static bool publish(String topic, const char *payload, bool retained = false) { return publish(topic.c_str(), payload, retained); };
    static bool publish(const char *topic, const char *payload, bool retained = false);
    static bool publish(const char *topic, const uint8_t *payload, unsigned int plength, bool retained = false);

    static bool subscribe(const char *topic, uint8_t qos = 0);
    static bool subscribe(String topic, uint8_t qos = 0) { return subscribe(topic.c_str(), qos); };
    static bool unsubscribe(const char *topic);
    static bool unsubscribe(String topic) { return unsubscribe(topic.c_str()); };

    static void perSecondDo(void *parameter);
    static void loop();
    static bool callModule(uint8_t function);
};

#endif

#endif