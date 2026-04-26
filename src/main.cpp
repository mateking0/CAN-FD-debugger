#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ACAN2517FD.h>
#include <ESP8266WiFi.h>  
#include <WiFiUdp.h>
#include "hardware_config.h"

// --- Halozat settings ---
const char* ssid = "A_TI_WIFI_HALOZATOTOK";
const char* password = "A_TI_WIFI_JELSZAVATOK";
const unsigned int udpPort = 8888; // Ezen a porton kommunikalunk a PC-vel
IPAddress pcIP(192, 168, 1, 100);  // PC ip cime

WiFiUDP udp;
char packetBuffer[255]; // Puffer a bejovo Wi-Fi adatoknak

// --- CAN FD settings  ---
ACAN2517FD can(CAN_CS_PIN, SPI, CAN_INT_PIN);

// --- Runtime allapotok ---
bool udpEnabled = false;     // Indulaskor OFF
bool sdReady = false;
bool canReady = false;
bool canBusError = false;
uint32_t sdWriteErrors = 0;
uint16_t canTxFailStreak = 0;

const char* canLogFilePath = "/can_log.csv";
const uint32_t logFlushIntervalMs = 1000;
uint32_t lastLogFlushMs = 0;

const uint32_t rxLedFlashMs = 80;
uint32_t rxLedFlashUntilMs = 0;

const uint16_t canTxFailErrorThreshold = 20;
const uint32_t canReinitIntervalMs = 2000;
uint32_t lastCanInitAttemptMs = 0;

// A kapcsolas szerint LED_A aktiv alacsony, LED_B aktiv magas.
const bool ledAActiveLow = true;
const bool ledBActiveLow = false;

// ADC gomb: elengedve ~0V, nyomva ~0.8V
const uint16_t adcPressThreshold = 700;
const uint16_t adcReleaseThreshold = 250;
const uint32_t buttonDebounceMs = 40;

bool buttonRawPressed = false;
bool buttonStablePressed = false;
uint32_t buttonLastChangeMs = 0;

File logFile;

void setLed(uint8_t pin, bool on, bool activeLow) {
    digitalWrite(pin, (on ^ activeLow) ? HIGH : LOW);
}

void updateStatusLeds() {
    if (canBusError) {
        setLed(LED_A, true, ledAActiveLow);
        setLed(LED_B, true, ledBActiveLow);
        return;
    }

    const bool rxBlinkActive = millis() < rxLedFlashUntilMs;
    setLed(LED_A, rxBlinkActive, ledAActiveLow);
    setLed(LED_B, udpEnabled, ledBActiveLow);
}

void noteCanTxResult(bool success) {
    if (success) {
        canTxFailStreak = 0;
        return;
    }

    if (canTxFailStreak < 0xFFFF) {
        canTxFailStreak++;
    }

    if (canTxFailStreak >= canTxFailErrorThreshold) {
        if (!canBusError) {
            Serial.println("CAN hiba allapot: tul sok sikertelen TX kiserlet.");
        }
        canBusError = true;
    }
}

void markCanHealthyTraffic() {
    canTxFailStreak = 0;
    if (canBusError) {
        canBusError = false;
        Serial.println("CAN hiba visszavonva, normal uzem folytatodik.");
    }
}

void tryInitializeCan(bool forceAttempt) {
    if (!forceAttempt && (millis() - lastCanInitAttemptMs < canReinitIntervalMs)) {
        return;
    }

    lastCanInitAttemptMs = millis();

    ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_40MHz, 500UL * 1000UL, DataBitRateFactor::x4);
    const uint32_t errorCode = can.begin(settings, [] { can.isr(); });

    if (errorCode == 0) {
        const bool recovered = !canReady;
        canReady = true;
        canBusError = false;
        canTxFailStreak = 0;
        if (recovered) {
            Serial.println("CAN FD ujrainicializalas sikeres.");
        } else if (forceAttempt) {
            Serial.println("CAN FD sikeresen inicializalva.");
        }
    } else {
        canReady = false;
        canBusError = true;
        if (forceAttempt) {
            Serial.printf("Hiba a CAN indulaskor: 0x%X\n", errorCode);
            Serial.println("CAN hibaban indul, automatikus ujraprobalas aktiv.");
        } else {
            Serial.printf("CAN ujrainicializalas sikertelen: 0x%X\n", errorCode);
        }
    }
}

void formatDataHex(const CANFDMessage& frame, char* outBuffer, size_t outSize) {
    size_t idx = 0;
    for (uint8_t i = 0; i < frame.len && idx + 3 < outSize; i++) {
        idx += snprintf(&outBuffer[idx], outSize - idx, "%02X", frame.data[i]);
        if (i + 1 < frame.len && idx + 2 < outSize) {
            outBuffer[idx++] = ' ';
            outBuffer[idx] = '\0';
        }
    }
}

bool initSdLogging() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD init hiba, log letiltva.");
        return false;
    }

    logFile = SD.open(canLogFilePath, FILE_WRITE);
    if (!logFile) {
        Serial.println("SD logfajl nyitasi hiba, log letiltva.");
        return false;
    }

    if (logFile.size() == 0) {
        logFile.println("timestamp_ms,direction,id,len,data");
    }
    logFile.flush();
    lastLogFlushMs = millis();

    Serial.println("SD logolas aktiv.");
    return true;
}

void flushLogIfDue() {
    if (sdReady && logFile && (millis() - lastLogFlushMs >= logFlushIntervalMs)) {
        logFile.flush();
        lastLogFlushMs = millis();
    }
}

