/*
 * ota_handler.h
 *
 *  Created on: Apr 20, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_OTA_HANDLER_H_
#define INC_OTA_HANDLER_H_

#include <stdint.h>

/* ================= PUBLIC FUNCTIONS ================= */

// Initialize OTA handler
void OTA_Handler_Init(void);

// Check if OTA button was pressed (call in main loop)
void OTA_Check_Trigger(void);

// Check if OTA is currently in progress
uint8_t OTA_Is_In_Progress(void);

// Verify W25Q64 content (for debugging)
void Verify_W25Q64_Content(void);

// External flag for button interrupt (set from button.c)
extern volatile uint8_t ota_button_trigger;

#endif /* INC_OTA_HANDLER_H_ */
