#ifndef DISABLE_MQTT

#include "Mqtt.h"
#include "Module.h"
#include "Rtc.h"

#ifndef USE_ESP32_MQTT
PubSubClient Mqtt::mqttClient;
#endif
uint8_t Mqtt::operationFlag = 0;
uint32_t Mqtt::disconnectCounter = 0;    // 重连次数
uint32_t Mqtt::lastReconnectAttempt = 0; // 最后尝试重连时间
uint32_t Mqtt::kMqttReconnectTime = 30;  // 重新连接尝试之间的延迟（秒）
std::function<void()> Mqtt::connectedcallback = NULL;
#ifdef USE_ASYNC_MQTT_CLIENT
std::function<void(char *, uint8_t *, unsigned int)> Mqtt::callback = NULL;
char mqttWill[80] = {0};
#elif defined USE_ESP32_MQTT
PubSubClient Mqtt::mqttClient = NULL;
std::function<void(char *, uint8_t *, unsigned int)> Mqtt::callback = NULL;
char revTopic[100] = {0};
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        bitSet(Config::statusFlag, 1);
        Mqtt::availability();
        if (globalConfig.mqtt.interval > 0)
        {
            Mqtt::doReportInfo();
        }
        if (Mqtt::connectedcallback != NULL)
        {
            Mqtt::connectedcallback();
        }
        Log::Info(PSTR("mqtt connection successful"));
        break;
    case MQTT_EVENT_DISCONNECTED:
        bitClear(Config::statusFlag, 1);
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        Mqtt::disconnectCounter++;
        Mqtt::lastReconnectAttempt = millis();
        if (Mqtt::disconnectCounter > 1)
        {
            Log::Info(PSTR("mqtt reconnection"));
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        Log::Info(PSTR("MQTT_EVENT_PUBLISHED, msg_id=%d"), event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        strncpy(revTopic, event->topic, event->topic_len);
        revTopic[event->topic_len] = 0;
        Mqtt::callback(revTopic, (byte *)event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            Log::Info(PSTR("Last errno string (%s)"), strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        Log::Info(PSTR("Other event id:%d"), event->event_id);
        break;
    }
}
#endif

bool Mqtt::mqttConnect()
{
    if (!bitRead(Config::statusFlag, 0) && !bitRead(Config::statusFlag, 2))
    {
        Log::Info(PSTR("wifi disconnected"));
        return false;
    }
    if (globalConfig.mqtt.port == 0)
    {
        Log::Info(PSTR("no set mqtt info"));
        return false;
    }
#ifdef USE_ESP32_MQTT
    if (bitRead(Config::statusFlag, 1))
#else
    if (mqttClient.connected())
#endif
    {
        return true;
    }

    Mqtt::disconnectCounter = 0;
    Log::Info(PSTR("mqtt connect to %s:%d Broker"), globalConfig.mqtt.server, globalConfig.mqtt.port);
#ifdef USE_ASYNC_MQTT_CLIENT
    mqttClient.disconnect(true);
    mqttClient.setClientId(UID);
    mqttClient.setServer(globalConfig.mqtt.server, globalConfig.mqtt.port);
    mqttClient.setCredentials(globalConfig.mqtt.user, globalConfig.mqtt.pass);
    strcpy(mqttWill, getTeleTopic(F("availability")).c_str());
    mqttClient.setWill(mqttWill, 0, true, "offline", 7);
    mqttClient.connect();
#elif defined USE_ESP32_MQTT
    esp_log_level_set("*", ESP_LOG_NONE);
    char t[80];
    strcpy(t, getTeleTopic(F("availability")).c_str());
    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.client_id = UID;
    mqtt_cfg.host = globalConfig.mqtt.server;
    mqtt_cfg.port = globalConfig.mqtt.port;
    mqtt_cfg.username = globalConfig.mqtt.user;
    mqtt_cfg.password = globalConfig.mqtt.pass;
    mqtt_cfg.lwt_topic = t;
    mqtt_cfg.lwt_qos = 0;
    mqtt_cfg.lwt_retain = true;
    mqtt_cfg.lwt_msg = "offline";
    mqtt_cfg.disable_auto_reconnect = false;
    mqtt_cfg.transport = MQTT_TRANSPORT_OVER_TCP;
    mqtt_cfg.reconnect_timeout_ms = 30 * 1000;
#ifdef MQTT_KEEPALIVE
    mqtt_cfg.keepalive = MQTT_KEEPALIVE;
#else
    mqtt_cfg.keepalive = 30;
#endif
#ifdef MQTT_MAX_PACKET_SIZE
    mqtt_cfg.buffer_size = MQTT_MAX_PACKET_SIZE;
#endif
    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqttClient);
#else
    mqttClient.setServer(globalConfig.mqtt.server, globalConfig.mqtt.port);
    if (mqttClient.connect(UID, globalConfig.mqtt.user, globalConfig.mqtt.pass, getTeleTopic(F("availability")).c_str(), 0, true, "offline"))
    {
        bitSet(Config::statusFlag, 1);
        Log::Info(PSTR("mqtt connection successful"));
        availability();
        if (globalConfig.mqtt.interval > 0)
        {
            doReportInfo();
        }
        if (connectedcallback != NULL)
        {
            connectedcallback();
        }
    }
    else
    {
        Log::Info(PSTR("mqtt connection failed, rc=%d"), mqttClient.state());
    }
#endif

#ifdef USE_ESP32_MQTT
    return bitRead(Config::statusFlag, 1);
#else
    return mqttClient.connected();
#endif
}

void Mqtt::doReportInfo()
{
    char message[250];
    sprintf(message, PSTR("{\"uid\":\"%s\",\"ssid\":\"%s\",\"rssi\":\"%s\",\"version\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"freemem\":%d,\"uptime\":%lu,\"buildtime\":\"%s\"}"),
            UID, WiFi.SSID().c_str(), String(WiFi.RSSI()).c_str(), (module ? module->getModuleVersion().c_str() : PSTR("0")), WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(), ESP.getFreeHeap(), millis() / 1000, Rtc::GetBuildDateAndTime().c_str());
    //Log::Info(PSTR("%s"), message);
    publish(getTeleTopic(F("info")), message);
}

void Mqtt::availability()
{
    publish(getTeleTopic(F("availability")), "online", true);
}

void Mqtt::perSecondDo(void *parameter)
{
    if ((!bitRead(Config::statusFlag, 0) && !bitRead(Config::statusFlag, 2)) || globalConfig.mqtt.port == 0)
    {
        bitClear(Mqtt::operationFlag, 0);
#if USE_TASK_MQTT_CONNECT
        vTaskDelete(NULL);
#endif
        return;
    }

#ifdef USE_ESP32_MQTT
    if (!mqttClient)
    {
        Mqtt::mqttConnect();
    }
#else
    if (!mqttClient.connected())
    {
        bitClear(Config::statusFlag, 1);
        if (lastReconnectAttempt == 0 || millis() - lastReconnectAttempt > kMqttReconnectTime * 1000)
        {
            disconnectCounter++;
            lastReconnectAttempt = millis();
            if (mqttConnect())
            {
                lastReconnectAttempt = 0;
            }
            else
            {
                lastReconnectAttempt = millis();
            }
        }
    }
    else
    {
        bitSet(Config::statusFlag, 1);
    }
#endif

    if (bitRead(Config::statusFlag, 1))
    {
        if (globalConfig.mqtt.interval > 0 && (perSecond % globalConfig.mqtt.interval) == 0)
        {
            doReportInfo();
        }
        if (perSecond % 3609 == 0)
        {
            availability();
        }
    }

    bitClear(Mqtt::operationFlag, 0);
#if USE_TASK_MQTT_CONNECT
    vTaskDelete(NULL);
#endif
}

void Mqtt::loop()
{
    if ((!bitRead(Config::statusFlag, 0) && !bitRead(Config::statusFlag, 2)) || !bitRead(Config::statusFlag, 1))
    {
        return;
    }
#if !defined USE_ASYNC_MQTT_CLIENT and !defined USE_ESP32_MQTT
    if (mqttClient.loop())
    {
        bitSet(Config::statusFlag, 1);
    }
    else
    {
        bitClear(Config::statusFlag, 1);
    }
#endif
}

bool Mqtt::callModule(uint8_t function)
{
    switch (function)
    {
    case FUNC_EVERY_SECOND:
        if (bitRead(Mqtt::operationFlag, 0))
        {
            return false;
        }
        if (globalConfig.mqtt.port > 0)
        {
            bitSet(Mqtt::operationFlag, 0);
#if USE_TASK_MQTT_CONNECT
            xTaskCreateUniversal(perSecondDo, "perSecondDo", 4096, NULL, 2, NULL, CONFIG_ARDUINO_RUNNING_CORE == 1 ? 0 : 1);
#else
            perSecondDo(NULL);
#endif
        }
        break;
    case FUNC_LOOP:
        loop();
        break;
    }
    return false;
}

String Mqtt::getCmndTopic(String topic, String devType)
{
    return getTopic(0, topic, devType);
}

String Mqtt::getStatTopic(String topic, String devType)
{
    return getTopic(1, topic, devType);
}

String Mqtt::getTeleTopic(String topic, String devType)
{
    return getTopic(2, topic, devType);
}

String Mqtt::getForceOtaTopic(String topic, String devType)
{
    return getTopic(3, topic, devType);
}


void Mqtt::mqttSetLoopCallback(MQTT_CALLBACK_SIGNATURE)
{
#ifdef USE_ASYNC_MQTT_CLIENT
    Mqtt::callback = callback;
    mqttClient.onMessage([&](char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
                         { Mqtt::callback(topic, (byte *)payload, len); });
#elif defined USE_ESP32_MQTT
    Mqtt::callback = callback;
#else
    mqttClient.setCallback(callback);
#endif
}

void Mqtt::mqttSetConnectedCallback(MQTT_CONNECTED_CALLBACK_SIGNATURE)
{
    Mqtt::connectedcallback = connectedcallback;
#ifdef USE_ASYNC_MQTT_CLIENT
    mqttClient.onConnect([&](bool sessionPresent)
                         {
                             bitSet(Config::statusFlag, 1);
                             Log::Info(PSTR("successful client mqtt connection"));
                             availability();
                             if (globalConfig.mqtt.interval > 0)
                             {
                                 doReportInfo();
                             }
                             Mqtt::connectedcallback();
                         });
#endif
}

#if !defined USE_ASYNC_MQTT_CLIENT and !defined USE_ESP32_MQTT
PubSubClient &Mqtt::setClient(Client &client)
{
    return mqttClient.setClient(client);
}
#endif

bool Mqtt::publish(const char *topic, const char *payload, bool retained)
{
    if (!bitRead(Config::statusFlag, 1))
    {
        return false;
    }
#ifdef USE_ASYNC_MQTT_CLIENT
    return mqttClient.publish(topic, 0, retained, payload);
#elif defined USE_ESP32_MQTT
    return esp_mqtt_client_publish(mqttClient, topic, payload, 0, 0, retained ? 1 : 0);
#else
    return mqttClient.publish(topic, payload, retained);
#endif
}

bool Mqtt::publish(const char *topic, const uint8_t *payload, unsigned int plength, bool retained)
{
    if (!bitRead(Config::statusFlag, 1))
    {
        return false;
    }
#ifdef USE_ASYNC_MQTT_CLIENT
    return mqttClient.publish(topic, 0, retained, (const char *)payload, plength);
#elif defined USE_ESP32_MQTT
    return esp_mqtt_client_publish(mqttClient, topic, (const char *)payload, plength, 0, retained ? 1 : 0);
#else
    return mqttClient.publish(topic, payload, plength, retained);
#endif
}

bool Mqtt::subscribe(const char *topic, uint8_t qos)
{
    if (!bitRead(Config::statusFlag, 1))
    {
        return false;
    }
#ifdef USE_ESP32_MQTT
    return esp_mqtt_client_subscribe(mqttClient, topic, qos) != -1;
#else
    return mqttClient.subscribe(topic, qos);
#endif
}

bool Mqtt::unsubscribe(const char *topic)
{
    if (!bitRead(Config::statusFlag, 1))
    {
        return false;
    }
#ifdef USE_ESP32_MQTT
    return esp_mqtt_client_unsubscribe(mqttClient, topic) != -1;
#else
    return mqttClient.unsubscribe(topic);
#endif
}

String Mqtt::getTopic(uint8_t prefix, String subtopic, String devType)
{
    // 0: Cmnd  1:Stat 2:Tele 3:Force_Ota
    
    String fulltopic;
    if(prefix == 3)
    {
        fulltopic = String(globalConfig.mqtt.force_ota_topic);
    }
    else
    {
        fulltopic = String(globalConfig.mqtt.topic);
    }
    if ((0 == prefix) && (-1 == fulltopic.indexOf(F("%prefix%"))))
    {
        fulltopic += F("/%prefix%"); // Need prefix for commands to handle mqtt topic loops
    }
    fulltopic.replace(F("%prefix%"), (prefix == 0 ? F("cmnd") : ((prefix == 1 ? F("stat") : F("tele")))));
    fulltopic.replace(F("%hostname%"), UID);
    fulltopic.replace(F("%module%"), module ? module->getModuleName() : F("module"));
    fulltopic.replace(F("#"), F(""));
    fulltopic.replace(F("//"), F("/"));
    if (!fulltopic.endsWith(F("/")))
        fulltopic += F("/");
    return fulltopic + subtopic;
}
#endif