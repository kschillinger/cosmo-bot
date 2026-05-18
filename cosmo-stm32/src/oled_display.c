/**
 * oled_display.c — SSD1306 128x64 I2C driver + simple state renderers.
 */

#include "oled_display.h"

#include "oled_canvas.h"
#include "system_utils.h"

#include "stm32l4xx_hal.h"

#include <stdio.h>

#ifndef OLED_I2C_INSTANCE
#define OLED_I2C_INSTANCE I2C1
#endif

#ifndef OLED_I2C_GPIO_PORT
#define OLED_I2C_GPIO_PORT GPIOB
#endif

#ifndef OLED_I2C_GPIO_CLK_ENABLE
#define OLED_I2C_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

#ifndef OLED_I2C_SCL_PIN
#define OLED_I2C_SCL_PIN GPIO_PIN_8
#endif

#ifndef OLED_I2C_SDA_PIN
#define OLED_I2C_SDA_PIN GPIO_PIN_9
#endif

#ifndef OLED_I2C_AF
#define OLED_I2C_AF GPIO_AF4_I2C1
#endif

#ifndef OLED_I2C_ADDR_PRIMARY
#define OLED_I2C_ADDR_PRIMARY 0x3CU
#endif

#ifndef OLED_I2C_ADDR_SECONDARY
#define OLED_I2C_ADDR_SECONDARY 0x3DU
#endif

#define OLED_I2C_TIMEOUT_MS 30U
#define OLED_LISTEN_REFRESH_MS 60U
#define OLED_I2C_CONTROL_COMMAND 0x00U
#define OLED_I2C_CONTROL_DATA 0x40U
#define OLED_PAGE_COUNT (OLED_CANVAS_HEIGHT / 8U)

typedef enum {
    OLED_MODE_NONE = 0,
    OLED_MODE_IDLE,
    OLED_MODE_LISTENING,
    OLED_MODE_PROCESSING,
    OLED_MODE_RESPONDING,
    OLED_MODE_ERROR
} oled_mode_t;

static I2C_HandleTypeDef s_hi2c;
static uint8_t s_framebuffer[OLED_CANVAS_BYTES];
static uint8_t s_initialized = 0;
static uint8_t s_dirty = 0;
static uint8_t s_error_logged = 0;
static oled_mode_t s_mode = OLED_MODE_NONE;
static uint16_t s_last_frame = 0xFFFFU;
static uint32_t s_next_listening_refresh_ms = 0U;
static uint8_t s_oled_addr = OLED_I2C_ADDR_PRIMARY;

static void oled_hw_init(void);
static HAL_StatusTypeDef oled_detect_address(void);
static HAL_StatusTypeDef oled_write_command(uint8_t command);
static HAL_StatusTypeDef oled_write_data(const uint8_t *data, uint16_t length);
static HAL_StatusTypeDef oled_write_commands(const uint8_t *commands, uint16_t length);
static HAL_StatusTypeDef oled_flush_framebuffer(void);

void oled_init(void)
{
    static const uint8_t init_sequence[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide ratio */
        0xA8, 0x3F, /* multiplex ratio 1/64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* display start line */
        0x8D, 0x14, /* charge pump on */
        0x20, 0x00, /* horizontal addressing mode */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan dec */
        0xDA, 0x12, /* COM pins config */
        0x81, 0x8F, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOM detect */
        0xA4,       /* display follows RAM */
        0xA6,       /* normal display */
        0x2E,       /* deactivate scroll */
        0xAF        /* display on */
    };

    oled_hw_init();
    oled_canvas_render_idle(s_framebuffer);

    if (oled_detect_address() != HAL_OK) {
        printf("[OLED] init failed: no ACK at 0x3C/0x3D\r\n");
        s_initialized = 0;
        return;
    }

    if (oled_write_commands(init_sequence, (uint16_t)sizeof(init_sequence)) != HAL_OK) {
        printf("[OLED] init failed: command sequence error\r\n");
        s_initialized = 0;
        return;
    }
    if (oled_flush_framebuffer() != HAL_OK) {
        printf("[OLED] init failed: initial frame flush error\r\n");
        s_initialized = 0;
        return;
    }

    s_initialized = 1;
    s_dirty = 0;
    s_mode = OLED_MODE_IDLE;
    s_last_frame = 0xFFFFU;
    s_next_listening_refresh_ms = 0U;
    s_error_logged = 0;

    printf("[OLED] SSD1306 init complete (I2C1 PB8/PB9, addr=0x%02X)\r\n",
           (unsigned)s_oled_addr);
}

void oled_display_idle(void)
{
    if (s_mode != OLED_MODE_IDLE) {
        oled_canvas_render_idle(s_framebuffer);
        s_mode = OLED_MODE_IDLE;
        s_last_frame = 0xFFFFU;
        s_dirty = 1;
    }
}

void oled_display_listening(uint16_t *samples, uint16_t sample_count)
{
    uint32_t now = system_get_tick_ms();

    if (s_mode != OLED_MODE_LISTENING || now >= s_next_listening_refresh_ms) {
        oled_canvas_render_listening(s_framebuffer, samples, sample_count);
        s_mode = OLED_MODE_LISTENING;
        s_last_frame = 0xFFFFU;
        s_next_listening_refresh_ms = now + OLED_LISTEN_REFRESH_MS;
        s_dirty = 1;
    }
}

void oled_display_processing(uint16_t frame)
{
    if (s_mode != OLED_MODE_PROCESSING || frame != s_last_frame) {
        oled_canvas_render_processing(s_framebuffer, frame);
        s_mode = OLED_MODE_PROCESSING;
        s_last_frame = frame;
        s_dirty = 1;
    }
}

