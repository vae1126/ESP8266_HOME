#ifndef STATUS_LED_H
#define STATUS_LED_H

typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_STARTUP,
    STATUS_LED_WIFI_CONNECTING,
    STATUS_LED_MQTT_CONNECTING,
    STATUS_LED_SMARTCONFIG,
    STATUS_LED_OTA_UPGRADE,
    STATUS_LED_READY,
    STATUS_LED_ERROR,
} status_led_state_t;

void status_led_init(void);
void status_led_set_state(status_led_state_t state);

#endif
