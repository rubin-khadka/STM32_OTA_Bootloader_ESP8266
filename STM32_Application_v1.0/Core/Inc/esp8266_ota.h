/*
 * esp8266_ota.h
 *
 *  Created on: Apr 19, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_ESP8266_OTA_H_
#define INC_ESP8266_OTA_H_

#include <stdint.h>

typedef enum {
    ESP8266_OK = 0,
    ESP8266_ERROR,
    ESP8266_TIMEOUT,
    ESP8266_NO_RESPONSE,
} ESP8266_Status;

typedef enum {
    ESP8266_DISCONNECTED = 0,
    ESP8266_CONNECTED_NO_IP,
    ESP8266_CONNECTED_IP
} ESP8266_ConnectionState;

extern ESP8266_ConnectionState ESP_ConnState;

ESP8266_Status ESP_Init(void);
ESP8266_Status ESP_ConnectWiFi(const char *ssid, const char *password, char *ip_buffer, uint16_t buffer_len);
ESP8266_ConnectionState ESP_GetConnectionState(void);
ESP8266_Status ESP_SendCommand(char *cmd, const char *ack, uint32_t timeout);
void ESP_DMA_Flush(void);

void ESP8266_OTA_Task(void);
void ota_start(void);

uint8_t ESP8266_OTA_IsComplete(void);
uint8_t ESP8266_OTA_HasError(void);

void ESP8266_Test_Receive(void);

#endif /* INC_ESP8266_OTA_H_ */