void logCanEvent(const CANFDMessage& frame, const char* direction) {
    if (!sdReady || !logFile) {
        return;
    }

    char dataHex[3 * 64 + 1] = {0};
    formatDataHex(frame, dataHex, sizeof(dataHex));

    char line[512] = {0};
    snprintf(line, sizeof(line), "%lu,%s,0x%lX,%u,%s",
             millis(), direction, static_cast<unsigned long>(frame.id), frame.len, dataHex);

    if (logFile.println(line) == 0) {
        sdWriteErrors++;
        if (sdWriteErrors <= 5 || (sdWriteErrors % 50 == 0)) {
            Serial.printf("SD log irasi hiba (db: %lu)\n", static_cast<unsigned long>(sdWriteErrors));
        }
    }
}

void updateUdpToggleButton() {
    const uint16_t adcValue = analogRead(ADC_BUTTON_PIN);

    bool newRawPressed = buttonRawPressed;
    if (buttonRawPressed) {
        if (adcValue <= adcReleaseThreshold) {
            newRawPressed = false;
        }
    } else {
        if (adcValue >= adcPressThreshold) {
            newRawPressed = true;
        }
    }

    if (newRawPressed != buttonRawPressed) {
        buttonRawPressed = newRawPressed;
        buttonLastChangeMs = millis();
    }

    if ((millis() - buttonLastChangeMs >= buttonDebounceMs) && (buttonStablePressed != buttonRawPressed)) {
        buttonStablePressed = buttonRawPressed;

        if (buttonStablePressed) {
            udpEnabled = !udpEnabled;
            Serial.printf("UDP adatkuldes: %s\n", udpEnabled ? "ON" : "OFF");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- CAN FD Debug Tool Indulása ---");
    Serial.println("UDP alapallapot: OFF");

    pinMode(LED_A, OUTPUT);
    pinMode(LED_B, OUTPUT);
    setLed(LED_A, false, ledAActiveLow);
    setLed(LED_B, false, ledBActiveLow);

    // 1. Wi-Fi 
    WiFi.begin(ssid, password);
    Serial.print("Csatlakozás a Wi-Fi hálózathoz...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi csatlakoztatva! IP cím: ");
    Serial.println(WiFi.localIP());

    // 2. UDP szerver start
    udp.begin(udpPort);
    Serial.printf("UDP szerver hallgat a %d porton.\n", udpPort);

    // 3. CAN FD initialize
    SPI.begin();
    SPI.pins(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);

    // 4. SD initialize (mindig logolni probalunk)
    sdReady = initSdLogging();
    
    tryInitializeCan(true);

    updateStatusLeds();
}

// CAN message formatting
String formatCanMessage(const CANFDMessage& frame) {
    String msg = "RX -> ID: " + String(frame.id, HEX) + " Len: " + String(frame.len) + " Data: ";
    for (int i = 0; i < frame.len; i++) {
        if (frame.data[i] < 0x10) msg += "0";
        msg += String(frame.data[i], HEX) + " ";
    }
    return msg;
}

void loop() {
    
    CANFDMessage frame;
    String outputMsg = "";

    updateUdpToggleButton();
    flushLogIfDue();

    if (!canReady) {
        tryInitializeCan(false);
    }

    updateStatusLeds();

    // CAN read
    if (canReady && can.receive(frame)) {
        outputMsg = formatCanMessage(frame);
        logCanEvent(frame, "RX");
        rxLedFlashUntilMs = millis() + rxLedFlashMs;
        markCanHealthyTraffic();
        
        // USB-n (Virtuális soros port)
        Serial.println(outputMsg);
        
        // Wi-Fi-n (UDP)
        if (udpEnabled) {
            udp.beginPacket(pcIP, udpPort);
            udp.print(outputMsg);
            udp.endPacket();
        }
    }

    // CAN send USB-n
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "SEND_TEST") {
            CANFDMessage txFrame;
            txFrame.id = 0x100;
            txFrame.len = 8;
            for(int i=0; i<8; i++) txFrame.data[i] = i;

            const bool txOk = canReady && can.tryToSend(txFrame);
            noteCanTxResult(txOk);
            if (txOk) {
                markCanHealthyTraffic();
                Serial.println("Teszt üzenet injektálva USB parancsra!");
                logCanEvent(txFrame, "TX");
            } else {
                Serial.println("CAN TX hiba USB parancsnal.");
            }
        }
    }

    // CAN send Wi-Fi-n
    int packetSize = udp.parsePacket();
    if (packetSize) {
        if (udpEnabled) {
            int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
            if (len > 0) {
                packetBuffer[len] = '\0';
            } else {
                packetBuffer[0] = '\0';
            }

            String command = String(packetBuffer);
            command.trim();

            if (command == "SEND_TEST") {
                CANFDMessage txFrame;
                txFrame.id = 0x200;
                txFrame.len = 8;
                for (int i = 0; i < 8; i++) txFrame.data[i] = 0xFF;

                const bool txOk = canReady && can.tryToSend(txFrame);
                noteCanTxResult(txOk);
                if (txOk) {
                    markCanHealthyTraffic();
                    Serial.println("Teszt üzenet injektálva Wi-Fi parancsra!");
                    logCanEvent(txFrame, "TX");
                } else {
                    Serial.println("CAN TX hiba Wi-Fi parancsnal.");
                }
            }
        } else {
            // UDP kikapcsolt allapotban a csomagokat eldobjuk.
            while (udp.available()) {
                udp.read(packetBuffer, sizeof(packetBuffer));
            }
        }
    }
}