/**
 * @file    led_control.c
 * @brief   统一LED控制模块实现 (ESP8266版本)
 *
 * 支持4种设备类型，每种支持多实例：
 *   - PWM RGB 灯：3个GPIO（R/G/B），支持颜色和亮度调节
 *   - PWM 单色灯：1个GPIO，支持亮度调节
 *   - WS2812 灯带：1个GPIO，支持逐像素RGB控制
 *   - 继电器/开关：1个GPIO，仅开关控制
 *
 * ESP8266 PWM说明：
 *   - 使用 pwm.h 软件PWM库，最大支持8个通道
 *   - PWM周期1000us = 1kHz频率
 *   - 占空比范围：0 ~ PWM_PERIOD (0~1000)
 *
 * WS2812说明：
 *   - 使用GPIO位操作(bit-banging)方式驱动
 *   - 需要精确时序：T0H=350ns, T0H=700ns, T1H=700ns, T1L=600ns, Reset>50us
 *   - ESP8266主频160MHz，每周期6.25ns
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

/* ==========================================================================
 * 状态存储
 * ========================================================================== */

/* PWM RGB灯状态 */
static led_state_t rgb_states[MAX_PWM_RGB] = {0};
static uint32_t rgb_r_duties[MAX_PWM_RGB] = {0};
static uint32_t rgb_g_duties[MAX_PWM_RGB] = {0};
static uint32_t rgb_b_duties[MAX_PWM_RGB] = {0};

/* 单色灯状态 */
static led_state_t single_states[MAX_PWM_SINGLE] = {0};
static uint32_t single_duties[MAX_PWM_SINGLE] = {0};

/* WS2812状态 */
static led_state_t ws2812_states[MAX_WS2812] = {0};

/* 开关状态 */
static bool switch_states[MAX_SWITCH] = {false};

/* WS2812像素缓冲区 */
#define WS2812_MAX_LEDS 256
static uint8_t ws2812_pixels[MAX_WS2812][WS2812_MAX_LEDS * 3] = {0};

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
 * WS2812 位操作驱动
 *
 * ESP8266没有RMT外设，使用GPIO位操作实现WS2812时序
 * 通过精确的nop循环控制高低电平持续时间
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812

/**
 * @brief  发送1字节数据到WS2812
 *
 * WS2812协议：
 *   - 发送1: 高电平700ns, 低电平600ns
 *   - 发送0: 高电平350ns, 低电平700ns
 *
 * ESP8266 160MHz下，1个nop约6.25ns
 */
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

/**
 * @brief  刷新WS2812像素缓冲区到灯带
 */
static void ws2812_refresh(int index)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    if (cfg.gpio < 0 || cfg.num_leds <= 0) return;

    /* 关中断，保证时序精确 */
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    for (int i = 0; i < cfg.num_leds && i < WS2812_MAX_LEDS; i++) {
        int offset = i * 3;
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset + 1]);  /* G */
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset]);      /* R */
        ws2812_send_byte(cfg.gpio, ws2812_pixels[index][offset + 2]);  /* B */
    }

    taskEXIT_CRITICAL(&mux);

    /* 复位信号 */
    ets_delay_us(WS2812_RESET_US);
}

/**
 * @brief  设置WS2812单个像素颜色
 */
static void ws2812_set_pixel(int index, int pixel, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    if (pixel < 0 || pixel >= cfg.num_leds || pixel >= WS2812_MAX_LEDS) return;

    int offset = pixel * 3;
    ws2812_pixels[index][offset] = r;
    ws2812_pixels[index][offset + 1] = g;
    ws2812_pixels[index][offset + 2] = b;
}

/**
 * @brief  清空WS2812所有像素
 */
static void ws2812_clear(int index)
{
    ws2812_config_t cfg = get_ws2812_config(index);
    memset(ws2812_pixels[index], 0, cfg.num_leds * 3);
    ws2812_refresh(index);
}
#endif

/* ==========================================================================
 * PWM 初始化
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
/**
 * @brief  初始化PWM RGB灯
 *
 * ESP8266 PWM库使用方式：
 *   1. 收集所有RGB通道的GPIO和初始占空比
 *   2. 调用 pwm_init() 一次性初始化所有通道
 *   3. 调用 pwm_start() 开始输出
 *
 * 每个RGB灯需要3个PWM通道（R/G/B）
 * ESP8266最多支持8个PWM通道，所以最多2个RGB灯(6通道)
 */
