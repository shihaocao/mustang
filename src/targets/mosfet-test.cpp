// MOSFET switching test for ESP32-WROOM.
// When the trigger input is pulled to ground, the MOSFET switches at
// PWM_FREQ_HZ for a fixed pulse. The trigger must return high before the next pulse can fire.
//
// The main loop runs three sections in order:
//   Read     - sample the pins into the global state.
//   Control  - decide outputs from the state.
//   Actuate  - drive the pins from the state.

#include <Arduino.h>

// constexpr uint8_t MOSFET_GATE_PIN = 25;
// constexpr uint8_t TRIGGER_PIN = 16;  // Active low, internal pull-up (plus external 22k to 3V3).
constexpr uint8_t MOSFET_GATE_PIN = 16;
constexpr uint8_t TRIGGER_PIN = 27;  // Active low, internal pull-up (plus external 22k to 3V3).
// constexpr uint8_t TRIGGER_PIN = ;  // Active low, internal pull-up (plus external 22k to 3V3).


constexpr uint32_t PULSE_MS = 1000;

// Hardware PWM (LEDC) on the gate while firing.
constexpr uint8_t PWM_CHANNEL = 0;
constexpr uint32_t PWM_FREQ_HZ = 100;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint32_t PWM_DUTY = 128;  // 50% of 2^PWM_RESOLUTION_BITS.
constexpr uint32_t DEBOUNCE_MS = 50;
constexpr uint32_t STATUS_INTERVAL_MS = 100;  // 10 Hz.

enum class Mode {
    ARMED,         // Waiting for the trigger to go low.
    FIRING,        // MOSFET switching, waiting for the pulse to elapse.
    WAIT_RELEASE,  // Waiting for the trigger to return high.
};

struct State {
    // Inputs, written by Read.
    uint32_t now_ms = 0;
    bool trigger_grounded = false;  // Raw pin level.

    // Control state.
    // Start as if the trigger were pressed so it must read released for
    // DEBOUNCE_MS before arming. A trigger held low at boot never fires.
    Mode mode = Mode::WAIT_RELEASE;
    uint32_t mode_entered_ms = 0;
    bool trigger_pressed = true;  // Debounced.
    bool trigger_last_raw = true;
    uint32_t trigger_last_change_ms = 0;

    // Outputs, written by Control.
    bool mosfet_pulsing = false;

    // Bookkeeping for print_state.
    uint32_t last_status_ms = 0;
};

static State state;

// Prints "[s.mmm] " with time since boot.
static void printTimestamp(uint32_t ms) {
    Serial.printf("[%lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::ARMED: return "ARMED";
        case Mode::FIRING: return "FIRING";
        case Mode::WAIT_RELEASE: return "WAIT_RELEASE";
    }
    return "?";
}

static void read() {
    state.now_ms = millis();
    state.trigger_grounded = digitalRead(TRIGGER_PIN) == LOW;
}

// Updates trigger_pressed once the raw level has held for DEBOUNCE_MS.
static void debounce_trigger() {
    if (state.trigger_grounded != state.trigger_last_raw) {
        state.trigger_last_raw = state.trigger_grounded;
        state.trigger_last_change_ms = state.now_ms;
    } else if (state.now_ms - state.trigger_last_change_ms >= DEBOUNCE_MS) {
        state.trigger_pressed = state.trigger_grounded;
    }
}

static void enter_mode(Mode next) {
    state.mode = next;
    state.mode_entered_ms = state.now_ms;
    printTimestamp(state.now_ms);
    Serial.printf("mode: %s\r\n", modeName(next));
}

// Prints the state at STATUS_INTERVAL_MS.
static void print_state() {
    if (state.now_ms - state.last_status_ms < STATUS_INTERVAL_MS) {
        return;
    }
    state.last_status_ms = state.now_ms;
    printTimestamp(state.now_ms);
    Serial.printf("(mode=%s, mosfet=%s, trigger=%s)\r\n",
                  modeName(state.mode),
                  state.mosfet_pulsing ? "PULSING" : "OFF",
                  state.trigger_grounded ? "GROUNDED" : "OPEN");
}

static void control() {
    debounce_trigger();
    print_state();

    switch (state.mode) {
        case Mode::ARMED:
            if (state.trigger_pressed) {
                state.mosfet_pulsing = true;
                enter_mode(Mode::FIRING);
            }
            break;

        case Mode::FIRING:
            if (state.now_ms - state.mode_entered_ms >= PULSE_MS) {
                state.mosfet_pulsing = false;
                enter_mode(Mode::WAIT_RELEASE);
            }
            break;

        case Mode::WAIT_RELEASE:
            if (!state.trigger_pressed) {
                enter_mode(Mode::ARMED);
            }
            break;
    }

}

static void actuate() {
    ledcWrite(PWM_CHANNEL, state.mosfet_pulsing ? PWM_DUTY : 0);
}

void setup() {
    Serial.begin(115200);
    // Set duty to 0 before attaching the pin so the gate never sees a high
    // glitch.
    uint32_t pwm_freq = ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcWrite(PWM_CHANNEL, 0);
    ledcAttachPin(MOSFET_GATE_PIN, PWM_CHANNEL);
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    Serial.println("mosfet-test: starting");
    // ledcSetup returns the actual frequency, or 0 if it can't be configured.
    Serial.printf("pwm: requested %lu Hz, got %lu Hz\r\n",
                  (unsigned long)PWM_FREQ_HZ, (unsigned long)pwm_freq);
}

void loop() {
    read();
    control();
    actuate();
}
