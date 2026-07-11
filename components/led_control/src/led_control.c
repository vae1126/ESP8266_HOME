/**
 * @file    led_control.c
 * @brief   统一LED控制模块实现 (ESP8266 修复版)
 *
 * 修复内容：
 * 1. 合并所有PWM通道一次性初始化，解决PWM与开关GPIO冲突导致灯关不掉的问题
 * 2. 修复低电平有效(Active Low)设备关灯时占空比反转的Bug
 * 3. 增加NVS防抖写入机制，防止高频开关烧毁Flash
 *
 * 支持4种设备类型，每种支持多实例：
 *   - PWM RGB 灯：3个GPIO（R/G/B），支持颜色和亮度调节
 *   - PWM 单色灯：1个GPIO，支持亮度调节
 *   - WS2812 灯带：1个GPIO，支持逐像素RGB控制
 *   - 继电器/开关：1个GPIO，仅开关控制
 */

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/pwm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_control.h"
#include "device_config.h"
#include "esp_timer.h"

static const char *TAG = "led_ctrl";

/* ==========================================================================
 * 常量定义
 * ========================================================================== */

#define NVS_NAMESPACE "led_state"
#define MAX_PWM_RGB    2   /* ESP8266最多2个RGB灯(6个PWM通道) */
#define MAX_PWM_SINGLE 8   /* 最多8个单色灯 */
#define MAX_WS2812    2   /* 最多2条WS2812灯带 */
#define MAX_SWITCH    8   /* 最多8个开关 */

/* PWM参数 */
#define PWM_PERIOD 1000   /* 1kHz */

/* WS2812时序参数 (ESP8266 160MHz, 1周期=6.25ns) */
#define WS2812_T0H_CYCLES 56   /* 350ns / 6.25ns = 56 */
#define WS2812_T0L_CYCLES 112  /* 700ns / 6.25ns = 112 */
#define WS2812_T1H_CYCLES 112  /* 700ns / 6.25ns = 112 */
#define WS2812_T1L_CYCLES 56   /* 350ns / 6.25ns = 56 */
#define WS2812_RESET_US 60     /* 复位时间 >50us */

/* NVS 防抖保存延时 (毫秒) */
#define NVS_SAVE_DELAY_MS 1000

/* ==========================================================================
 * 状态存储
 * ========================================================================== */

/* PWM RGB灯状态 */
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
static led_state_t rgb_states[MAX_PWM_RGB] = {0};
static uint32_t rgb_r_duties[MAX_PWM_RGB] = {0};
static uint32_t rgb_g_duties[MAX_PWM_RGB] = {0};
static uint32_t rgb_b_duties[MAX_PWM_RGB] = {0};
#endif

/* 单色灯状态 */
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
static led_state_t single_states[MAX_PWM_SINGLE] = {0};
static uint32_t single_duties[MAX_PWM_SINGLE] = {0};
#endif

/* WS2812状态 */
#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
static led_state_t ws2812_states[MAX_WS2812] = {0};
#define WS2812_MAX_LEDS 256
static uint8_t ws2812_pixels[MAX_WS2812][WS2812_MAX_LEDS * 3] = {0};
#endif

/* 开关状态 */
#ifdef CONFIG_DEVICE_TYPE_SWITCH
static bool switch_states[MAX_SWITCH] = {false};
#endif

/* NVS 防抖定时器句柄 */
static esp_timer_handle_t nvs_save_timer = NULL;

/* ==========================================================================
 * NVS 持久化
 * ========================================================================== */

static void save_state_blob(const char *prefix, int index, const void *data, size_t size)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    char key[20];
    snprintf(key, sizeof(key), "%s_%d", prefix, index);
    nvs_set_blob(handle, key, data, size);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool load_state_blob(const char *prefix, int index, void *data, size_t size)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    char key[20];
    snprintf(key, sizeof(key), "%s_%d", prefix, index);
    size_t loaded = size;
    esp_err_t err = nvs_get_blob(handle, key, data, &loaded);
    nvs_close(handle);
    return (err == ESP_OK && loaded == size);
}

