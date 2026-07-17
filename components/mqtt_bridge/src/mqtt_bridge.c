#include "mqtt_bridge.h"
#include "esp_log.h"
#include <string.h>

#define MAX_DATA_CBS   4
#define MAX_PENDING_SUBS 8

static const char *TAG = "mqtt_bridge";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_bridge_connected_cb_t s_connected_cb = NULL;
static mqtt_bridge_data_cb_t s_data_cbs[MAX_DATA_CBS] = {NULL};
static int s_data_cb_count = 0;
static bool s_connected = false;

typedef struct {
    char topic[128];
    int qos;
} pending_sub_t;
static pending_sub_t s_pending_subs[MAX_PENDING_SUBS];
static int s_pending_sub_count = 0;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        for (int i = 0; i < s_pending_sub_count; i++) {
            esp_mqtt_client_subscribe(s_client, s_pending_subs[i].topic,
                                      s_pending_subs[i].qos);
            ESP_LOGI(TAG, "Subscribed: %s", s_pending_subs[i].topic);
        }
        s_pending_sub_count = 0;
        if (s_connected_cb) s_connected_cb();
        break;

    case MQTT_EVENT_DATA: {
        for (int i = 0; i < s_data_cb_count; i++) {
            if (s_data_cbs[i] && s_data_cbs[i](event->topic, event->topic_len,
                                                event->data, event->data_len))
                break;
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

void mqtt_bridge_init(const char *broker_url, const char *client_id,
                      const char *lwt_topic, const char *lwt_msg)
{
    if (s_client) return;

    esp_mqtt_client_config_t cfg = {
        .uri = broker_url,
        .client_id = client_id,
        .keepalive = 60,
    };

    if (lwt_topic) {
        cfg.lwt_topic = lwt_topic;
        cfg.lwt_msg = lwt_msg;
        cfg.lwt_qos = 1;
        cfg.lwt_retain = 1;
    }

    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", broker_url);
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

esp_mqtt_client_handle_t mqtt_bridge_get_client(void)
{
    return s_client;
}

int mqtt_bridge_publish(const char *topic, const char *payload, int retain)
{
    if (!s_client || !s_connected) {
        ESP_LOGW(TAG, "Publish skip (not connected): %s", topic);
        return -1;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, retain);
    if (msg_id < 0) ESP_LOGW(TAG, "Publish failed(%d): %s", msg_id, topic);
    else ESP_LOGI(TAG, "Published [%d]: %s", msg_id, topic);
    return msg_id;
}

int mqtt_bridge_subscribe(const char *topic, int qos)
{
    if (!s_client) return -1;
    if (!s_connected) {
        if (s_pending_sub_count < MAX_PENDING_SUBS) {
            strncpy(s_pending_subs[s_pending_sub_count].topic, topic, 127);
            s_pending_subs[s_pending_sub_count].topic[127] = '\0';
            s_pending_subs[s_pending_sub_count].qos = qos;
            s_pending_sub_count++;
        } else {
            ESP_LOGW(TAG, "Pending sub queue full: %s", topic);
        }
        return -1;
    }
    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    if (msg_id < 0) ESP_LOGW(TAG, "Subscribe failed: %s", topic);
    return msg_id;
}

void mqtt_bridge_set_connected_cb(mqtt_bridge_connected_cb_t cb)
{
    s_connected_cb = cb;
}

void mqtt_bridge_add_data_cb(mqtt_bridge_data_cb_t cb)
{
    if (s_data_cb_count < MAX_DATA_CBS) {
        s_data_cbs[s_data_cb_count++] = cb;
    } else {
        ESP_LOGE(TAG, "Max data callbacks (%d) reached", MAX_DATA_CBS);
    }
}

void mqtt_bridge_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
    }
}
