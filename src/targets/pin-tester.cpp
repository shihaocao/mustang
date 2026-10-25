// Pin tester for the 38-pin ESP32 DevKitC-style board.
// Every safe GPIO is an input with the internal pull-up on, so touching a
// ground wire to a header pin flips it HIGH -> LOW and prints which GPIO it is.
//
// The main loop runs three sections in order:
//   Read     - sample the pins into the global state.
//   Control  - detect level changes and print them.
//   Actuate  - drive the pins from the state (nothing to drive here).

#include <Arduino.h>

constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;

struct Pin {
    uint8_t gpio;
    const char* location;  // Header position, components up, USB at bottom.
};

// Excluded: GPIO1/3 (USB serial), GPIO6-11 (flash), and GPIO34-39
// (input-only with no pull-up, so they float and flip constantly).
static const Pin PINS[] = {
    {32, "left 7"},   {33, "left 8"},   {25, "left 9"},   {26, "left 10"},
    {27, "left 11"},  {14, "left 12"},  {12, "left 13"},  {13, "left 15"},
    {23, "right 2"},  {22, "right 3"},  {21, "right 6"},  {19, "right 8"},
    {18, "right 9"},  {5, "right 10"},  {17, "right 11"}, {16, "right 12"},
    {4, "right 13"},  {0, "right 14"},  {2, "right 15"},  {15, "right 16"},
};
constexpr size_t NUM_PINS = sizeof(PINS) / sizeof(PINS[0]);

struct State {
    // Inputs, written by Read.
    uint32_t now_ms = 0;
    bool level[NUM_PINS] = {};  // true = HIGH.

    // Control state.
    bool prev_level[NUM_PINS] = {};
    bool initialized = false;
    uint32_t last_heartbeat_ms = 0;
};

static State state;

// Prints "[s.mmm] " with time since boot.
static void printTimestamp(uint32_t ms) {
    Serial.printf("[%lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

static void read() {
    state.now_ms = millis();
    for (size_t i = 0; i < NUM_PINS; i++) {
        state.level[i] = digitalRead(PINS[i].gpio) == HIGH;
    }
}

// Prints every pin's level once at startup.
static void print_all_levels() {
    for (size_t i = 0; i < NUM_PINS; i++) {
        printTimestamp(state.now_ms);
        Serial.printf("GPIO%-2u (%-8s) %s\r\n", PINS[i].gpio, PINS[i].location,
                      state.level[i] ? "HIGH" : "LOW");
    }
}

// Prints any pin whose level differs from the previous loop.
static void print_changes() {
    for (size_t i = 0; i < NUM_PINS; i++) {
        if (state.level[i] != state.prev_level[i]) {
            printTimestamp(state.now_ms);
            Serial.printf("GPIO%-2u (%-8s) %s -> %s\r\n", PINS[i].gpio, PINS[i].location,
                          state.prev_level[i] ? "HIGH" : "LOW",
                          state.level[i] ? "HIGH" : "LOW");
        }
    }
}

// Prints which pins are LOW every HEARTBEAT_INTERVAL_MS, to show it's alive.
static void print_heartbeat() {
    if (state.now_ms - state.last_heartbeat_ms < HEARTBEAT_INTERVAL_MS) {
        return;
    }
    state.last_heartbeat_ms = state.now_ms;
    printTimestamp(state.now_ms);
    Serial.print("alive, LOW pins:");
    for (size_t i = 0; i < NUM_PINS; i++) {
        if (!state.level[i]) {
            Serial.printf(" GPIO%u", PINS[i].gpio);
        }
    }
    Serial.print("\r\n");
}

static void control() {
    if (!state.initialized) {
        print_all_levels();
        state.initialized = true;
        state.last_heartbeat_ms = state.now_ms;
    } else {
        print_changes();
        print_heartbeat();
    }
    memcpy(state.prev_level, state.level, sizeof(state.level));
}

static void actuate() {
    // Nothing to drive: every pin is an input.
}

void setup() {
    Serial.begin(115200);
    for (size_t i = 0; i < NUM_PINS; i++) {
        pinMode(PINS[i].gpio, INPUT_PULLUP);
    }
    Serial.println("pin-tester: starting");
}

void loop() {
    read();
    control();
    actuate();
}