#ifdef CONFIG_DEVICE_TYPE_SWITCH
static void save_switch_state(int index, bool state)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    char key[20];
    snprintf(key, sizeof(key), "sw_%d", index);
    nvs_set_u8(handle, key, state ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool load_switch_state(int index)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    char key[20];
    snprintf(key, sizeof(key), "sw_%d", index);
    uint8_t val = 0;
    nvs_get_u8(handle, key, &val);
    nvs_close(handle);
    return (val != 0);
}
#endif

static void load_all_states(void)
{
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    for (int i = 0; i < CONFIG_PWM_RGB_COUNT && i < MAX_PWM_RGB; i++) {
        if (!load_state_blob("rgb", i, &rgb_states[i], sizeof(led_state_t))) {
            rgb_states[i] = (led_state_t){.state=false, .brightness=255, .red=255, .green=255, .blue=255};
        }
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    for (int i = 0; i < CONFIG_PWM_SINGLE_COUNT && i < MAX_PWM_SINGLE; i++) {
        if (!load_state_blob("s", i, &single_states[i], sizeof(led_state_t))) {
            single_states[i] = (led_state_t){.state=false, .brightness=255, .red=255, .green=255, .blue=255};
        }
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
    for (int i = 0; i < CONFIG_WS2812_COUNT && i < MAX_WS2812; i++) {
        if (!load_state_blob("ws", i, &ws2812_states[i], sizeof(led_state_t))) {
            ws2812_states[i] = (led_state_t){.state=false, .brightness=255, .red=255, .green=255, .blue=255};
        }
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    for (int i = 0; i < CONFIG_SWITCH_COUNT && i < MAX_SWITCH; i++) {
        switch_states[i] = load_switch_state(i);
    }
#endif
    ESP_LOGI(TAG, "All states loaded from NVS");
}

/* ==========================================================================
 * NVS 防抖保存机制 (防止高频操作烧毁Flash)
 * ========================================================================== */

static void nvs_save_task(void *arg)
{
    ESP_LOGI(TAG, "NVS debounce timer triggered, saving all states...");
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    for (int i = 0; i < CONFIG_PWM_RGB_COUNT && i < MAX_PWM_RGB; i++) {
        save_state_blob("rgb", i, &rgb_states[i], sizeof(led_state_t));
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    for (int i = 0; i < CONFIG_PWM_SINGLE_COUNT && i < MAX_PWM_SINGLE; i++) {
        save_state_blob("s", i, &single_states[i], sizeof(led_state_t));
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
    for (int i = 0; i < CONFIG_WS2812_COUNT && i < MAX_WS2812; i++) {
        save_state_blob("ws", i, &ws2812_states[i], sizeof(led_state_t));
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    for (int i = 0; i < CONFIG_SWITCH_COUNT && i < MAX_SWITCH; i++) {
        save_switch_state(i, switch_states[i]);
    }
#endif
}

static void trigger_nvs_save(void)
{
    if (nvs_save_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &nvs_save_task,
            .name = "nvs_save"
        };
        esp_err_t err = esp_timer_create(&timer_args, &nvs_save_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create NVS save timer: %d, saving immediately", err);
            nvs_save_task(NULL);
            return;
        }
    }
    // 重置定时器，只有在停止操作1秒后才会真正写入Flash
    esp_timer_stop(nvs_save_timer);
    esp_timer_start_once(nvs_save_timer, NVS_SAVE_DELAY_MS * 1000);
}

/* ==========================================================================
 * WS2812 位操作驱动
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812

static void IRAM_ATTR ws2812_send_byte(uint8_t gpio, uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        if (data & (1 << i)) {
            /* 发送1 */
            GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDR, 1 << gpio);
            __asm__ __volatile__("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop");
            GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDR, 1 << gpio);
            __asm__ __volatile__("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop");
        } else {
            /* 发送0 */
            GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDR, 1 << gpio);
            __asm__ __volatile__("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop");
            GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDR, 1 << gpio);
            __asm__ __volatile__("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
                                 "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop");
        }
    }
}

static void ws2812_refresh(int index)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    if (cfg.gpio < 0 || cfg.num_leds <= 0) return;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    for (int i = 0; i < cfg.num_leds && i < WS2812_MAX_LEDS; i++) {
        int offset = i * 3;
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset + 1]);  /* G */
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset]);      /* R */
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset + 2]);  /* B */
    }

    taskEXIT_CRITICAL(&mux);

    ets_delay_us(WS2812_RESET_US);
}

