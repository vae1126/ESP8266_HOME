#include "status_led.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "status_led";

#if defined(CONFIG_STATUS_LED_ENABLE)

static volatile status_led_state_t s_current_state = STATUS_LED_OFF;
static volatile int s_sub_phase = 0;
static TimerHandle_t s_timer = NULL;

#define LED_GPIO CONFIG_STATUS_LED_GPIO
#ifdef CONFIG_STATUS_LED_ACTIVE_HIGH
#define LED_ON  1
#define LED_OFF 0
#else
#define LED_ON  0
#define LED_OFF 1
#endif

static void led_set(bool on)
{
    gpio_set_level(LED_GPIO, on ? LED_ON : LED_OFF);
}

static void timer_callback(TimerHandle_t xTimer)
{
    int next_period = 200;

    switch (s_current_state) {
    case STATUS_LED_STARTUP:
        led_set(s_sub_phase % 2 == 0);
        s_sub_phase++;
        next_period = 200;
        break;

    case STATUS_LED_WIFI_CONNECTING:
        switch (s_sub_phase) {
        case 0: led_set(true);  s_sub_phase = 1; next_period = 200; break;
        case 1: led_set(false); s_sub_phase = 2; next_period = 1000; break;
        default: s_sub_phase = 0; next_period = 100; break;
        }
        break;

    case STATUS_LED_MQTT_CONNECTING:
        switch (s_sub_phase) {
        case 0: led_set(true);  s_sub_phase = 1; next_period = 400; break;
        case 1: led_set(false); s_sub_phase = 2; next_period = 400; break;
        case 2: led_set(true);  s_sub_phase = 3; next_period = 400; break;
        case 3: led_set(false); s_sub_phase = 4; next_period = 2000; break;
        default: s_sub_phase = 0; next_period = 100; break;
        }
        break;

    case STATUS_LED_SMARTCONFIG:
        switch (s_sub_phase) {
        case 0: led_set(true);  s_sub_phase = 1; next_period = 200; break;
        case 1: led_set(false); s_sub_phase = 2; next_period = 200; break;
        case 2: led_set(true);  s_sub_phase = 3; next_period = 200; break;
        case 3: led_set(false); s_sub_phase = 4; next_period = 200; break;
        case 4: led_set(true);  s_sub_phase = 5; next_period = 200; break;
        case 5: led_set(false); s_sub_phase = 6; next_period = 1000; break;
        default: s_sub_phase = 0; next_period = 100; break;
        }
        break;

    case STATUS_LED_OTA_UPGRADE:
        switch (s_sub_phase) {
        case 0: led_set(true);  s_sub_phase = 1; next_period = 200; break;
        case 1: led_set(false); s_sub_phase = 2; next_period = 200; break;
        case 2: led_set(true);  s_sub_phase = 3; next_period = 200; break;
        case 3: led_set(false); s_sub_phase = 4; next_period = 200; break;
        case 4: led_set(true);  s_sub_phase = 5; next_period = 200; break;
        case 5: led_set(false); s_sub_phase = 6; next_period = 200; break;
        case 6: led_set(true);  s_sub_phase = 7; next_period = 200; break;
        case 7: led_set(false); s_sub_phase = 8; next_period = 1000; break;
        default: s_sub_phase = 0; next_period = 100; break;
        }
        break;

    case STATUS_LED_READY:
        switch (s_sub_phase) {
        case 0: led_set(true);  s_sub_phase = 1; next_period = 150; break;
        case 1: led_set(false); s_sub_phase = 2; next_period = 75; break;
        case 2: led_set(true);  s_sub_phase = 3; next_period = 150; break;
        case 3: led_set(false); s_sub_phase = 4; next_period = 1000; break;
        default: s_sub_phase = 0; next_period = 100; break;
        }
        break;

    case STATUS_LED_ERROR:
        led_set(true);
        next_period = 1000;
        break;

    case STATUS_LED_OFF:
    default:
        led_set(false);
        next_period = 1000;
        break;
    }

    xTimerChangePeriod(xTimer, pdMS_TO_TICKS(next_period), 0);
}

void status_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    led_set(false);

    s_timer = xTimerCreate("status_led", pdMS_TO_TICKS(200),
                           pdTRUE, NULL, timer_callback);
    if (s_timer) {
        xTimerStart(s_timer, pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Status LED initialized on GPIO %d (active_%s)",
             LED_GPIO,
#ifdef CONFIG_STATUS_LED_ACTIVE_HIGH
             "high"
#else
             "low"
#endif
            );
}

void status_led_set_state(status_led_state_t state)
{
    if (state == s_current_state) return;
    s_current_state = state;
    s_sub_phase = 0;
}

#else

void status_led_init(void) {}
void status_led_set_state(status_led_state_t state) {}

#endif
