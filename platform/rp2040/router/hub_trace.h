#ifndef GRIDPICO_RP2040_HUB_TRACE_H
#define GRIDPICO_RP2040_HUB_TRACE_H

#include <stdbool.h>
#include <stdint.h>

void gridpico_hub_trace_device(uint8_t address, uint8_t subclass, uint8_t protocol);
void gridpico_hub_trace_config(uint8_t address);
void gridpico_hub_trace_serial_reset(void);
void gridpico_hub_trace_serial_start(void);
bool gridpico_hub_trace_serial_active(void);
void gridpico_hub_trace_serial_byte(uint8_t byte);
void gridpico_hub_trace_serial_pump(bool (*write)(const char *, uint32_t));

#endif
