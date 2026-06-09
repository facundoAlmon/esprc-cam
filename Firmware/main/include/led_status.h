#pragma once

// Onboard LED (GPIO 33, active LOW) state machine.
// Patterns:
//   BOOT      — 1 Hz slow blink  (ESP booting, camera not yet init)
//   RETRY     — 5 Hz fast blink  (camera init retry or DMA reinit)
//   READY     — solid ON         (camera OK, no active stream client)
//   STREAMING — 2 Hz medium blink (client connected, streaming frames)
typedef enum {
    LED_BOOT      = 0,
    LED_RETRY     = 1,
    LED_READY     = 2,
    LED_STREAMING = 3,
} led_state_t;

// Configure GPIO 33 and start the LED driver task. Call before any other
// module so the boot blink is visible from the very first instruction.
void led_status_init(void);

// Update the LED state (thread-safe, callable from any context).
void led_status_set(led_state_t s);
