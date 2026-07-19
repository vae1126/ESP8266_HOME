#ifndef MQTT_HA_H
#define MQTT_HA_H

void mqtt_ha_init(void);
void mqtt_ha_report_device_status(void);
void mqtt_ha_publish_discovery(void);
void mqtt_ha_publish_states(void);

#endif
