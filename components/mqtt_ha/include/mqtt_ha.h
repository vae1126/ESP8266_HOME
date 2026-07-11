/**
 * @file    mqtt_ha.h
 * @brief   MQTT + Home Assistant集成模块
 */

#ifndef MQTT_HA_H
#define MQTT_HA_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化MQTT客户端
 */
void mqtt_ha_init(void);

/**
 * @brief  发布MQTT消息
 */
void mqtt_ha_publish(const char *topic, const char *payload, int retain);

/**
 * @brief  获取MQTT客户端句柄
 */
esp_mqtt_client_handle_t mqtt_ha_get_client(void);

/**
 * @brief  上报设备状态
 */
void mqtt_ha_report_device_status(void);

/**
 * @brief  重新发布所有发现配置
 */
void mqtt_ha_publish_discovery(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_HA_H
