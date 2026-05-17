/**
 * Cosmo Bot — ESP32-C3-DevKitM-1
 * ============================================================================
 * Phase 1: UART link bring-up
 *
 * Responsibilities (this phase):
 *   - Open Serial1 at 115200 8N1 on GPIO4 (TX1) / GPIO5 (RX1) for the link
 *     to the STM32L476.
 *   - Use Serial (native USB-CDC) for debug printing to the host PC.
 *   - Listen for "[USER_INPUT: <text>]\n" lines on Serial1.
 *   - Reply with a hardcoded "[BOT_RESPONSE: ...]\r\n" line.
 *
 * Wiring:
 *   GPIO4 (TX1) -> Nucleo PA10 (USART1_RX)
 *   GPIO5 (RX1) <- Nucleo PA9  (USART1_TX)
 *   GND         <-> Nucleo GND
 *
 * Build / flash / monitor:
 *   pio run -e esp32-c3-devkitm-1 -t upload
 *   pio device monitor -e esp32-c3-devkitm-1
 *
 * Notes on ESP32-C3 GPIO selection:
 *   - GPIO 2, 8, 9 are strapping pins — avoid for general use.
 *   - GPIO 18, 19 are wired to the USB-C connector for native USB.
 *   - GPIO 4, 5 are general-purpose and convenient on the DevKitM-1 header.
 * ============================================================================
 */

#include "Arduino.h"
#include <HardwareSerial.h>

/* ---- Pin / config -------------------------------------------------------- */
static constexpr int  LINK_TX_PIN = 4;        // ESP32 TX1 -> STM32 RX (PA10)
static constexpr int  LINK_RX_PIN = 5;        // ESP32 RX1 <- STM32 TX (PA9)
static constexpr long LINK_BAUD   = 115200;

HardwareSerial& Link = Serial1;

/* ---- Line buffer --------------------------------------------------------- */
static constexpr size_t MAX_LINE = 256;
static char   lineBuf[MAX_LINE];
static size_t linePos = 0;

/* Hardcoded Phase 1 response. Phase 3 replaces this with the intent engine. */
static const char* PHASE1_REPLY =
    "Hello from ESP32-C3! UART link is alive.";

/* ========================================================================== */
static void sendBotResponse(const char* text) {
    Link.print("[BOT_RESPONSE: ");
    Link.print(text);
    Link.print("]\r\n");
    Serial.printf("[ESP32] -> BOT_RESPONSE: %s\n", text);
}

static void handleLine(const char* line) {
    Serial.printf("[ESP32] <- line: %s\n", line);

    static const char prefix[] = "[USER_INPUT: ";
    constexpr size_t  prefixLen = sizeof(prefix) - 1;

    if (strncmp(line, prefix, prefixLen) != 0) {
        Serial.println("[ESP32] (not a USER_INPUT — ignoring)");
        return;
    }

    const char* start = line + prefixLen;
    const char* end   = strrchr(start, ']');
    if (!end || end <= start) {
        Serial.println("[ESP32] malformed USER_INPUT — no closing ']'");
        return;
    }

    char userText[MAX_LINE];
    size_t len = (size_t)(end - start);
    if (len >= sizeof(userText)) len = sizeof(userText) - 1;
    memcpy(userText, start, len);
    userText[len] = '\0';

    Serial.printf("[ESP32] parsed user text: \"%s\"\n", userText);

    /* Phase 1: ignore content, always reply with the hardcoded line. */
    sendBotResponse(PHASE1_REPLY);
}

/* ========================================================================== */
void setup() {
    Serial.begin(115200);
    delay(200);  // give USB-CDC a moment to enumerate
    Serial.println();
    Serial.println("==========================================");
    Serial.println(" Cosmo Bot — ESP32-C3  Phase 1");
    Serial.printf (" Link UART: TX=GPIO%d  RX=GPIO%d  @ %ld baud\n",
                   LINK_TX_PIN, LINK_RX_PIN, LINK_BAUD);
    Serial.println("==========================================");

    Link.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
}

void loop() {
    while (Link.available()) {
        int b = Link.read();
        if (b < 0) break;
        char c = (char)b;

        if (c == '\n') {
            lineBuf[linePos] = '\0';
            if (linePos > 0 && lineBuf[linePos - 1] == '\r') {
                lineBuf[linePos - 1] = '\0';
            }
            if (linePos > 0) {
                handleLine(lineBuf);
            }
            linePos = 0;
        } else if (linePos < MAX_LINE - 1) {
            lineBuf[linePos++] = c;
        } else {
            // overrun — resync
            linePos = 0;
        }
    }
}
