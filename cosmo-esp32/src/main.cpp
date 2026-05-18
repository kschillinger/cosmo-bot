/**
 * Cosmo Bot — ESP32-C3-DevKitM-1
 * ============================================================================
 * Phase 3: dialogue engine bring-up.
 *
 * Responsibilities (this phase):
 *   - Open Serial1 at 115200 8N1 on GPIO4 (TX1) / GPIO5 (RX1) for the link
 *     to the STM32L476.
 *   - Use Serial (native USB-CDC) for debug printing to the host PC.
 *   - Listen for "[USER_INPUT: <text>]\n" lines on Serial1.
 *   - Route the parsed text through dialogue_respond() (intents.cpp +
 *     dialogue.cpp), then reply with "[BOT_RESPONSE: ...]\r\n".
 *
 * Wiring (unchanged from Phase 1):
 *   GPIO4 (TX1) -> Nucleo PA10 (USART1_RX)
 *   GPIO5 (RX1) <- Nucleo PA9  (USART1_TX)
 *   GND         <-> Nucleo GND
 *
 * Build / flash / monitor:
 *   pio run -e esp32-c3-devkitm-1 -t upload
 *   pio device monitor -e esp32-c3-devkitm-1
 *
 * USB-CDC startup — why the boilerplate in setup()
 * ------------------------------------------------
 * The C3's native USB enumerates as a CDC device. Two things conspire to
 * eat output on every flash if not handled:
 *
 *   1. Serial.print() BLOCKS indefinitely when the host hasn't opened the
 *      port. Without Serial.setTxTimeoutMs(0), the sketch hangs forever
 *      if you upload without `pio device monitor` already running.
 *
 *   2. The host takes ~700–1500 ms after USB enumeration to open the COM
 *      port for the first time. Anything printed in that window is dropped.
 *      A 2000 ms warm-up delay + Serial.flush() gives the host plenty of
 *      time to attach and see the startup banner.
 *
 * If you flash and the monitor stays blank:
 *   - Confirm platformio.ini has ARDUINO_USB_MODE=1, ARDUINO_USB_CDC_ON_BOOT=1
 *     and monitor_dtr/rts = 0.
 *   - Unplug + replug the USB-C cable, then `pio device monitor`.
 *   - The chip should enumerate as VID:PID 303A:1001 (Espressif USB JTAG/
 *     Serial). Check the OS device list (Windows Device Manager, or
 *     `ls /dev/ttyACM*` on Linux).
 *
 * Notes on ESP32-C3 GPIO selection:
 *   - GPIO 2, 8, 9 are strapping pins — avoid for general use.
 *   - GPIO 18, 19 are wired to the USB-C connector for native USB.
 *   - GPIO 4, 5 are general-purpose and convenient on the DevKitM-1 header.
 * ============================================================================
 */

#include <Arduino.h>
#include <HardwareSerial.h>

#include "dialogue.h"

/* ---- Pin / config -------------------------------------------------------- */
static constexpr int  LINK_TX_PIN = 4;        // ESP32 TX1 -> STM32 RX (PA10)
static constexpr int  LINK_RX_PIN = 5;        // ESP32 RX1 <- STM32 TX (PA9)
static constexpr long LINK_BAUD   = 115200;

HardwareSerial& Link = Serial1;

/* ---- Line buffer --------------------------------------------------------- */
static constexpr size_t MAX_LINE = 256;
static char   lineBuf[MAX_LINE];
static size_t linePos = 0;

/* ========================================================================== */
static void sendBotResponse(const char* text)
{
    Link.print("[BOT_RESPONSE: ");
    Link.print(text);
    Link.print("]\r\n");
    Serial.printf("[ESP32] -> BOT_RESPONSE: %s\n", text);
}

static void handleLine(const char* line)
{
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

    /* Local mutable copy: dialogue_respond() normalizes in place
     * (lowercases + replaces punctuation with spaces). We want to log
     * the original text first, then hand a writable copy to the engine. */
    char userText[MAX_LINE];
    size_t len = (size_t)(end - start);
    if (len >= sizeof(userText)) len = sizeof(userText) - 1;
    memcpy(userText, start, len);
    userText[len] = '\0';

    Serial.printf("[ESP32] parsed user text: \"%s\"\n", userText);

    DialogueResult r = dialogue_respond(userText);
    Serial.printf("[ESP32] classified -> %s  conf=%u  resp=\"%s\"\n",
                  intent_name(r.intent), (unsigned)r.confidence, r.response);

    sendBotResponse(r.response);
}

/* ========================================================================== */
void setup()
{
    /* Make Serial.print() non-blocking when the host hasn't opened the
     * USB-CDC port. Without this, the sketch hangs forever if you flash
     * without the monitor already running. */
    Serial.setTxTimeoutMs(0);
    Serial.begin(115200);

    /* Give the host's USB-CDC stack time to enumerate AND open the port.
     * The C3 boots much faster than the host driver can attach, and any
     * prints in the enumeration window get dropped silently. 2000 ms is
     * comfortable across Windows/Linux/macOS host stacks. */
    delay(2000);

    Serial.println();
    Serial.println("==========================================");
    Serial.println(" Cosmo Bot — ESP32-C3  Phase 3 (dialogue)");
    Serial.printf (" Link UART: TX=GPIO%d  RX=GPIO%d  @ %ld baud\n",
                   LINK_TX_PIN, LINK_RX_PIN, LINK_BAUD);
    Serial.println("==========================================");
    Serial.flush();   /* push the banner out before we touch anything else  */

    Link.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
    dialogue_init();

    Serial.println("[ESP32] dialogue engine ready");
}

void loop()
{
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
            // overrun — resync at the next newline
            linePos = 0;
        }
    }
}