static void pwm_rgb_init(void)
{
    int count = CONFIG_PWM_RGB_COUNT;
    if (count > MAX_PWM_RGB) count = MAX_PWM_RGB;

    /* 收集所有RGB通道的GPIO和占空比 */
    uint32_t all_gpios[MAX_PWM_RGB * 3];
    uint32_t all_duties[MAX_PWM_RGB * 3];
    int channel = 0;

    for (int i = 0; i < count; i++) {
        pwm_rgb_config_t cfg = get_pwm_rgb_config(i);

        /* 根据状态计算初始占空比 */
        if (rgb_states[i].state) {
            uint8_t r = (rgb_states[i].red * rgb_states[i].brightness) / 255;
            uint8_t g = (rgb_states[i].green * rgb_states[i].brightness) / 255;
            uint8_t b = (rgb_states[i].blue * rgb_states[i].brightness) / 255;
            rgb_r_duties[i] = cfg.active_high ? (r * PWM_PERIOD / 255) : (PWM_PERIOD - r * PWM_PERIOD / 255);
            rgb_g_duties[i] = cfg.active_high ? (g * PWM_PERIOD / 255) : (PWM_PERIOD - g * PWM_PERIOD / 255);
            rgb_b_duties[i] = cfg.active_high ? (b * PWM_PERIOD / 255) : (PWM_PERIOD - b * PWM_PERIOD / 255);
        } else {
            rgb_r_duties[i] = cfg.active_high ? 0 : PWM_PERIOD;
            rgb_g_duties[i] = cfg.active_high ? 0 : PWM_PERIOD;
            rgb_b_duties[i] = cfg.active_high ? 0 : PWM_PERIOD;
        }

        all_gpios[channel] = cfg.r_gpio;
        all_duties[channel] = rgb_r_duties[i];
        channel++;
        all_gpios[channel] = cfg.g_gpio;
        all_duties[channel] = rgb_g_duties[i];
        channel++;
        all_gpios[channel] = cfg.b_gpio;
        all_duties[channel] = rgb_b_duties[i];
        channel++;

        ESP_LOGI(TAG, "PWM RGB[%d]: R=%d G=%d B=%d active_%s duty=%d,%d,%d",
                 i, cfg.r_gpio, cfg.g_gpio, cfg.b_gpio,
                 cfg.active_high ? "high" : "low",
                 rgb_r_duties[i], rgb_g_duties[i], rgb_b_duties[i]);
    }

    pwm_init(PWM_PERIOD, all_duties, channel, all_gpios);
    pwm_start();
}
#endif

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
static void pwm_single_init(void)
{
    int count = CONFIG_PWM_SINGLE_COUNT;
    if (count > MAX_PWM_SINGLE) count = MAX_PWM_SINGLE;

    uint32_t gpios[MAX_PWM_SINGLE];
    uint32_t duties[MAX_PWM_SINGLE];

    for (int i = 0; i < count; i++) {
        pwm_single_config_t cfg = get_pwm_single_config(i);
        gpios[i] = cfg.gpio;

        if (single_states[i].state) {
            uint32_t duty = single_states[i].brightness * PWM_PERIOD / 255;
            duties[i] = cfg.active_high ? duty : (PWM_PERIOD - duty);
        } else {
            duties[i] = cfg.active_high ? 0 : PWM_PERIOD;
        }

        ESP_LOGI(TAG, "PWM Single[%d]: GPIO %d duty %d", i, cfg.gpio, duties[i]);
    }

    pwm_init(PWM_PERIOD, duties, count, gpios);
    pwm_start();
}
#endif

#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
static void ws2812_init(void)
{
    int count = CONFIG_WS2812_COUNT;
    if (count > MAX_WS2812) count = MAX_WS2812;

    for (int i = 0; i < count; i++) {
        ws2812_config_t cfg = get_ws2812_config(i);
        if (cfg.gpio < 0) continue;

        /* 配置GPIO为输出 */
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg.gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(cfg.gpio, 0);

        /* 恢复状态 */
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
}
#endif

#ifdef CONFIG_DEVICE_TYPE_SWITCH
static void switch_init(void)
{
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
        gpio_set_level(cfg.gpio, cfg.active_high ? 0 : 1);
        if (switch_states[i]) {
            gpio_set_level(cfg.gpio, cfg.active_high ? 1 : 0);
        }
        ESP_LOGI(TAG, "Switch[%d]: GPIO %d", i, cfg.gpio);
    }
}
#endif

