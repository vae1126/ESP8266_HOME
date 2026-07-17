#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *type_id;
    const char *type_name;
    bool support_power;
    bool support_brightness;
    bool support_color_temp;
    bool support_color;
    bool support_query;
} device_caps_t;

typedef struct {
    int gpio;
    bool active_high;
} switch_config_t;

static inline int get_switch_count_impl(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    return CONFIG_SWITCH_COUNT;
#else
    return 0;
#endif
}

static inline const switch_config_t* get_switch_config(int index)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    static const switch_config_t configs[] = {
        { .gpio = CONFIG_SWITCH_1_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_1_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#if CONFIG_SWITCH_COUNT >= 2
        { .gpio = CONFIG_SWITCH_2_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_2_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 3
        { .gpio = CONFIG_SWITCH_3_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_3_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 4
        { .gpio = CONFIG_SWITCH_4_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_4_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 5
        { .gpio = CONFIG_SWITCH_5_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_5_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 6
        { .gpio = CONFIG_SWITCH_6_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_6_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 7
        { .gpio = CONFIG_SWITCH_7_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_7_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
#if CONFIG_SWITCH_COUNT >= 8
        { .gpio = CONFIG_SWITCH_8_GPIO,
          .active_high =
#ifdef CONFIG_SWITCH_8_ACTIVE_HIGH
              true
#else
              false
#endif
        },
#endif
    };
    if (index >= 0 && index < CONFIG_SWITCH_COUNT) {
        return &configs[index];
    }
#endif
    return NULL;
}

static inline const device_caps_t* get_switch_capabilities(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    static const device_caps_t caps = {
        .type_id = "relay",
        .type_name = "继电器/插座",
        .support_power = true,
        .support_brightness = false,
        .support_color_temp = false,
        .support_color = false,
        .support_query = false,
    };
    return &caps;
#else
    return NULL;
#endif
}

static inline const device_caps_t* get_device_capabilities(void)
{
    const device_caps_t *caps;
    caps = get_switch_capabilities();
    if (caps) return caps;

    static const device_caps_t default_caps = {
        .type_id = "relay",
        .type_name = "继电器/插座",
        .support_power = true,
        .support_brightness = false,
        .support_color_temp = false,
        .support_color = false,
        .support_query = false,
    };
    return &default_caps;
}

static inline bool has_switch(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    return true;
#else
    return false;
#endif
}

#endif /* DEVICE_CONFIG_H */
