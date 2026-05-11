/*
    Header file describing the hardware configurations
    Made by: Neo Vorsatz

    ESP32-S3-WROOM-1U-N8
    Datasheet: https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.html
    Technical Reference Manual v1.8: https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf

    Features
    ROM: 384 KB
    SRAM: 512 KB
    SRAM in RTC: 16 KB
    Clock: 40 MHz crystal oscillator
    Flash: 8 MB (Quad SPI) 
*/

// Header guard
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// ESP32 PIN CONNECTIONS ================================

// Strapping pins
#define IO0 0 // [GPIO 0], RTC GPIO 0, firmware download
#define IO3 3 // [GPIO 3], RTC GPIO 3, touch 3, ADC 1 channel 2, JTAG behaviour

// Simple I/O
#define IO1 1 // [GPIO 1], RTC GPIO 1, touch 1, ADC 1 channel 0
#define IO2 2 // [GPIO 2], RTC GPIO 2, touch 2, ADC 1 channel 1
#define IO6 6 // [GPIO 6], RTC GPIO 6, touch 6, ADC 1 channel 5
#define IO7 7 // [GPIO 7], RTC GPIO 7, touch 7, ADC 1 channel 6
#define IO45 45 // [GPIO 45]
#define IO46 46 // [GPIO 46]

// I2C (chosen because they are the Arduino defaults)
#define I2C_SDA 8 // [GPIO 8], RTC GPIO 8, touch 8, ADC 1 channel 7, SUBSPICS1
#define I2C_SCL 9 // [GPIO 9], RTC GPIO 9, touch 9, ADC 1 channel 8, FSPIHD, SUBSPIHD

// SPI (chosen because they are the Arduino defaults)
#define CS_SD_CARD 10 // [GPIO 10], RTC GPIO, touch 10, ADC 1 channel 9, FSPICS0, FSPIIO4, SUBSPICS0
#define SPI_MOSI 11 // [GPIO 11], RTC GPIO 11, touch 11, ADC 2 channel 0, FSPID, FSPIIO5, SUBSPID
#define SPI_SCK 12 // [GPIO 12], RTC GPIO 12, touch 12, ADC 2 channel 1, FSPICLK, FSPIIO6, SUBSPICLK
#define SPI_MISO 13 // [GPIO 13], RTC GPIO 13, touch 13, ADC 2 channel 2, FSPIQ, FSPIIO7, SUBSPIQ

// USB
#define USB_DP 19 // GPIO 19, RTC GPIO 19, ADC 2 channel 8, [USB D-], U1RTS, CLK out 2
#define USB_DM 20 // GPIO 20, RTC GPIO 20, ADC 2 channel 9, [USB D+], U1CTS, CLK out 1

// OCTAL flash/PSRAM
#define OCTAL_SPIIO6 35 // [GPIO 35], SPIIO6, FSPID, SUBSPID
#define OCTAL_SPIIO7 36 // [GPIO 36], SPIIO7, FSPICLK, SUBSPICLK
#define OCTAL_SPIDQS 37 // [GPIO 37], SPIDQS, FSPIQ, SUBSPIQ

// JTAG interface
#define IO39 39 // GPIO 39, [MTCK], SUBSPICS1, CLK out 3
#define IO40 40 // GPIO 40, [MTDO], CLK out 2
#define IO41 41 // GPIO 41, [MTDI], CLK out 1
#define IO42 42 // GPIO 42, [MTMS]

// UART
#define TXD0 43 // GPIO 43, UART TX
#define RXD0 44 // GPIO 44, UART RX

// LoRa
#define LORA_NSS 5 // [GPIO 5], RTC GPIO 5, touch 5, ADC 1 channel 4
#define LORA_SCK 47 // [GPIO 47], SPICLK P Diff, SUBSPICLK P Diff
#define LORA_MOSI 48 // [GPIO 48], SPICLK N Diff, SUBSPICLK N Diff
#define LORA_MISO 38 // [GPIO 38], FSPIWP, SUBSPIWP
#define LORA_NRESET 14 // [GPIO 14], RTC GPIO 14, touch 14, DC 2 channel 3, FSPIWP, FSPIDQS, SUBSPIWP
#define LORA_BUSY 16 // [GPIO 16], RTC GPIO 16, ADC 2 channel 5, U0RTS, XTAL 32K N
#define LORA_DIO1 15 // [GPIO 15], RTC GPIO 15, ADC 2 channel 4, U0RTS, XTAL 32K P

// GPS
#define GPS_TXD 17 // [GPIO 17], RTC GPIO 17, ADC 2 channel 6, U1TXD
#define GPS_RXD 18 // [GPIO 18], RTC GPIO 18, ADC 2 channel 7, U1RXD, CLK out 3

// Satellite
#define SAT_TXD 4 // [GPIO 4], RTC GPIO 4, touch 4, ADC 1 channel 3
#define SAT_RXD 21 // [GPIO 21], RTC GPIO 21

// ================================

#endif