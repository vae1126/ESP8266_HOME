/**
 * @file    led_control.c
 * @brief   统一LED控制模块实现 (ESP8266版本)
 *
 * 本模块支持4种设备类型，每种支持多实例：
 *   - PWM RGB 灯：需要3个GPIO（红、绿、蓝），支持颜色和亮度调节
 *   - PWM 单色灯：需要1个GPIO，支持亮度调节
 *   - WS2812 灯带：需要1个GPIO，支持逐像素RGB控制（简化版，仅开关）
 *   - 继电器/开关：需要1个GPIO，仅开关控制
 *
 * 与ESP32-C3版本的主要区别：
 *   - ESP32-C3使用硬件LEDC外设实现PWM，精度高、CPU占用低
 *   - ESP8266使用软件PWM库（pwm.h），精度稍低但够用
 *   - ESP32-C3使用RMT外设驱动WS2812，ESP8266需要位操作或I2S（本版本简化为开关）
 *   - ESP32-C3支持6个LEDC通道，ESP8266支持8个软件PWM通道
 *
 * NVS持久化：
 *   - 所有设备状态（开/关、亮度、颜色）保存在NVS中
 *   - 重启后自动恢复上次状态
 *   - 命名空间："led_state"
 *   - 键格式："s_0", "s_1"...（单色灯）, "sw_0", "sw_1"...（开关）
 */

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/pwm.h"      /* ESP8266软件PWM库，非ESP32的LEDC */
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

#define NVS_NAMESPACE "led_state"   /* NVS命名空间，所有LED状态存在这里 */
#define MAX_PWM_SINGLE 8            /* 最大单色灯实例数 */
#define MAX_SWITCH 8                /* 最大开关实例数 */

/**
 * PWM周期，单位：微秒(us)
 * 1000us = 1kHz频率
 * ESP8266的pwm库使用周期而非频率来配置PWM
 * 与ESP32-C3的LEDC不同，这里没有duty_resolution概念
 * 占空比直接用 0~PWM_PERIOD 的数值表示
 */
#define PWM_PERIOD 1000

/* ==========================================================================
 * 状态存储
 * ========================================================================== */

/**
 * 单色灯状态数组
 * 每个元素保存一个灯的完整状态：开/关、亮度、RGB颜色
 * 对于单色灯，只使用 state 和 brightness 字段
 */
static led_state_t single_states[MAX_PWM_SINGLE] = {0};

/**
 * 开关状态数组
 * true=开, false=关
 */
static bool switch_states[MAX_SWITCH] = {false};

/**
 * PWM GPIO引脚数组
 * 传给 pwm_init() 函数，告诉PWM库控制哪些引脚
 */
static uint32_t single_gpios[MAX_PWM_SINGLE] = {0};

/**
 * PWM占空比数组
 * 传给 pwm_init() 函数，设置每个通道的初始占空比
 * 值范围：0 ~ PWM_PERIOD (0~1000)
 */
static uint32_t single_duties[MAX_PWM_SINGLE] = {0};

/* ==========================================================================
 * NVS 持久化函数
 * ========================================================================== */

/**
 * @brief  保存单色灯状态到NVS
 *
 * 将 led_state_t 结构体以 blob 形式存入NVS
 * 重启后可通过 load_single_state() 恢复
 *
 * @param index  灯的索引 (0-based)
 * @param state  要保存的状态指针
 */
static void save_single_state(int index, const led_state_t *state)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

    char key[20];
    snprintf(key, sizeof(key), "s_%d", index);  /* 键名如 "s_0", "s_1" */
    nvs_set_blob(handle, key, state, sizeof(led_state_t));
    nvs_commit(handle);
    nvs_close(handle);
}

/**
 * @brief  从NVS加载单色灯状态
 *
 * @param index  灯的索引 (0-based)
 * @param state  输出参数，加载的状态写入此处
 * @return true=加载成功, false=NVS中无数据
 */
static bool load_single_state(int index, led_state_t *state)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    char key[20];
    snprintf(key, sizeof(key), "s_%d", index);
    size_t size = sizeof(led_state_t);
    esp_err_t err = nvs_get_blob(handle, key, state, &size);
    nvs_close(handle);
    return (err == ESP_OK && size == sizeof(led_state_t));
}

/**
 * @brief  保存开关状态到NVS
 *
 * 开关只有开/关两种状态，用 uint8_t (0/1) 存储即可
 */
static void save_switch_state(int index, bool state)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

    char key[20];
    snprintf(key, sizeof(key), "sw_%d", index);  /* 键名如 "sw_0", "sw_1" */
    uint8_t val = state ? 1 : 0;
    nvs_set_u8(handle, key, val);
    nvs_commit(handle);
    nvs_close(handle);
}