/* ==========================================================================
 * 公开 API - PWM RGB 灯
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
/**
 * @brief  设置PWM RGB灯状态
 *
 * @param index  灯的索引 (0-based)
 * @param state  目标状态
 *
 * 状态更新逻辑：
 * 1. 保存新状态到内存
 * 2. 如果state=false(关闭)，R/G/B全部设为0
 * 3. 如果state=true(开启)，根据brightness缩放R/G/B
 * 4. 根据active_high配置反转输出
 * 5. 更新PWM占空比并保存到NVS
 */
void led_pwm_rgb_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_PWM_RGB || !state) return;

    pwm_rgb_config_t cfg = get_pwm_rgb_config(index);
    rgb_states[index] = *state;

    uint8_t r, g, b;
    if (state->state) {
        /* 开启状态：根据亮度缩放颜色 */
        r = (state->red * state->brightness) / 255;
        g = (state->green * state->brightness) / 255;
        b = (state->blue * state->brightness) / 255;
    } else {
        /* 关闭状态：所有颜色为0 */
        r = g = b = 0;
    }

    /* 计算PWM占空比，考虑active_high */
    if (cfg.active_high) {
        rgb_r_duties[index] = r * PWM_PERIOD / 255;
        rgb_g_duties[index] = g * PWM_PERIOD / 255;
        rgb_b_duties[index] = b * PWM_PERIOD / 255;
    } else {
        rgb_r_duties[index] = PWM_PERIOD - r * PWM_PERIOD / 255;
        rgb_g_duties[index] = PWM_PERIOD - g * PWM_PERIOD / 255;
        rgb_b_duties[index] = PWM_PERIOD - b * PWM_PERIOD / 255;
    }

    /* 更新PWM */
    pwm_set_duty(index * 3, rgb_r_duties[index]);
    pwm_set_duty(index * 3 + 1, rgb_g_duties[index]);
    pwm_set_duty(index * 3 + 2, rgb_b_duties[index]);
    pwm_start();

    save_state_blob("rgb", index, &rgb_states[index], sizeof(led_state_t));

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
 * 公开 API - 单色灯
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
/**
 * @brief  设置单色灯状态
 *
 * 修复bug：之前只根据brightness计算占空比，没有正确处理state=false(关闭)
 * 现在逻辑：
 *   - state=false → duty = 0(active_high) 或 PWM_PERIOD(active_low) → LED灭
 *   - state=true  → duty = brightness映射 → LED亮
 */
void led_pwm_single_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_PWM_SINGLE || !state) return;

    pwm_single_config_t cfg = get_pwm_single_config(index);
    single_states[index] = *state;

    uint32_t duty;
    if (state->state) {
        /* 开启：根据亮度计算占空比 */
        uint32_t brightness_duty = state->brightness * PWM_PERIOD / 255;
        duty = cfg.active_high ? brightness_duty : (PWM_PERIOD - brightness_duty);
    } else {
        /* 关闭 */
        duty = cfg.active_high ? 0 : PWM_PERIOD;
    }

    single_duties[index] = duty;
    pwm_set_duty(index, duty);
    pwm_start();

    save_state_blob("s", index, &single_states[index], sizeof(led_state_t));

    ESP_LOGI(TAG, "Single %d -> %s brightness %d duty %d",
             index, state->state ? "ON" : "OFF", state->brightness, duty);
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
/**
 * @brief  设置WS2812灯带状态
 *
 * 支持逐像素RGB控制：
 * 1. 如果关闭，清空所有像素
 * 2. 如果开启，根据brightness缩放颜色，设置所有像素
 * 3. 刷新到灯带
 */
void led_ws2812_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_WS2812 || !state) return;

    ws2812_config_t cfg = get_ws2812_config(index);
    ws2812_states[index] = *state;

    if (!state->state) {
        /* 关闭：清空所有像素 */
        ws2812_clear(index);
    } else {
        /* 开启：设置所有像素颜色 */
        uint8_t r = (state->red * state->brightness) / 255;
        uint8_t g = (state->green * state->brightness) / 255;
        uint8_t b = (state->blue * state->brightness) / 255;

        for (int i = 0; i < cfg.num_leds && i < WS2812_MAX_LEDS; i++) {
            ws2812_set_pixel(index, i, r, g, b);
        }
        ws2812_refresh(index);
    }

    save_state_blob("ws", index, &ws2812_states[index], sizeof(led_state_t));

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

    save_switch_state(index, on);
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

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    pwm_rgb_init();
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    pwm_single_init();
#endif
#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
    ws2812_init();
#endif
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    switch_init();
#endif
}
