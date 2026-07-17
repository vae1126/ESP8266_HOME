#ifndef OTA_UPGRADE_H
#define OTA_UPGRADE_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ota_upgrade_init(void);
int ota_upgrade_get_status(void);
int ota_upgrade_get_progress(void);

#endif