/**
 * @brief  从NVS加载开关状态
 */
static bool load_switch_state(int index)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    char key[20];
    snprintf(key, sizeof(key), "sw_%d", index);
    uint8_t val = 0;
    esp_err_t err = nvs_get_u8(handle, key, &val);
    nvs_close(handle);
    return (err == ESP_OK && val != 0);
}

/**
 * @brief  加载所有设备状态
 *
 * 在 led_control_init() 中调用，从NVS恢复所有设备的上次状态
 * 如果NVS中没有数据（首次启动），使用默认值
 */
static void load_all_states(void)
{
#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    for (int i = 0; i < CONFIG_PWM_SINGLE_COUNT && i < MAX_PWM_SINGLE; i++) {
        if (!load_single_state(i, &single_states[i])) {
            /* 首次启动，NVS无数据，使用默认值 */
            single_states[i].state = false;       /* 默认关闭 */
            single_states[i].brightness = 255;     /* 默认最大亮度 */
            single_states[i].red = 255;
            single_states[i].green = 255;
            single_states[i].blue = 255;
        }
    }
#endif
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    for (int i = 0; i < CONFIG_SWITCH_COUNT && i < MAX_SWITCH; i++) {
        switch_states[i] = load_switch_state(i);  /* 默认false（关闭） */
    }
#endif
    ESP_LOGI(TAG, "All states loaded from NVS");
}

/* ==========================================================================
 * PWM 单色灯实现
 *
 * ESP8266 PWM库 API 说明：
 *   pwm_init(period, duties, channel_num, pin_num)
 *     - period: PWM周期（微秒），如1000=1kHz
 *     - duties: 各通道初始占空比数组（0~period）
 *     - channel_num: 通道数
 *     - pin_num: GPIO引脚数组
 *   pwm_set_duty(channel, duty) - 设置指定通道的占空比
 *   pwm_start() - 应用设置，立即生效
 *
 * 与ESP32-C3的区别：
 *   ESP32-C3使用LEDC硬件外设：
 *     ledc_timer_config() - 配置定时器（频率、分辨率）
 *     ledc_channel_config() - 配置通道（GPIO、定时器绑定）
 *     ledc_set_duty() + ledc_update_duty() - 设置占空比
 *   ESP8266使用软件PWM：
 *     pwm_init() - 一次性配置所有通道
 *     pwm_set_duty() + pwm_start() - 设置占空比并生效
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE

/**
 * @brief  初始化PWM单色灯
 *
 * 1. 收集所有单色灯的GPIO引脚
 * 2. 计算初始占空比（根据NVS恢复的状态）
 * 3. 调用 pwm_init() 初始化PWM库
 * 4. 调用 pwm_start() 启动PWM输出
 * 5. 执行LED闪烁测试，验证GPIO工作正常
 */
static void pwm_single_init(void)
{
    ESP_LOGI(TAG, "Initializing %d PWM single lights", CONFIG_PWM_SINGLE_COUNT);

    int count = CONFIG_PWM_SINGLE_COUNT;
    if (count > MAX_PWM_SINGLE) count = MAX_PWM_SINGLE;

    for (int i = 0; i < count; i++) {
        pwm_single_config_t cfg = get_pwm_single_config(i);
        single_gpios[i] = cfg.gpio;

        /**
         * 计算初始占空比
         * 对于低电平点亮(active_low)的LED：
         *   - duty=0 → GPIO始终低电平 → LED全亮
         *   - duty=PWM_PERIOD → GPIO始终高电平 → LED全灭
         * 对于高电平点亮(active_high)的LED：
         *   - duty=0 → GPIO始终低电平 → LED全灭
         *   - duty=PWM_PERIOD → GPIO始终高电平 → LED全亮
         */
        if (single_states[i].state) {
            /* LED开启：根据亮度计算占空比 */
            uint32_t duty = single_states[i].brightness * PWM_PERIOD / 255;
            if (!cfg.active_high) {
                duty = PWM_PERIOD - duty;  /* 低电平点亮需要反转 */
            }
            single_duties[i] = duty;
        } else {
            /* LED关闭 */
            single_duties[i] = cfg.active_high ? 0 : PWM_PERIOD;
        }

        ESP_LOGI(TAG, "PWM Single[%d]: GPIO %d active_%s state %s brightness %d duty %d",
                 i, cfg.gpio, cfg.active_high ? "high" : "low",
                 single_states[i].state ? "ON" : "OFF",
                 single_states[i].brightness, single_duties[i]);
    }

    /* 初始化PWM库 */
    esp_err_t ret = pwm_init(PWM_PERIOD, single_duties, count, single_gpios);
    ESP_LOGI(TAG, "pwm_init returned: %d", ret);
    pwm_start();  /* 必须调用pwm_start()才能开始输出PWM信号 */
    ESP_LOGI(TAG, "pwm_start done");

    /**
     * 启动测试：闪烁LED 3次
     * 验证GPIO接线正确、PWM输出正常
     * 如果LED不闪烁，请检查：
     *   1. GPIO引脚号是否正确
     *   2. LED是否正确连接（正极→GPIO，负极→GND，或反之）
     *   3. 是否需要限流电阻
     */
    ESP_LOGI(TAG, "LED test: blinking 3 times...");
    for (int j = 0; j < 3; j++) {
        pwm_set_duty(0, 0);           /* 占空比=0 → 低电平 → LED亮(active_low) */
        pwm_start();
        vTaskDelay(pdMS_TO_TICKS(500));
        pwm_set_duty(0, PWM_PERIOD);  /* 占空比=PWM_PERIOD → 高电平 → LED灭 */
        pwm_start();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "LED test done");
}