void oled_display_responding(const char *text, uint16_t frame)
{
    (void)text;

    if (s_mode != OLED_MODE_RESPONDING || frame != s_last_frame) {
        oled_canvas_render_responding(s_framebuffer, frame);
        s_mode = OLED_MODE_RESPONDING;
        s_last_frame = frame;
        s_dirty = 1;
    }
}

void oled_display_error(const char *error_message)
{
    (void)error_message;

    oled_canvas_render_error(s_framebuffer);
    s_mode = OLED_MODE_ERROR;
    s_last_frame = 0xFFFFU;
    s_dirty = 1;
}

void oled_update_display(void)
{
    if (!s_initialized) {
        if (!s_error_logged) {
            printf("[OLED] update skipped: display not initialized\r\n");
            s_error_logged = 1;
        }
        return;
    }
    if (!s_dirty) return;

    if (oled_flush_framebuffer() != HAL_OK) {
        printf("[OLED] frame flush failed\r\n");
        return;
    }
    s_dirty = 0;
}

static void oled_hw_init(void)
{
    GPIO_InitTypeDef g = {0};

    OLED_I2C_GPIO_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    g.Pin = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = OLED_I2C_AF;
    HAL_GPIO_Init(OLED_I2C_GPIO_PORT, &g);

    s_hi2c.Instance = OLED_I2C_INSTANCE;
    s_hi2c.Init.Timing = 0x10909CECU; /* 100 kHz @ 80 MHz SYSCLK on STM32L4 */
    s_hi2c.Init.OwnAddress1 = 0;
    s_hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c.Init.OwnAddress2 = 0;
    s_hi2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_hi2c) != HAL_OK) {
        printf("[OLED] HAL_I2C_Init failed\r\n");
        return;
    }

    if (HAL_I2CEx_ConfigAnalogFilter(&s_hi2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        printf("[OLED] I2C analog filter config failed\r\n");
        return;
    }

    if (HAL_I2CEx_ConfigDigitalFilter(&s_hi2c, 0) != HAL_OK) {
        printf("[OLED] I2C digital filter config failed\r\n");
    }
}

static HAL_StatusTypeDef oled_detect_address(void)
{
    if (HAL_I2C_IsDeviceReady(&s_hi2c,
                              (uint16_t)(OLED_I2C_ADDR_PRIMARY << 1U),
                              2U,
                              OLED_I2C_TIMEOUT_MS) == HAL_OK) {
        s_oled_addr = OLED_I2C_ADDR_PRIMARY;
        return HAL_OK;
    }
    if (HAL_I2C_IsDeviceReady(&s_hi2c,
                              (uint16_t)(OLED_I2C_ADDR_SECONDARY << 1U),
                              2U,
                              OLED_I2C_TIMEOUT_MS) == HAL_OK) {
        s_oled_addr = OLED_I2C_ADDR_SECONDARY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

static HAL_StatusTypeDef oled_write_command(uint8_t command)
{
    uint8_t packet[2];
    packet[0] = OLED_I2C_CONTROL_COMMAND;
    packet[1] = command;
    return HAL_I2C_Master_Transmit(&s_hi2c,
                                   (uint16_t)(s_oled_addr << 1U),
                                   packet,
                                   (uint16_t)sizeof(packet),
                                   OLED_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef oled_write_data(const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t offset = 0U;
    uint8_t packet[17];

    if (data == NULL || length == 0U) return HAL_OK;

    while (offset < length) {
        uint16_t chunk = (uint16_t)(length - offset);
        uint16_t i;
        if (chunk > 16U) chunk = 16U;

        packet[0] = OLED_I2C_CONTROL_DATA;
        for (i = 0U; i < chunk; ++i) {
            packet[1U + i] = data[offset + i];
        }

        status = HAL_I2C_Master_Transmit(&s_hi2c,
                                         (uint16_t)(s_oled_addr << 1U),
                                         packet,
                                         (uint16_t)(1U + chunk),
                                         OLED_I2C_TIMEOUT_MS);
        if (status != HAL_OK) return status;
        offset = (uint16_t)(offset + chunk);
    }

    return status;
}

static HAL_StatusTypeDef oled_write_commands(const uint8_t *commands, uint16_t length)
{
    uint16_t i;
    HAL_StatusTypeDef status = HAL_OK;

    if (commands == NULL || length == 0U) return HAL_OK;
    for (i = 0U; i < length; ++i) {
        status = oled_write_command(commands[i]);
        if (status != HAL_OK) return status;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef oled_flush_framebuffer(void)
{
    HAL_StatusTypeDef status;
    uint8_t page;

    for (page = 0U; page < OLED_PAGE_COUNT; ++page) {
        uint8_t commands[3];
        uint8_t row[OLED_CANVAS_WIDTH];
        uint8_t x;

        commands[0] = (uint8_t)(0xB0U + page);
        commands[1] = 0x00U;
        commands[2] = 0x10U;
        status = oled_write_commands(commands, (uint16_t)sizeof(commands));
        if (status != HAL_OK) return status;

        for (x = 0U; x < OLED_CANVAS_WIDTH; ++x) {
            uint8_t bit = 0U;
            uint8_t b;
            for (b = 0U; b < 8U; ++b) {
                uint8_t y = (uint8_t)(page * 8U + b);
                if (oled_canvas_get_pixel(s_framebuffer, x, y)) {
                    bit |= (uint8_t)(1U << b);
                }
            }
            row[x] = bit;
        }

        status = oled_write_data(row, (uint16_t)sizeof(row));
        if (status != HAL_OK) return status;
    }

    return HAL_OK;
}
