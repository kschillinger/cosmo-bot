/**
 * system_utils.h — Timing + simple GPIO helpers for the FSM.
 *
 * Wraps HAL_GetTick() / HAL_Delay() so the FSM stays HAL-free.
 * Also exposes a debounced edge detector for the B1 user button (PC13)
 * which the FSM uses to leave IDLE.
 */

#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <stdint.h>

uint32_t system_get_tick_ms(void);
void     system_delay_ms(uint32_t ms);

/**
 * One-shot rising-press detector for B1 (PC13).
 * Returns 1 exactly once per press (transition idle->pressed, after debounce);
 * returns 0 otherwise. Safe to call every fsm_run() iteration.
 *
 * Assumes the GPIO has already been configured as input (main.c does this
 * during GPIO_Init()).
 */
uint8_t system_button_pressed_edge(void);

#endif /* SYSTEM_UTILS_H */