/**
 * @brief  设置单色灯状态
 *
 * @param index  灯的索引 (0-based)
 * @param state  目标状态（开/关、亮度、颜色）
 *
 * 处理流程：
 * 1. 保存新状态到内存
 * 2. 根据亮度和active_high计算PWM占空比
 * 3. 调用 pwm_set_duty() 更新占空比
 * 4. 调用 pwm_start() 使设置生效
 * 5. 保存状态到NVS（持久化）
 */
void led_pwm_single_set_state(int index, const led_state_t *state)
{
    if (index < 0 || index >= MAX_PWM_SINGLE) return;

    single_states[index] = *state;
    pwm_single_config_t cfg = get_pwm_single_config(index);

    /* 计算占空比 */
    uint32_t duty;
    if (state->state) {
        /* LED开启：brightness 0~255 映射到 0~PWM_PERIOD */
        duty = state->brightness * PWM_PERIOD / 255;
        if (!cfg.active_high) {
            duty = PWM_PERIOD - duty;  /* 低电平点亮反转 */
        }
    } else {
        /* LED关闭 */
        duty = cfg.active_high ? 0 : PWM_PERIOD;
    }

    /* 应用PWM设置 */
    pwm_set_duty(index, duty);
    pwm_start();  /* 必须调用pwm_start()才能生效 */

    /* 持久化到NVS */
    save_single_state(index, state);

    ESP_LOGI(TAG, "PWM Single %d -> %s brightness %d duty %d",
             index, state->state ? "ON" : "OFF", state->brightness, duty);
}

led_state_t* led_pwm_single_get_state(int index)
{
    if (index < 0 || index >= MAX_PWM_SINGLE) return NULL;
    return &single_states[index];
}

#else
/* 未启用单色灯时的空实现 */
void led_pwm_single_set_state(int index, const led_state_t *state) {}
led_state_t* led_pwm_single_get_state(int index) { return NULL; }
#endif

/* ==========================================================================
 * PWM RGB 灯实现（简化版 - 仅日志）
 *
 * ESP32-C3版本使用3个LEDC通道分别控制R/G/B：
 *   ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0 + i*3, r);
 *   ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0 + i*3);
 *
 * ESP8266要实现完整RGB PWM需要：
 *   1. 使用3个PWM通道（每个通道控制一个颜色）
 *   2. 或使用软件PWM同时控制3个GPIO
 *
 * 当前版本为简化实现，仅输出日志，不实际控制硬件
 * 如需完整实现，参考ESP8266 pwm_init() API
 * ========================================================================== */

static led_state_t dummy_rgb_state = {0};

void led_pwm_rgb_set_state(int index, const led_state_t *state)
{
    (void)index;
    ESP_LOGI(TAG, "RGB state: %d brightness: %d color: %d,%d,%d",
             state->state, state->brightness, state->red, state->green, state->blue);
}

led_state_t* led_pwm_rgb_get_state(int index) { (void)index; return &dummy_rgb_state; }

