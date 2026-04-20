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
#include <string.h>
#include <stdio.h>

/* ================= GLOBAL VARIABLES ================= */
volatile uint8_t ota_button_trigger = 0;
static uint8_t ota_in_progress = 0;
static uint8_t ota_progress_percent = 0;
static uint8_t ota_complete = 0;
static uint8_t ota_error = 0;

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

static void reset_ota_state(void)
{
  ota_in_progress = 0;
  ota_progress_percent = 0;
  ota_complete = 0;
  ota_error = 0;
}

/* ================= PUBLIC FUNCTIONS ================= */

void OTA_Handler_Init(void)
{
  reset_ota_state();
  ota_button_trigger = 0;
  USART2_SendString("OTA Handler Initialized\r\n");
}

void OTA_Check_Trigger(void)
{
  if(ota_button_trigger == 1)
  {
    ota_button_trigger = 0;

    if(!ota_in_progress)
    {
      show_ota_start();
      ota_start();
      ota_in_progress = 1;
    }
  }
}

void OTA_Process_Task(void)
{
  if(!ota_in_progress)
    return;

  // Process OTA data
  ESP8266_OTA_Task();

  // Update progress (you can add actual progress tracking)
  // ota_progress_percent = ESP8266_OTA_GetProgress();

  // Check completion
  if(ESP8266_OTA_IsComplete())
  {
    show_ota_complete();
    ota_complete = 1;
    DWT_Delay_ms(1000);
    enable_ota_request();  // This will reset the system
  }
  else if(ESP8266_OTA_HasError())
  {
    show_ota_failed();
    ota_error = 1;
    ota_in_progress = 0;
    DWT_Delay_ms(2000);
    reset_ota_state();
  }
}

void OTA_Update_Display(void)
{
  char buffer[17];

  if(!ota_in_progress)
    return;

  sprintf(buffer, "Download %3d%%", ota_progress_percent);
  LCD_SetCursor(0, 0);
  LCD_SendString(buffer);

  sprintf(buffer, "[");
  uint8_t blocks = ota_progress_percent / 10;
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

uint8_t OTA_Is_In_Progress(void)
{
  return ota_in_progress;
}

uint8_t OTA_Is_Complete(void)
{
  return ota_complete;
}

uint8_t OTA_Has_Error(void)
{
  return ota_error;
}
