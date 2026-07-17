#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include "mqtt_client.h"

typedef bool (*mqtt_bridge_data_cb_t)(const char *topic, int topic_len,
                                       const char *data, int data_len);

typedef void (*mqtt_bridge_connected_cb_t)(void);

void mqtt_bridge_init(const char *broker_url, const char *client_id,
                      const char *lwt_topic, const char *lwt_msg);

esp_mqtt_client_handle_t mqtt_bridge_get_client(void);

int  mqtt_bridge_publish(const char *topic, const char *payload, int retain);
int  mqtt_bridge_subscribe(const char *topic, int qos);

void mqtt_bridge_set_connected_cb(mqtt_bridge_connected_cb_t cb);
void mqtt_bridge_add_data_cb(mqtt_bridge_data_cb_t cb);

void mqtt_bridge_stop(void);

#endif
