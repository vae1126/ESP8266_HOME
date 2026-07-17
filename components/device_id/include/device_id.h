#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <stddef.h>
#include "esp_err.h"

#define FIRMWARE_VERSION "1.0.0"

#define DEVICE_ID_LEN   13
#define DEVICE_NAME_LEN 64
#define TOPIC_LEN       128
#define MQTT_BROKER_URL_LEN 128

extern char base_device_id[DEVICE_ID_LEN];
extern char base_device_name[DEVICE_NAME_LEN];
extern char device_avail_topic[TOPIC_LEN];

extern char device_status_topic[TOPIC_LEN];
extern char device_command_topic[TOPIC_LEN];
extern char device_response_topic[TOPIC_LEN];

#if defined(CONFIG_DEVICE_TYPE_SWITCH)
extern char switch_config_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN];
extern char switch_command_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN];
extern char switch_state_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN];
#endif

void device_id_init(void);

char* device_id_get_mqtt_broker(char *url, size_t size);

esp_err_t device_id_set_mqtt_broker(const char *url);

#endif /* DEVICE_ID_H */