static void ws2812_set_pixel(int index, int pixel, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    if (pixel < 0 || pixel >= cfg.num_leds || pixel >= WS2812_MAX_LEDS) return;

    int offset = pixel * 3;
    ws2812_pixels[index][offset] = r;
    ws2812_pixels[index][offset + 1] = g;
    ws2812_pixels[index][offset + 2] = b;
}

static void ws2812_clear(int index)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    memset(ws2812_pixels[index], 0, cfg.num_leds * 3);
    ws2812_refresh(index);
}
#endif

/* ==========================================================================
 * 统一硬件初始化 (修复PWM与GPIO冲突)
 * ========================================================================== */

static void hardware_init(void)
{
    /* 1. 收集所有PWM通道，一次性初始化，防止多次pwm_init互相覆盖 */
    uint32_t all_gpios[MAX_PWM_RGB * 3 + MAX_PWM_SINGLE];
    uint32_t all_duties[MAX_PWM_RGB * 3 + MAX_PWM_SINGLE];
    int channel = 0;

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    int rgb_count = CONFIG_PWM_RGB_COUNT;
    if (rgb_count > MAX_PWM_RGB) rgb_count = MAX_PWM_RGB;

    for (int i = 0; i < rgb_count; i++) {
        pwm_rgb_config_t cfg = get_pwm_rgb_config(i);

        if (rgb_states[i].state) {
            uint8_t r = (rgb_states[i].red * rgb_states[i].brightness) / 255;
            uint8_t g = (rgb_states[i].green * rgb_states[i].brightness) / 255;
            uint8_t b = (rgb_states[i].blue * rgb_states[i].brightness) / 255;
            rgb_r_duties[i] = cfg.active_high ? (r * PWM_PERIOD / 255) : (PWM_PERIOD - r * PWM_PERIOD / 255);
            rgb_g_duties[i] = cfg.active_high ? (g * PWM_PERIOD / 255) : (PWM_PERIOD - g * PWM_PERIOD / 255);
            rgb_b_duties[i] = cfg.active_high ? (b * PWM_PERIOD / 255) : (PWM_PERIOD - b * PWM_PERIOD / 255);
        } else {
            rgb_r_duties[i] = 0;
            rgb_g_duties[i] = 0;
            rgb_b_duties[i] = 0;
        }

        all_gpios[channel] = cfg.r_gpio; all_duties[channel++] = rgb_r_duties[i];
        all_gpios[channel] = cfg.g_gpio; all_duties[channel++] = rgb_g_duties[i];
        all_gpios[channel] = cfg.b_gpio; all_duties[channel++] = rgb_b_duties[i];

        ESP_LOGI(TAG, "PWM RGB[%d]: R=%d G=%d B=%d active_%s duty=%d,%d,%d",
                 i, cfg.r_gpio, cfg.g_gpio, cfg.b_gpio,
                 cfg.active_high ? "high" : "low",
                 rgb_r_duties[i], rgb_g_duties[i], rgb_b_duties[i]);
    }
#endif

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    int single_count = CONFIG_PWM_SINGLE_COUNT;
    if (single_count > MAX_PWM_SINGLE) single_count = MAX_PWM_SINGLE;

    for (int i = 0; i < single_count; i++) {
        pwm_single_config_t cfg = get_pwm_single_config(i);
        all_gpios[channel] = cfg.gpio;

        if (single_states[i].state) {
            uint32_t brightness_duty = single_states[i].brightness * PWM_PERIOD / 255;
            all_duties[channel] = cfg.active_high ? brightness_duty : (PWM_PERIOD - brightness_duty);
        } else {
            // 关灯：高电平有效输出低电平(duty=0)，低电平有效输出高电平(duty=PWM_PERIOD)
            all_duties[channel] = cfg.active_high ? 0 : PWM_PERIOD;
        }
        single_duties[i] = all_duties[channel];
        channel++;

        ESP_LOGI(TAG, "PWM Single[%d]: GPIO %d duty %d", i, cfg.gpio, single_duties[i]);
    }
#endif

    if (channel > 0) {
        pwm_init(PWM_PERIOD, all_duties, channel, all_gpios);
        pwm_start();
    }

    /* 2. 初始化 WS2812 */
#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
    int ws_count = CONFIG_WS2812_COUNT;
    if (ws_count > MAX_WS2812) ws_count = MAX_WS2812;

    for (int i = 0; i < ws_count; i++) {
        ws2812_config_t cfg = get_ws2812_config(i);
        if (cfg.gpio < 0) continue;

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg.gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(cfg.gpio, 0);

        if (ws2812_states[i].state) {
            uint8_t r = (ws2812_states[i].red * ws2812_states[i].brightness) / 255;
            uint8_t g = (ws2812_states[i].green * ws2812_states[i].brightness) / 255;
            uint8_t b = (ws2812_states[i].blue * ws2812_states[i].brightness) / 255;
            for (int j = 0; j < cfg.num_leds && j < WS2812_MAX_LEDS; j++) {
                ws2812_set_pixel(i, j, r, g, b);
            }
            ws2812_refresh(i);
        } else {
            ws2812_clear(i);
        }
        ESP_LOGI(TAG, "WS2812[%d]: GPIO %d, %d LEDs", i, cfg.gpio, cfg.num_leds);
    }
#endif

    /* 3. 初始化 开关/继电器 */
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    for (int i = 0; i < CONFIG_SWITCH_COUNT && i < MAX_SWITCH; i++) {
        switch_config_t cfg = get_switch_config(i);
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg.gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        
        // 明确设置初始电平
        int level = switch_states[i] ? (cfg.active_high ? 1 : 0) : (cfg.active_high ? 0 : 1);
        gpio_set_level(cfg.gpio, level);
        
        ESP_LOGI(TAG, "Switch[%d]: GPIO %d, Level %d", i, cfg.gpio, level);
    }
#endif
}

