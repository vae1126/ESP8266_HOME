/**
 * @file    device_id.h
 * @brief   设备ID和MQTT主题定义
 *
 * 基于MAC地址生成唯一设备ID，构建所有MQTT主题
 */

#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备ID长度 (12字符MAC + null) */
#define DEVICE_ID_LEN 13

/* 主题长度 */
#define TOPIC_LEN 128

/* 设备名称长度 */
#define DEVICE_NAME_LEN 64

/* 全局变量 - 其他模块可直接读取 */
extern char base_device_id[DEVICE_ID_LEN];
extern char base_device_name[DEVICE_NAME_LEN];

/* 共享主题 */
extern char device_avail_topic[TOPIC_LEN];
extern char rssi_config_topic[TOPIC_LEN];
extern char rssi_state_topic[TOPIC_LEN];
extern char device_status_topic[TOPIC_LEN];
extern char device_command_topic[TOPIC_LEN];
extern char device_response_topic[TOPIC_LEN];

/**
 * @brief  初始化设备ID
 *
 * 读取MAC地址，生成设备ID和所有MQTT主题
 */
void device_id_init(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_ID_H
