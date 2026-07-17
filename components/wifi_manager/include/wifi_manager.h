#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_event.h"
#include "freertos/event_groups.h"

typedef enum {
    WIFI_STATE_CONNECTING,
    WIFI_STATE_SMARTCONFIG,
    WIFI_STATE_CONNECTED,
} wifi_manager_state_t;

typedef void (*wifi_state_change_cb_t)(wifi_manager_state_t state);

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

void wifi_manager_init(void);

EventGroupHandle_t wifi_manager_get_event_group(void);

void wifi_manager_set_state_change_cb(wifi_state_change_cb_t cb);

#endif /* WIFI_MANAGER_H */
