/**
 * @file    ota_upgrade.h
 * @brief   OTA远程升级模块
 */

#ifndef OTA_UPGRADE_H
#define OTA_UPGRADE_H

#include "device_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OTA通知主题（用于MQTT消息匹配） */
extern char ota_notify_topic[TOPIC_LEN];

/**
 * @brief  初始化OTA升级模块
 */
esp_err_t ota_upgrade_init(void);

/**
 * @brief  订阅OTA主题
 *
 * 在MQTT连接成功后调用
 */
void ota_upgrade_subscribe(void);

/**
 * @brief  处理OTA命令
 *
 * @param data      命令数据（JSON格式，包含url字段）
 * @param data_len  数据长度
 */
void ota_upgrade_handle_command(const char *data, int data_len);

/**
 * @brief  获取OTA状态
 *
 * @return 0=idle, 1=downloading, 2=success, 3=failed
 */
int ota_upgrade_get_status(void);

/**
 * @brief  获取OTA进度 (0-100)
 */
int ota_upgrade_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_UPGRADE_H
