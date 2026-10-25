// Open-drain switch test for ESP32-WROOM.
// The switch pin is open-drain: OPEN lets an external pull-up (3.3 V max) hold
// the line high, CLOSED sinks it to ground. It starts OPEN. When the trigger
// input is pulled to ground, the switch closes for a fixed pulse. The trigger
// must return high before the next pulse can fire.
//
// The main loop runs three sections in order:
//   Read     - sample the pins into the global state.
//   Control  - decide outputs from the state.
//   Actuate  - drive the pins from the state.

#include <Arduino.h>

constexpr uint8_t SWITCH_PIN = 16;   // Open-drain, external pull-up to 3V3 or less.
constexpr uint8_t TRIGGER_PIN = 27;  // Active low, internal pull-up (plus external 22k to 3V3).

constexpr uint32_t PULSE_MS = 1000;
constexpr uint32_t DEBOUNCE_MS = 50;
constexpr uint32_t STATUS_INTERVAL_MS = 100;  // 10 Hz.

enum class Mode {
    ARMED,         // Waiting for the trigger to go low.
    FIRING,        // Switch closed, waiting for the pulse to elapse.
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
    bool switch_closed = false;

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
    Serial.printf("(mode=%s, switch=%s, trigger=%s)\r\n",
                  modeName(state.mode),
                  state.switch_closed ? "CLOSED" : "OPEN",
                  state.trigger_grounded ? "GROUNDED" : "OPEN");
}

static void control() {
    debounce_trigger();
    print_state();

    switch (state.mode) {
        case Mode::ARMED:
            if (state.trigger_pressed) {
                state.switch_closed = true;
                enter_mode(Mode::FIRING);
            }
            break;

        case Mode::FIRING:
            if (state.now_ms - state.mode_entered_ms >= PULSE_MS) {
                state.switch_closed = false;
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
    // Open-drain: LOW sinks to ground, HIGH releases the line.
    digitalWrite(SWITCH_PIN, state.switch_closed ? LOW : HIGH);
}

void setup() {
    Serial.begin(115200);
    // Set the output latch high (released) before enabling the driver so the
    // line is never pulled low by a glitch.
    digitalWrite(SWITCH_PIN, HIGH);
    pinMode(SWITCH_PIN, OUTPUT_OPEN_DRAIN);
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    Serial.println("switch-test: starting");
}

void loop() {
    read();
    control();
    actuate();
}
