/**
 * @file    device_config.h
 * @brief   编译时设备配置
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== PWM RGB 配置 ========== */
typedef struct {
    int r_gpio, g_gpio, b_gpio;
    bool active_high;
} pwm_rgb_config_t;

/* ========== 单色灯配置 ========== */
typedef struct {
    int gpio;
    bool active_high;
} pwm_single_config_t;

/* ========== WS2812 配置 ========== */
typedef struct {
    int gpio;
    int num_leds;
} ws2812_config_t;

/* ========== 开关配置 ========== */
typedef struct {
    int gpio;
    bool active_high;
} switch_config_t;

/* ========== 设备能力 ========== */
typedef struct {
    bool has_pwm_rgb;
    bool has_pwm_single;
    bool has_ws2812;
    bool has_switch;
    int pwm_rgb_count;
    int pwm_single_count;
    int ws2812_count;
    int switch_count;
} device_caps_t;

/* 获取设备能力 */
static inline device_caps_t get_device_capabilities(void)
{
    device_caps_t caps = {0};

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_RGB
    caps.has_pwm_rgb = true;
    caps.pwm_rgb_count = CONFIG_PWM_RGB_COUNT;
#endif

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    caps.has_pwm_single = true;
    caps.pwm_single_count = CONFIG_PWM_SINGLE_COUNT;
#endif

#ifdef CONFIG_DEVICE_TYPE_LIGHT_WS2812
    caps.has_ws2812 = true;
    caps.ws2812_count = CONFIG_WS2812_COUNT;
#endif

#ifdef CONFIG_DEVICE_TYPE_SWITCH
    caps.has_switch = true;
    caps.switch_count = CONFIG_SWITCH_COUNT;
#endif

    return caps;
}

/* 获取PWM RGB配置 */
static inline pwm_rgb_config_t get_pwm_rgb_config(int index)
{
    pwm_rgb_config_t cfg = {0};
    switch (index) {
#ifdef CONFIG_PWM_RGB_1_R_GPIO
        case 0:
            cfg.r_gpio = CONFIG_PWM_RGB_1_R_GPIO;
            cfg.g_gpio = CONFIG_PWM_RGB_1_G_GPIO;
            cfg.b_gpio = CONFIG_PWM_RGB_1_B_GPIO;
#ifdef CONFIG_PWM_RGB_1_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_RGB_1_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_PWM_RGB_2_R_GPIO
        case 1:
            cfg.r_gpio = CONFIG_PWM_RGB_2_R_GPIO;
            cfg.g_gpio = CONFIG_PWM_RGB_2_G_GPIO;
            cfg.b_gpio = CONFIG_PWM_RGB_2_B_GPIO;
#ifdef CONFIG_PWM_RGB_2_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_RGB_2_ACTIVE_HIGH;
#endif
            break;
#endif
        default: break;
    }
    return cfg;
}

/* 获取单色灯配置 */
static inline pwm_single_config_t get_pwm_single_config(int index)
{
    pwm_single_config_t cfg = {0};
    switch (index) {
#ifdef CONFIG_PWM_SINGLE_1_GPIO
        case 0:
            cfg.gpio = CONFIG_PWM_SINGLE_1_GPIO;
#ifdef CONFIG_PWM_SINGLE_1_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_SINGLE_1_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_PWM_SINGLE_2_GPIO
        case 1:
            cfg.gpio = CONFIG_PWM_SINGLE_2_GPIO;
#ifdef CONFIG_PWM_SINGLE_2_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_SINGLE_2_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_PWM_SINGLE_3_GPIO
        case 2:
            cfg.gpio = CONFIG_PWM_SINGLE_3_GPIO;
#ifdef CONFIG_PWM_SINGLE_3_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_SINGLE_3_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_PWM_SINGLE_4_GPIO
        case 3:
            cfg.gpio = CONFIG_PWM_SINGLE_4_GPIO;
#ifdef CONFIG_PWM_SINGLE_4_ACTIVE_HIGH
            cfg.active_high = CONFIG_PWM_SINGLE_4_ACTIVE_HIGH;
#endif
            break;
#endif
        default: break;
    }
    return cfg;
}

/* 获取WS2812配置 */
static inline ws2812_config_t get_ws2812_config(int index)
{
    ws2812_config_t cfg = {0};
    switch (index) {
#ifdef CONFIG_WS2812_1_GPIO
        case 0:
            cfg.gpio = CONFIG_WS2812_1_GPIO;
            cfg.num_leds = CONFIG_WS2812_1_NUM;
            break;
#endif
#ifdef CONFIG_WS2812_2_GPIO
        case 1:
            cfg.gpio = CONFIG_WS2812_2_GPIO;
            cfg.num_leds = CONFIG_WS2812_2_NUM;
            break;
#endif
        default: break;
    }
    return cfg;
}

/* 获取开关配置 */
static inline switch_config_t get_switch_config(int index)
{
    switch_config_t cfg = {0};
    switch (index) {
#ifdef CONFIG_SWITCH_1_GPIO
        case 0:
            cfg.gpio = CONFIG_SWITCH_1_GPIO;
#ifdef CONFIG_SWITCH_1_ACTIVE_HIGH
            cfg.active_high = CONFIG_SWITCH_1_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_SWITCH_2_GPIO
        case 1:
            cfg.gpio = CONFIG_SWITCH_2_GPIO;
#ifdef CONFIG_SWITCH_2_ACTIVE_HIGH
            cfg.active_high = CONFIG_SWITCH_2_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_SWITCH_3_GPIO
        case 2:
            cfg.gpio = CONFIG_SWITCH_3_GPIO;
#ifdef CONFIG_SWITCH_3_ACTIVE_HIGH
            cfg.active_high = CONFIG_SWITCH_3_ACTIVE_HIGH;
#endif
            break;
#endif
#ifdef CONFIG_SWITCH_4_GPIO
        case 3:
            cfg.gpio = CONFIG_SWITCH_4_GPIO;
#ifdef CONFIG_SWITCH_4_ACTIVE_HIGH
            cfg.active_high = CONFIG_SWITCH_4_ACTIVE_HIGH;
#endif
            break;
#endif
        default: break;
    }
    return cfg;
}

/* 快捷判断函数 */
static inline bool has_pwm_rgb(void) { return get_device_capabilities().has_pwm_rgb; }
static inline bool has_pwm_single(void) { return get_device_capabilities().has_pwm_single; }
static inline bool has_ws2812(void) { return get_device_capabilities().has_ws2812; }
static inline bool has_switch(void) { return get_device_capabilities().has_switch; }

#ifdef __cplusplus
}
#endif

#endif // DEVICE_CONFIG_H