/* ==========================================================================
 * WS2812 灯带实现（简化版 - 仅日志）
 *
 * ESP32-C3版本使用led_strip库 + RMT外设：
 *   led_strip_new_rmt_device() - 创建WS2812设备
 *   led_strip_set_pixel(strip, i, r, g, b) - 设置每个像素颜色
 *   led_strip_refresh(strip) - 刷新显示
 *
 * ESP8266要实现完整WS2812需要：
 *   1. 位操作（bit-banging）：精确时序控制GPIO
 *   2. I2S DMA方式：通过I2S外设生成WS2812时序
 *   3. 使用第三方库如 ws2812_i2s 或 ws2812_bitbang
 *
 * 当前版本为简化实现，仅输出日志
 * 如需完整实现，可参考：
 *   - https://github.com/CHERTS/esp8266-ws2812
 *   - 使用I2S + DMA方式（性能最好）
 * ========================================================================== */

static led_state_t dummy_ws2812_state = {0};

void led_ws2812_set_state(int index, const led_state_t *state)
{
    (void)index;
    ESP_LOGI(TAG, "WS2812 state: %d brightness: %d", state->state, state->brightness);
}

led_state_t* led_ws2812_get_state(int index) { (void)index; return &dummy_ws2812_state; }

/* ==========================================================================
 * 开关/继电器实现
 *
 * 最简单的控制方式：GPIO高/低电平
 * ESP32-C3和ESP8266的GPIO API基本相同：
 *   gpio_config() - 配置GPIO参数
 *   gpio_set_level() - 设置输出电平
 * ========================================================================== */

#ifdef CONFIG_DEVICE_TYPE_SWITCH

/**
 * @brief  初始化开关/继电器
 *
 * 配置GPIO为输出模式，恢复NVS中保存的状态
 */
static void switch_init(void)
{
    ESP_LOGI(TAG, "Initializing %d switches", CONFIG_SWITCH_COUNT);

    for (int i = 0; i < CONFIG_SWITCH_COUNT && i < MAX_SWITCH; i++) {
        switch_config_t cfg = get_switch_config(i);

        /* 配置GPIO */
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg.gpio),    /* GPIO引脚掩码 */
            .mode = GPIO_MODE_OUTPUT,               /* 输出模式 */
            .pull_up_en = GPIO_PULLUP_DISABLE,      /* 禁用上拉 */
            .pull_down_en = GPIO_PULLDOWN_DISABLE,  /* 禁用下拉 */
            .intr_type = GPIO_INTR_DISABLE,         /* 禁用中断 */
        };
        gpio_config(&io_conf);

        /* 初始关闭 */
        gpio_set_level(cfg.gpio, cfg.active_high ? 0 : 1);

        /* 恢复NVS状态 */
        if (switch_states[i]) {
            gpio_set_level(cfg.gpio, cfg.active_high ? 1 : 0);
        }

        ESP_LOGI(TAG, "Switch[%d]: GPIO %d active_%s state %s", i,
                 cfg.gpio, cfg.active_high ? "high" : "low",
                 switch_states[i] ? "ON" : "OFF");
    }
}

/**
 * @brief  设置开关状态
 *
 * @param index  开关索引 (0-based)
 * @param on     true=开, false=关
 */
void led_switch_set_state(int index, bool on)
{
    if (index < 0 || index >= MAX_SWITCH) return;

    switch_states[index] = on;
    switch_config_t cfg = get_switch_config(index);

    /* 根据active_high配置计算输出电平 */
    int level = on ? (cfg.active_high ? 1 : 0) : (cfg.active_high ? 0 : 1);
    gpio_set_level(cfg.gpio, level);

    save_switch_state(index, on);
    ESP_LOGI(TAG, "Switch %d -> %s (GPIO %d = %d)", index, on ? "ON" : "OFF", cfg.gpio, level);
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
 * 兼容旧 API（默认操作第一个实例）
 * ========================================================================== */

void led_set_state(const led_state_t *state)
{
    led_pwm_single_set_state(0, state);
}

led_state_t* led_get_state(void)
{
    return led_pwm_single_get_state(0);
}

/* ==========================================================================
 * 模块初始化入口
 * ========================================================================== */

/**
 * @brief  初始化LED控制模块
 *
 * 初始化顺序：
 * 1. 打印设备能力（各类型实例数）
 * 2. 从NVS加载所有设备状态
 * 3. 初始化各类型硬件（PWM、GPIO）
 */
void led_control_init(void)
{
    ESP_LOGI(TAG, "LED control init");

    device_caps_t caps = get_device_capabilities();
    ESP_LOGI(TAG, "PWM RGB: %d, Single: %d, WS2812: %d, Switch: %d",
             caps.pwm_rgb_count, caps.pwm_single_count, caps.ws2812_count, caps.switch_count);

    load_all_states();

#ifdef CONFIG_DEVICE_TYPE_LIGHT_PWM_SINGLE
    pwm_single_init();
#endif
#ifdef CONFIG_DEVICE_TYPE_SWITCH
    switch_init();
#endif
}
