#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>


#define CAN_SCK_PIN   14  // D5
#define CAN_MISO_PIN  12  // D6
#define CAN_MOSI_PIN  13  // D7
#define CAN_CS_PIN    5   // D1 Chip Select az MCP2518FD-hez
#define CAN_INT_PIN   4   // D2 MCP2518FD megszakitas

#define SD_CS_PIN     16  // D0 Chip Select az SD kartyahoz

#define LED_A         2   // IO2 (Amber LED)
#define LED_B         15  // IO15 (Blue LED)

#define ADC_BUTTON_PIN A0 // ADC bemenet (0-1V)

#endif