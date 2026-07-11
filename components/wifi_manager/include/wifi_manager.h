/**
 * @file    wifi_manager.h
 * @brief   Wi-Fi管理模块
 *
 * 支持多凭据存储、SmartConfig配网、自动重连
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 事件位定义 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* 最大WiFi凭据数量 */
#define MAX_WIFI_CREDS 3

/**
 * @brief  初始化Wi-Fi管理器
 *
 * 阻塞函数，连接成功后才返回
 * 如果没有保存的凭据，自动进入SmartConfig配网
 */
void wifi_manager_init(void);

/**
 * @brief  获取Wi-Fi事件组句柄
 */
EventGroupHandle_t wifi_manager_get_event_group(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