/* ==========================================================================
 * 公开 API - PWM RGB 灯
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
void led_pwm_rgb_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_PWM_RGB || !state) return;

    pwm_rgb_config_t cfg = get_pwm_rgb_config(index);
    rgb_states[index] = *state;

    uint8_t r, g, b;
    if (state->state) {
        r = (state->red * state->brightness) / 255;
        g = (state->green * state->brightness) / 255;
        b = (state->blue * state->brightness) / 255;
    } else {
        r = g = b = 0;
    }

    if (cfg.active_high) {
        rgb_r_duties[index] = r * PWM_PERIOD / 255;
        rgb_g_duties[index] = g * PWM_PERIOD / 255;
        rgb_b_duties[index] = b * PWM_PERIOD / 255;
    } else {
        rgb_r_duties[index] = PWM_PERIOD - r * PWM_PERIOD / 255;
        rgb_g_duties[index] = PWM_PERIOD - g * PWM_PERIOD / 255;
        rgb_b_duties[index] = PWM_PERIOD - b * PWM_PERIOD / 255;
    }

    pwm_set_duty(index * 3, rgb_r_duties[index]);
    pwm_set_duty(index * 3 + 1, rgb_g_duties[index]);
    pwm_set_duty(index * 3 + 2, rgb_b_duties[index]);
    pwm_start();

    trigger_nvs_save(); // 替换原有的直接保存

    ESP_LOGI(TAG, "RGB %d -> %s brightness %d color %d,%d,%d",
             index, state->state ? "ON" : "OFF", state->brightness,
             state->red, state->green, state->blue);
}

led_state_t* led_pwm_rgb_get_state(int index)
{
    if (index < 0 || index >= MAX_PWM_RGB) return NULL;
    return &rgb_states[index];
}
#else
void led_pwm_rgb_set_state(int index, const led_state_t *state) {}
led_state_t* led_pwm_rgb_get_state(int index) { return NULL; }
#endif

/* ==========================================================================
 * 公开 API - 单色灯 (修复开关灯逻辑)
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
void led_pwm_single_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_PWM_SINGLE || !state) return;

    pwm_single_config_t cfg = get_pwm_single_config(index);
    single_states[index] = *state;

    if (state->state) {
        /* 开灯：计算占空比并启动 PWM */
        uint32_t brightness_duty = state->brightness * PWM_PERIOD / 255;
        uint32_t duty = cfg.active_high ? brightness_duty : (PWM_PERIOD - brightness_duty);
        single_duties[index] = duty;
        
        pwm_set_duty(index, duty); 
        pwm_start();
    } else {
        /* 关灯：高电平有效输出duty=0(全低电平)，低电平有效输出duty=PWM_PERIOD(全高电平) */
        uint32_t duty = cfg.active_high ? 0 : PWM_PERIOD;
        single_duties[index] = duty;
        
        pwm_set_duty(index, duty);
        pwm_start();
    }

    trigger_nvs_save(); // 替换原有的直接保存

    ESP_LOGI(TAG, "Single %d -> %s brightness %d",
             index, state->state ? "ON" : "OFF", state->brightness);
}

