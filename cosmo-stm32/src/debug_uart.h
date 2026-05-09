/**
 * debug_uart.h — USART2 init (PA2/PA3) + printf retarget via ST-Link VCP.
 *
 * After DebugUart_Init(), printf() writes to the ST-Link USB serial port,
 * visible in `pio device monitor` at 115200 baud.
 */

#ifndef DEBUG_UART_H
#define DEBUG_UART_H

void DebugUart_Init(void);

#endif /* DEBUG_UART_H */
