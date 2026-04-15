#include <Arduino.h>
#include <SPI.h>
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

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- CAN FD Debug Tool Indulása ---");

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
    
    ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_40MHz, 500UL * 1000UL, DataBitRateFactor::x4);
    const uint32_t errorCode = can.begin(settings, [] { can.isr(); });

    if (errorCode == 0) {
        Serial.println("CAN FD sikeresen inicializálva!");
    } else {
        Serial.printf("Hiba a CAN induláskor: 0x%X\n", errorCode);
        while (1) yield();
    }
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

    // CAN read
    if (can.receive(frame)) {
        outputMsg = formatCanMessage(frame);
        
        // USB-n (Virtuális soros port)
        Serial.println(outputMsg);
        
        // Wi-Fi-n (UDP)
        udp.beginPacket(pcIP, udpPort);
        udp.print(outputMsg);
        udp.endPacket();
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
            
            if (can.tryToSend(txFrame)) {
                Serial.println("Teszt üzenet injektálva USB parancsra!");
            }
        }
    }

    // CAN send Wi-Fi-n
    int packetSize = udp.parsePacket();
    if (packetSize) {
        int len = udp.read(packetBuffer, 255);
        if (len > 0) packetBuffer[len] = '\0'; 
        
        String command = String(packetBuffer);
        command.trim();

        if (command == "SEND_TEST") {
            CANFDMessage txFrame;
            txFrame.id = 0x200; 
            txFrame.len = 8;
            for(int i=0; i<8; i++) txFrame.data[i] = 0xFF;
            
            if (can.tryToSend(txFrame)) {
                Serial.println("Teszt üzenet injektálva Wi-Fi parancsra!");
            }
        }
    }
}