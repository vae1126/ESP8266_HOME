#ifndef SWITCH_CONTROL_H
#define SWITCH_CONTROL_H

#include <stdbool.h>

void switch_control_init(void);
void led_switch_set_state(int index, bool on);
bool led_switch_get_state(int index);
int get_switch_count(void);

#endif
