/**
 * @file    app_main.c
 * @brief   ESP8266 智能家居 MQTT 控制器
 *
 * 支持：PWM RGB灯、单色灯、WS2812灯带、开关/继电器
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "device_id.h"
#include "led_control.h"
#include "device_config.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "ota_upgrade.h"
#include "rssi_reporter.h"
#include "version.h"

static const char *TAG = "app_main";

/* 内存监控阈值 (字节) */
#define MEM_CRITICAL_THRESHOLD  8192   /* 低于8KB触发重启 */
#define MEM_WARNING_THRESHOLD   16384  /* 低于16KB打印警告 */

/* 重启原因NVS存储 */
#define NVS_NAMESPACE       "sys_status"
#define NVS_KEY_RST_REASON  "rst_reason"
#define NVS_KEY_RST_COUNT   "rst_count"
#define NVS_KEY_RST_TIME    "rst_time"

/* 重启原因代码 */
typedef enum {
    RST_REASON_UNKNOWN = 0,
    RST_REASON_POWER_ON,        /* 正常上电 */
    RST_REASON_SOFTWARE,        /* 软件重启 (esp_restart) */
    RST_REASON_TASK_WDT,        /* 任务看门狗超时 */
    RST_REASON_MEM_CRITICAL,    /* 内存耗尽 */
    RST_REASON_PANIC,           /* 异常/崩溃 */
    RST_REASON_INT_WDT,         /* 中断看门狗超时 */
} rst_reason_t;

/* 重启原因字符串 */
static const char* rst_reason_str[] = {
    "Unknown",
    "Power On",
    "Software Reset",
    "Task WDT Timeout",
    "Memory Critical",
    "Panic/Crash",
    "Interrupt WDT Timeout",
};

/**
 * @brief  记录重启原因到NVS
 */
static void save_restart_reason(rst_reason_t reason)
{
    /* 参数检查 */
    if (reason >= sizeof(rst_reason_str) / sizeof(rst_reason_str[0])) {
        reason = RST_REASON_UNKNOWN;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return;
    }

    /* 保存重启原因 */
    nvs_set_u8(handle, NVS_KEY_RST_REASON, (uint8_t)reason);

    /* 读取并递增重启次数 */
    uint32_t count = 0;
    nvs_get_u32(handle, NVS_KEY_RST_COUNT, &count);
    count++;
    nvs_set_u32(handle, NVS_KEY_RST_COUNT, count);

    /* 保存重启时间戳 (使用系统tick作为粗略时间) */
    uint32_t tick = xTaskGetTickCount();
    nvs_set_u32(handle, NVS_KEY_RST_TIME, tick);

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Restart reason saved: %s (count: %d)", rst_reason_str[reason], count);
}

/**
 * @brief  读取并打印上次重启原因
 */
static void print_last_restart_info(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No previous restart info found");
        return;
    }

    uint8_t reason = RST_REASON_UNKNOWN;
    uint32_t count = 0;
    uint32_t tick = 0;

    nvs_get_u8(handle, NVS_KEY_RST_REASON, &reason);
    nvs_get_u32(handle, NVS_KEY_RST_COUNT, &count);
    nvs_get_u32(handle, NVS_KEY_RST_TIME, &tick);

    nvs_close(handle);

    /* 防止数组越界 */
    uint8_t max_reason = sizeof(rst_reason_str) / sizeof(rst_reason_str[0]);
    if (reason >= max_reason) {
        reason = RST_REASON_UNKNOWN;
    }

    /* 打印重启历史 */
    ESP_LOGI(TAG, "--------------------------------------------");
    ESP_LOGI(TAG, "  Last Restart Info:");
    ESP_LOGI(TAG, "    Reason: %s", rst_reason_str[reason]);
    ESP_LOGI(TAG, "    Total restarts: %d", count);
    if (reason >= RST_REASON_TASK_WDT) {
        ESP_LOGW(TAG, "    WARNING: Abnormal restart detected!");
    }
    ESP_LOGI(TAG, "--------------------------------------------");
}

/**
 * @brief  检测当前重启原因
 */
