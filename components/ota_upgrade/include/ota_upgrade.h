/**
 * @file    ota_upgrade.h
 * @brief   OTA远程升级模块
 */

#ifndef OTA_UPGRADE_H
#define OTA_UPGRADE_H

#ifdef __cplusplus
extern "C" {
#endif

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
