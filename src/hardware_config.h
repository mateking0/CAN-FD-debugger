#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>


#define CAN_SCK_PIN   14  // D5
#define CAN_MISO_PIN  12  // D6
#define CAN_MOSI_PIN  13  // D7
#define CAN_CS_PIN    15  // D8 Chip Select az MCP2518FD-hez
#define CAN_INT_PIN   4   // D2 MCP2518FD megszakitas


#define LED_POWER     16  // D0 (Tápfeszültség indikátor)
#define LED_CAN_RX    5   // D1 (Fogadás indikátor)
#define LED_CAN_TX    0   // D3 (Küldés indikátor)
#define LED_ERROR     2   // D4 (Hiba indikátor)

#define BUTTON_USER   10  // SD3 (Példa user gomb)

#endif