static rst_reason_t detect_restart_reason(void)
{
    /* ESP8266重启原因 */
    rst_reason_t reason = RST_REASON_UNKNOWN;

    /* 检查是否有保存的重启原因 (由内存监控等设置) */
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t saved_reason = RST_REASON_UNKNOWN;
        if (nvs_get_u8(handle, NVS_KEY_RST_REASON, &saved_reason) == ESP_OK) {
            /* 防止数组越界 */
            uint8_t max_reason = sizeof(rst_reason_str) / sizeof(rst_reason_str[0]);
            if (saved_reason != RST_REASON_UNKNOWN && saved_reason < max_reason) {
                reason = (rst_reason_t)saved_reason;
            }
        }
        nvs_close(handle);

        /* 清除已读取的原因，避免下次误报 */
        if (reason != RST_REASON_UNKNOWN) {
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
                nvs_set_u8(handle, NVS_KEY_RST_REASON, RST_REASON_UNKNOWN);
                nvs_commit(handle);
                nvs_close(handle);
            }
        }
    }

    /* 如果没有保存的原因，使用ESP-IDF API获取 */
    if (reason == RST_REASON_UNKNOWN) {
        esp_reset_reason_t hw_reason = esp_reset_reason();
        switch (hw_reason) {
            case ESP_RST_POWERON:       /* 正常上电 */
                reason = RST_REASON_POWER_ON;
                break;
            case ESP_RST_SW:            /* 软件重启 */
            case ESP_RST_FAST_SW:       /* 快速重启 */
                reason = RST_REASON_SOFTWARE;
                break;
            case ESP_RST_TASK_WDT:      /* 任务看门狗 */
                reason = RST_REASON_TASK_WDT;
                break;
            case ESP_RST_INT_WDT:       /* 中断看门狗 */
            case ESP_RST_WDT:           /* 其他看门狗 */
                reason = RST_REASON_INT_WDT;
                break;
            case ESP_RST_PANIC:         /* 异常/崩溃 */
                reason = RST_REASON_PANIC;
                break;
            case ESP_RST_DEEPSLEEP:     /* 深度睡眠唤醒 */
            case ESP_RST_BROWNOUT:      /* 掉电复位 */
                reason = RST_REASON_POWER_ON;
                break;
            default:
                reason = RST_REASON_UNKNOWN;
                break;
        }
    }

    return reason;
}

/**
 * @brief  内存监控任务
 *
 * 定期检查可用内存，低于阈值时自动重启，防止内存耗尽导致系统崩溃
 */
static void memory_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Memory monitor task started");

    while (1) {
        /* 喂狗 */
        esp_task_wdt_reset();

        /* 获取可用堆内存 */
        size_t free_heap = esp_get_free_heap_size();

        /* 内存严重不足，自动重启 */
        if (free_heap < MEM_CRITICAL_THRESHOLD) {
            ESP_LOGE(TAG, "CRITICAL: Free heap %d bytes, below threshold %d, restarting...",
                     free_heap, MEM_CRITICAL_THRESHOLD);

            /* 记录重启原因 */
            save_restart_reason(RST_REASON_MEM_CRITICAL);

            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        /* 内存警告 */
        if (free_heap < MEM_WARNING_THRESHOLD) {
            ESP_LOGW(TAG, "WARNING: Low memory - Free heap: %d bytes", free_heap);
        }

        /* 每30秒检查一次 */
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP8266 Smart Home Controller");
    ESP_LOGI(TAG, "  Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "============================================");

    /* 1. 初始化NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. 检测并记录重启原因 */
    rst_reason_t rst_reason = detect_restart_reason();
    print_last_restart_info();
    save_restart_reason(rst_reason);

    /* 3. 初始化LED/开关控制 */
    led_control_init();

    /* 4. 初始化WiFi（阻塞） - 必须在device_id之前，因为需要MAC地址 */
    wifi_manager_init();

    /* 5. 初始化设备ID - WiFi连接后才能读取MAC地址 */
    device_id_init();

    /* 6. 初始化任务看门狗 (超时时间由sdkconfig中的CONFIG_ESP_TASK_WDT_TIMEOUT_S定义) */
    ret = esp_task_wdt_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Task Watchdog Timer initialized");
    } else {
        ESP_LOGW(TAG, "Failed to init TWDT: %d", ret);
    }

    /* 7. 初始化MQTT */
    mqtt_ha_init();

    /* 8. 初始化OTA */
    ota_upgrade_init();

    /* 9. 启动RSSI上报 */
    rssi_reporter_start();

    /* 10. 启动内存监控任务 */
    xTaskCreate(memory_monitor_task, "mem_monitor", 2048, NULL, 10, NULL);

    /* 打印设备能力 */
    device_caps_t caps = get_device_capabilities();
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  System ready!");
    ESP_LOGI(TAG, "  Device: %s", base_device_name);
    ESP_LOGI(TAG, "  Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  PWM RGB: %d", caps.pwm_rgb_count);
    ESP_LOGI(TAG, "  Single:  %d", caps.pwm_single_count);
    ESP_LOGI(TAG, "  WS2812:  %d", caps.ws2812_count);
    ESP_LOGI(TAG, "  Switch:  %d", caps.switch_count);
    ESP_LOGI(TAG, "============================================");
}
