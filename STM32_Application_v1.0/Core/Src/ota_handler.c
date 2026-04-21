/*
 * ota_handler.c
 *
 *  Created on: Apr 20, 2026
 *      Author: Rubin Khadka
 */

#include "ota_handler.h"
#include "esp8266_ota.h"
#include "app_ota.h"
#include "lcd.h"
#include "usart2.h"
#include "dwt.h"
#include "w25q64.h"
#include <string.h>
#include <stdio.h>

/* ================= GLOBAL VARIABLES ================= */
volatile uint8_t ota_button_trigger = 0;

/* ================= PRIVATE FUNCTIONS ================= */
static void show_ota_start(void)
{
  LCD_Clear();
  LCD_SetCursor(0, 0);
  LCD_SendString("OTA Starting...");
  USART2_SendString("\r\n*** OTA Triggered! ***\r\n");
}

static void show_ota_complete(void)
{
  LCD_Clear();
  LCD_SetCursor(0, 0);
  LCD_SendString("OTA Complete!");
  LCD_SetCursor(1, 0);
  LCD_SendString("Rebooting...");
  USART2_SendString("\r\n*** OTA Complete! ***\r\n");
}

static void show_ota_failed(void)
{
  LCD_Clear();
  LCD_SetCursor(0, 0);
  LCD_SendString("OTA Failed!");
  USART2_SendString("\r\n*** OTA Failed! ***\r\n");
}

static void show_ota_progress(uint8_t percent)
{
  char buffer[17];

  sprintf(buffer, "Download %3d%%", percent);
  LCD_SetCursor(0, 0);
  LCD_SendString(buffer);

  sprintf(buffer, "[");
  uint8_t blocks = percent / 10;
  for(uint8_t i = 0; i < blocks; i++)
  {
    strcat(buffer, "=");
  }
  for(uint8_t i = blocks; i < 10; i++)
  {
    strcat(buffer, " ");
  }
  strcat(buffer, "]");
  LCD_SetCursor(1, 0);
  LCD_SendString(buffer);
}

/* ================= PUBLIC FUNCTIONS ================= */

void OTA_Handler_Init(void)
{
  ota_button_trigger = 0;
  USART2_SendString("OTA Handler Initialized\r\n");
}

void Verify_W25Q64_Content(void)
{
  uint8_t buffer[256];
  uint32_t addr;

  USART2_SendString("\r\n=== Verifying W25Q64 Content ===\r\n");

  // Read and display first 64 bytes (where header should be)
  USART2_SendString("First 64 bytes at address 0:\r\n");
  W25Q64_FastRead(0, buffer, 64);

  for(int i = 0; i < 64; i++)
  {
    if(i % 16 == 0)
    {
      USART2_SendString("\r\n");
      USART2_SendHex((i >> 8) & 0xFF);
      USART2_SendHex(i & 0xFF);
      USART2_SendString(": ");
    }
    USART2_SendHex(buffer[i]);
    USART2_SendChar(' ');
  }
  USART2_SendString("\r\n\r\n");

  // Check magic number (first 4 bytes should be 0xDEADBEEF)
  uint32_t magic = *(uint32_t*) buffer;
  USART2_SendString("Magic number: 0x");
  USART2_SendHex((magic >> 24) & 0xFF);
  USART2_SendHex((magic >> 16) & 0xFF);
  USART2_SendHex((magic >> 8) & 0xFF);
  USART2_SendHex(magic & 0xFF);
  USART2_SendString("\r\n");

  if(magic == 0xDEADBEEF)
  {
    USART2_SendString("✓ Magic number CORRECT! Firmware header found.\r\n");

    // Read the header structure
    uint32_t image_size = *(uint32_t*) (buffer + 4);
    uint32_t crc = *(uint32_t*) (buffer + 8);
    uint32_t version = *(uint32_t*) (buffer + 12);

    USART2_SendString("Image size: ");
    USART2_SendNumber(image_size);
    USART2_SendString(" bytes\r\n");
    USART2_SendString("CRC: 0x");
    USART2_SendHex((crc >> 24) & 0xFF);
    USART2_SendHex((crc >> 16) & 0xFF);
    USART2_SendHex((crc >> 8) & 0xFF);
    USART2_SendHex(crc & 0xFF);
    USART2_SendString("\r\n");
    USART2_SendString("Version: ");
    USART2_SendNumber(version);
    USART2_SendString("\r\n");
  }
  else
  {
    USART2_SendString("✗ Magic number WRONG! No valid firmware header.\r\n");
    USART2_SendString("Expected: 0xDEADBEEF\r\n");
  }

  // Also check a few more locations
  USART2_SendString("\r\nChecking other locations:\r\n");

  // Read at offset 512
  W25Q64_FastRead(512, buffer, 16);
  USART2_SendString("Offset 512: ");
  for(int i = 0; i < 16; i++)
  {
    USART2_SendHex(buffer[i]);
    USART2_SendChar(' ');
  }
  USART2_SendString("\r\n");

  // Read at offset 1024
  W25Q64_FastRead(1024, buffer, 16);
  USART2_SendString("Offset 1024: ");
  for(int i = 0; i < 16; i++)
  {
    USART2_SendHex(buffer[i]);
    USART2_SendChar(' ');
  }
  USART2_SendString("\r\n");

  USART2_SendString("=== Verification Complete ===\r\n");
}

void OTA_Check_Trigger(void)
{
  if(ota_button_trigger == 1)
  {
    ota_button_trigger = 0;

    show_ota_start();

    // Start OTA - this will block until complete or error
    ota_start();

    // After ota_start() returns, check result
    if(ESP8266_OTA_IsComplete())
    {
      Verify_W25Q64_Content();
      show_ota_complete();
      DWT_Delay_ms(1000);
      enable_ota_request();  // This will reset the system
    }
    else if(ESP8266_OTA_HasError())
    {
      show_ota_failed();
      DWT_Delay_ms(2000);
    }
  }
}

uint8_t OTA_Is_In_Progress(void)
{
  // Use ota_active from esp8266_ota.c
  extern volatile uint8_t ota_active;
  return ota_active;
}