led_state_t* led_pwm_single_get_state(int index)
{
    if (index < 0 || index >= MAX_PWM_SINGLE) return NULL;
    return &single_states[index];
}
#else
void led_pwm_single_set_state(int index, const led_state_t *state) {}
led_state_t* led_pwm_single_get_state(int index) { return NULL; }
#endif

/* ==========================================================================
 * 公开 API - WS2812
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
void led_ws2812_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_WS2812 || !state) return;

    ws2812_config_t cfg = get_ws2812_config(index);
    ws2812_states[index] = *state;

    if (!state->state) {
        ws2812_clear(index);
    } else {
        uint8_t r = (state->red * state->brightness) / 255;
        uint8_t g = (state->green * state->brightness) / 255;
        uint8_t b = (state->blue * state->brightness) / 255;

        for (int i = 0; i < cfg.num_leds && i < WS2812_MAX_LEDS; i++) {
            ws2812_set_pixel(index, i, r, g, b);
        }
        ws2812_refresh(index);
    }

    trigger_nvs_save(); // 替换原有的直接保存

    ESP_LOGI(TAG, "WS2812 %d -> %s brightness %d color %d,%d,%d",
             index, state->state ? "ON" : "OFF", state->brightness,
             state->red, state->green, state->blue);
}

led_state_t* led_ws2812_get_state(int index)
{
    if (index < 0 || index >= MAX_WS2812) return NULL;
    return &ws2812_states[index];
}
#else
void led_ws2812_set_state(int index, const led_state_t *state) {}
led_state_t* led_ws2812_get_state(int index) { return NULL; }
#endif

/* ==========================================================================
 * 公开 API - 开关
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_SWITCH
void led_switch_set_state(int index, bool on)
{
    if (index < 0 || index >= MAX_SWITCH) return;

    switch_states[index] = on;
    switch_config_t cfg = get_switch_config(index);
    int level = on ? (cfg.active_high ? 1 : 0) : (cfg.active_high ? 0 : 1);
    gpio_set_level(cfg.gpio, level);

    trigger_nvs_save(); // 替换原有的直接保存
    
    ESP_LOGI(TAG, "Switch %d -> %s", index, on ? "ON" : "OFF");
}

bool led_switch_get_state(int index)
{
    if (index < 0 || index >= MAX_SWITCH) return false;
    return switch_states[index];
}
#else
void led_switch_set_state(int index, bool on) {}
bool led_switch_get_state(int index) { return false; }
#endif

/* ==========================================================================
 * 兼容旧 API
 * ========================================================================== */

void led_set_state(const led_state_t *state)
{
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    led_pwm_rgb_set_state(0, state);
#elif defined(CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE)
    led_pwm_single_set_state(0, state);
#elif defined(CONFIG_DEVICE_TYPE_LIGHT_WS2812)
    led_ws2812_set_state(0, state);
#endif
}

led_state_t* led_get_state(void)
{
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    return led_pwm_rgb_get_state(0);
#elif defined(CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE)
    return led_pwm_single_get_state(0);
#elif defined(CONFIG_DEVICE_TYPE_LIGHT_WS2812)
    return led_ws2812_get_state(0);
#else
    return NULL;
#endif
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */

void led_control_init(void)
{
    ESP_LOGI(TAG, "LED control init");

    device_caps_t caps = get_device_capabilities();
    ESP_LOGI(TAG, "RGB: %d, Single: %d, WS2812: %d, Switch: %d",
             caps.pwm_rgb_count, caps.pwm_single_count, caps.ws2812_count, caps.switch_count);

    load_all_states();
    
    // 统一初始化硬件，解决PWM与GPIO冲突
    hardware_init();
}
