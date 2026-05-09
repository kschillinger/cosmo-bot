/**
 * uart_link.c — see header for protocol description.
 *
 * Implementation notes:
 *   - RX is interrupt-driven, one byte at a time, into a ring buffer.
 *     This is fine at 115200 baud (~11.5 KB/s) and trivially safe re-entry.
 *     When we move to streaming audio in a later phase we'll switch to
 *     DMA + idle-line detection and bump the baud rate.
 *   - TX is blocking with a generous timeout. Phase 1 messages are short
 *     and we have nothing better to do while sending.
 *   - Line assembly happens in main-thread context (PollBotResponse) by
 *     draining the ring buffer; the ISR only stuffs bytes.
 */

#include "uart_link.h"
#include "stm32l4xx_hal.h"

#include <string.h>
#include <stdio.h>

/* ---- Tunables ------------------------------------------------------------ */
#define LINK_BAUD            115200U
#define RX_RING_SIZE         512U      /* power of 2 not required */
#define LINE_BUFFER_SIZE     256U

/* ---- Module state -------------------------------------------------------- */
static UART_HandleTypeDef huart1;

/* ISR -> main-thread ring buffer */
static volatile uint8_t  rx_byte;                    /* HAL one-shot RX target */
static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;                /* written by ISR */
static volatile uint16_t rx_tail = 0;                /* written by main */

/* Line assembly (main-thread only) */
static char     line_buf[LINE_BUFFER_SIZE];
static uint16_t line_pos = 0;

/* Latched response (main-thread) */
static bool response_ready = false;
static char response_text[LINE_BUFFER_SIZE];

/* ========================================================================== */
void UartLink_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PA9 = USART1_TX, PA10 = USART1_RX, both AF7 */
    g.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);

    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = LINK_BAUD;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        /* Park — Error_Handler is in main.c */
        while (1) {}
    }

    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* Kick off the first one-byte interrupt receive */
    HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_byte, 1);
}

/* ========================================================================== */
void UartLink_SendUserInput(const char* text)
{
    char buf[LINE_BUFFER_SIZE];
    int n = snprintf(buf, sizeof(buf), "[USER_INPUT: %s]\r\n",
                     text ? text : "");
    if (n > 0 && n < (int)sizeof(buf)) {
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, (uint16_t)n, 1000);
    }
}

/* ========================================================================== */
/* Line parser — looks specifically for [BOT_RESPONSE: ...].                  */
static void process_line(const char* line)
{
    static const char prefix[] = "[BOT_RESPONSE: ";
    const size_t      prefix_len = sizeof(prefix) - 1;

    if (strncmp(line, prefix, prefix_len) != 0) {
        return;  /* not a response line — ignore (could log [DEBUG: ...] later) */
    }

    const char* start = line + prefix_len;
    const char* end   = strrchr(start, ']');
    if (!end || end <= start) {
        return;  /* malformed */
    }

    size_t len = (size_t)(end - start);
    if (len >= sizeof(response_text)) len = sizeof(response_text) - 1;
    memcpy(response_text, start, len);
    response_text[len] = '\0';
    response_ready = true;
}

/* ========================================================================== */
bool UartLink_PollBotResponse(char* out_buffer, size_t buffer_size)
{
    /* Drain ring buffer into line buffer until a \n is seen */
    while (rx_tail != rx_head) {
        char c = (char)rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1) % RX_RING_SIZE);

        if (c == '\n') {
            line_buf[line_pos] = '\0';
            if (line_pos > 0 && line_buf[line_pos - 1] == '\r') {
                line_buf[line_pos - 1] = '\0';
            }
            if (line_pos > 0) {
                process_line(line_buf);
            }
            line_pos = 0;
        } else if (line_pos < LINE_BUFFER_SIZE - 1) {
            line_buf[line_pos++] = c;
        } else {
            /* Line too long — discard and resync on next \n */
            line_pos = 0;
        }
    }

    if (response_ready && out_buffer && buffer_size > 0) {
        size_t n = strnlen(response_text, buffer_size - 1);
        memcpy(out_buffer, response_text, n);
        out_buffer[n] = '\0';
        response_ready = false;
        return true;
    }
    return false;
}

/* ========================================================================== */
/* HAL callbacks / ISR                                                        */
/* ========================================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART1) {
        uint16_t next = (uint16_t)((rx_head + 1) % RX_RING_SIZE);
        if (next != rx_tail) {                     /* drop on overflow */
            rx_ring[rx_head] = rx_byte;
            rx_head = next;
        }
        /* Re-arm */
        HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART1) {
        /* Clear errors and re-arm RX */
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_PEFLAG(huart);
        HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_byte, 1);
    }
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
