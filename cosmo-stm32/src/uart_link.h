/**
 * uart_link.h — Cosmo Bot UART protocol layer (STM32 side)
 *
 * Wraps USART1 with non-blocking RX (interrupt-driven, byte-at-a-time) and
 * blocking TX. Speaks the line-delimited text protocol shared with the
 * ESP32-C3:
 *
 *   STM32 -> ESP32 :  [USER_INPUT: <text>]\r\n
 *   ESP32 -> STM32 :  [BOT_RESPONSE: <text>]\r\n
 *
 * In Phase 1 we only TX USER_INPUT and parse BOT_RESPONSE.
 *
 * Pin map: PA9 = USART1_TX, PA10 = USART1_RX, AF7
 */

#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>

/** Initialize USART1, GPIOs, NVIC, and start IT-driven RX. */
void UartLink_Init(void);

/** Send a USER_INPUT message to the ESP32. Blocks until TX completes. */
void UartLink_SendUserInput(const char* text);

/**
 * Drain the RX ring buffer; if a complete BOT_RESPONSE line has arrived,
 * copy its inner text into `out_buffer` (NUL-terminated, truncated to fit)
 * and return true. Otherwise return false.
 *
 * Call this regularly from the main loop.
 */
bool UartLink_PollBotResponse(char* out_buffer, size_t buffer_size);

#endif /* UART_LINK_H */
