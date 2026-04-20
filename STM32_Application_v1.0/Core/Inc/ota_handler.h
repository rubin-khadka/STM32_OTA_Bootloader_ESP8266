/*
 * ota_handler.h
 *
 *  Created on: Apr 20, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_OTA_HANDLER_H_
#define INC_OTA_HANDLER_H_

#include <stdint.h>

// Initialize OTA handler
void OTA_Handler_Init(void);

// Check if OTA button was pressed
void OTA_Check_Trigger(void);

// Process OTA download
void OTA_Process_Task(void);

// Update LCD display with OTA progress
void OTA_Update_Display(void);

// Get current OTA status
uint8_t OTA_Is_In_Progress(void);
uint8_t OTA_Is_Complete(void);
uint8_t OTA_Has_Error(void);

// External flag for button interrupt
extern volatile uint8_t ota_button_trigger;

#endif /* INC_OTA_HANDLER_H_ */
