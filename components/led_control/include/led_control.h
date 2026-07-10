/**
 * @file    led_control.h
 * @brief   统一LED控制模块接口 - 支持多实例
 *
 * 支持：PWM RGB灯、单色灯、WS2812灯带、开关/继电器
 */

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  灯光状态结构体
 */
typedef struct {
    bool state;           // 开关状态
    uint8_t brightness;   // 亮度 0-255
    uint8_t red;          // 红色分量 0-255
    uint8_t green;        // 绿色分量 0-255
    uint8_t blue;         // 蓝色分量 0-255
} led_state_t;

/**
 * @brief  初始化灯光控制模块
 */
void led_control_init(void);

/* ==========================================================================
 * PWM RGB 灯 API（多实例）
 * ========================================================================== */
void led_pwm_rgb_set_state(int index, const led_state_t *state);
led_state_t* led_pwm_rgb_get_state(int index);

/* ==========================================================================
 * 单色灯 API（多实例）
 * ========================================================================== */
void led_pwm_single_set_state(int index, const led_state_t *state);
led_state_t* led_pwm_single_get_state(int index);

/* ==========================================================================
 * WS2812 灯带 API（多实例）
 * ========================================================================== */
void led_ws2812_set_state(int index, const led_state_t *state);
led_state_t* led_ws2812_get_state(int index);

/* ==========================================================================
 * 开关 API（多实例）
 * ========================================================================== */
void led_switch_set_state(int index, bool on);
bool led_switch_get_state(int index);

/* ==========================================================================
 * 兼容旧 API（默认操作第一个实例）
 * ========================================================================== */
void led_set_state(const led_state_t *state);
led_state_t* led_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROL_H
