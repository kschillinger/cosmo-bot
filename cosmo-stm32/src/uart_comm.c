/**
 * uart_comm.c — Adapter over uart_link.h.
 *
 * uart_message_available() polls UartLink_PollBotResponse() once per call and
 * caches the most recent line in s_rx_line. The FSM's
 * fsm_execute_sending_to_esp32() then drains it with uart_receive_message().
 *
 * This is NOT a stub — it actually exchanges messages with the ESP32.
 * It's the one piece of subsystem plumbing already real, since uart_link
 * was brought up in Phase 1.
 */

#include "uart_comm.h"
#include "uart_link.h"

#include <stdio.h>
#include <string.h>

#define UART_COMM_RX_MAX  256

static char    s_rx_line[UART_COMM_RX_MAX];
static uint8_t s_rx_ready = 0;

void uart_init(void)
{
    printf("[UART] uart_init() -> UartLink_Init()\r\n");
    UartLink_Init();
    s_rx_line[0] = '\0';
    s_rx_ready   = 0;
}

void uart_send_message(const char *message)
{
    if (message == NULL) message = "";
    printf("[UART] uart_send_message(\"%s\")\r\n", message);
    UartLink_SendUserInput(message);
}

uint8_t uart_message_available(void)
{
    /* If we already have a buffered line waiting to be read, report ready. */
    if (s_rx_ready) return 1;

    /* Otherwise try to drain one from the link layer. */
    if (UartLink_PollBotResponse(s_rx_line, sizeof(s_rx_line))) {
        s_rx_ready = 1;
        printf("[UART] message_available -> \"%s\"\r\n", s_rx_line);
        return 1;
    }
    return 0;
}

char *uart_receive_message(void)
{
    if (!s_rx_ready) {
        return (char *)"";
    }
    s_rx_ready = 0;
    return s_rx_line;
}
