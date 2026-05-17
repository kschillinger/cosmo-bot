/**
 * uart_comm.h — FSM-facing UART API (STM32L476 <-> ESP32-C3 link)
 *
 * Thin adapter on top of uart_link.h. The FSM uses the simpler four-function
 * API here; the real protocol framing ([USER_INPUT: ...] / [BOT_RESPONSE: ...])
 * lives in uart_link.c.
 *
 * Strings handed to uart_send_message() / returned from uart_receive_message()
 * are inner text only — no framing, no \r\n.
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>

void    uart_init(void);
void    uart_send_message(const char *message);
uint8_t uart_message_available(void);    /* 1 if a complete reply is buffered */
char   *uart_receive_message(void);      /* NUL-terminated; "" if none        */

#endif /* UART_COMM_H */
