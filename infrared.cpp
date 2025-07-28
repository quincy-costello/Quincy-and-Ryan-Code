#include <M5Unified.h>

// GPIO pin for IR LED — change if needed
const int IR_LED_PIN = 26;
const int IR_RX_PIN = 36;

// PWM config
const int pwmChannel = 0;
const int pwmFreq = 38000;  // 38 kHz
const int pwmResolution = 8;  // 8-bit


// Use Serial2 for IR input (ESP32 has multiple UARTs)
HardwareSerial IRSerial(2);

/*
// Self-filtering buffer
struct SentByte {
    uint8_t value;
    unsigned long timestamp;
};
SentByte recentSent[5];
int sentIndex = 0;

void logSentByte(uint8_t b) {
    recentSent[sentIndex] = { b, millis() };
    sentIndex = (sentIndex + 1) % 5;
}

bool isSelfEcho(uint8_t received) {
    unsigned long now = millis();
    for (int i = 0; i < 5; i++) {
        if (recentSent[i].value == received && (now - recentSent[i].timestamp) < 5) {
            return true;
        }
    }
    return false;
}
*/

void sendBit(int bit) {
    // Transmit "0" = 38kHz ON for ~417 μs _______________________________
    ledcWrite(pwmChannel, bit?0:128);  // 50% duty
    delayMicroseconds(417);
}

void sendByte(uint8_t byte) {
    sendBit(0); // to get the attention of the receiver
    
    for (int i = 0; i < 8; i++) {
        sendBit(byte%2); byte /= 2;
    }

    sendBit(1); // to get the attention of the receiver for the next byte
}

void setup() {
    // Initialize M5Unified system
    auto cfg = M5.config();
    M5.begin(cfg);

    // Set up PWM on IR_LED_PIN
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(IR_LED_PIN, pwmChannel);

    // Set up display
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE, BLACK);

    IRSerial.begin(2400, SERIAL_8N1, IR_RX_PIN, -1);
    Serial.begin(115200);  // USB debug
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        M5.Lcd.println("Sending IR signal");
        sendByte(148);
        sendByte(185);
        sendByte(63);
    }

    else if (IRSerial.available()) {
        char c = IRSerial.read();
    
        // Show on M5 screen
        M5.Lcd.printf("%d\n", c);
    
        // Also print to Serial Monitor
        Serial.print(c);
    }
}
// event loop: schedule of events