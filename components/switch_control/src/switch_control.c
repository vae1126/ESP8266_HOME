#include "switch_control.h"
#include "device_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "switch_control";

#if defined(CONFIG_DEVICE_TYPE_SWITCH)
static bool switch_states[CONFIG_SWITCH_COUNT] = {false};
#endif

void switch_control_init(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        const switch_config_t *cfg = get_switch_config(i);
        if (!cfg) continue;

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg->gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);

        int level = switch_states[i] ? 1 : 0;
        if (!cfg->active_high) {
            level = 1 - level;
        }
        gpio_set_level(cfg->gpio, level);

        ESP_LOGI(TAG, "Switch[%d] init GPIO %d (active_%s)",
                 i, cfg->gpio, cfg->active_high ? "high" : "low");
    }
#else
    ESP_LOGW(TAG, "No switch configured");
#endif
}

void led_switch_set_state(int index, bool on)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    if (index < 0 || index >= CONFIG_SWITCH_COUNT) return;
    const switch_config_t *cfg = get_switch_config(index);
    if (!cfg) return;

    switch_states[index] = on;
    int level = on ? 1 : 0;
    if (!cfg->active_high) {
        level = 1 - level;
    }
    gpio_set_level(cfg->gpio, level);
#endif
}

bool led_switch_get_state(int index)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    if (index < 0 || index >= CONFIG_SWITCH_COUNT) return false;
    return switch_states[index];
#else
    return false;
#endif
}

int get_switch_count(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    return CONFIG_SWITCH_COUNT;
#else
    return 0;
#endif
}
