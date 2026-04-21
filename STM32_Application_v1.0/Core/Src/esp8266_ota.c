/*
 * esp8266_ota.c
 *
 *  Created on: Apr 19, 2026
 *      Author: Rubin Khadka
 */

#include "esp8266_ota.h"
#include "flash_layout.h"
#include "app_ota.h"

#include "w25q64.h"
#include "usart1.h"
#include "usart2.h"
#include "dwt.h"

#include "string.h"
#include "stdio.h"
#include "stdlib.h"

/* ================= CONFIG ================= */
#define ESP_DMA_RX_BUF_SIZE     1024
#define OTA_RX_BUF_SIZE         512
#define OTA_ACK_CHUNK           512
#define OTA_FLASH_OFFSET        0

#define WiFi_ssid               "mynoobu"
#define WiFi_pssd               "Sarah159!"
#define SERVER_IP               "10.201.248.65"
#define SERVER_PORT             5678

/* ================= OTA HEADER ================= */
#define OTA_HEADER_SIZE         sizeof(ota_image_hdr_t)
#define APP_MAGIC               0xDEADBEEF

/* ================= GLOBAL VARIABLES ================= */
ESP8266_ConnectionState ESP_ConnState = ESP8266_DISCONNECTED;
volatile uint8_t ota_active = 0;

char esp_rx_buffer[2048];

/* ================= OTA STATE ================= */
typedef enum
{
  OTA_IDLE = 0,
  OTA_CONNECTING,
  OTA_RECEIVING,
  OTA_COMPLETED,
  OTA_ERROR
} ota_state_t;

static ota_state_t ota_state = OTA_IDLE;

static uint8_t ota_rx_buf[OTA_RX_BUF_SIZE];
static uint32_t flash_offset = OTA_FLASH_OFFSET;
static uint32_t bytes_received = 0;
static uint8_t header_received = 0;
static uint8_t flash_erased = 0;
static uint8_t in_ipd_payload = 0;
static uint32_t ipd_remaining = 0;
static uint32_t last_progress_tick = 0;
static uint32_t last_bytes_received = 0;
static uint32_t bytes_in_chunk = 0;

static ota_image_hdr_t ota_hdr;

/* ================= DMA HELPERS ================= */
static uint16_t ESP_DMA_GetAvailable(void)
{
  return USART1_DMA_GetAvailable();
}

static uint16_t ESP_DMA_Read(uint8_t *buf, uint16_t maxlen)
{
  return USART1_DMA_Read(buf, maxlen);
}

void ESP_DMA_Flush(void)
{
  uint16_t available = ESP_DMA_GetAvailable();
  if(available > 0)
  {
    uint8_t dummy[available];
    USART1_DMA_Read(dummy, available);
  }
  memset(esp_rx_buffer, 0, sizeof(esp_rx_buffer));
}

/* ================= SEND COMMAND ================= */
ESP8266_Status ESP_SendCommand(char *cmd, const char *ack, uint32_t timeout)
{
  uint32_t tickstart = DWT_GetTick();
  ESP_DMA_Flush();

  if(cmd && *cmd)
  {
    USART2_SendString("[CMD] ");
    USART2_SendString(cmd);
    USART1_SendString(cmd);
  }

  while((DWT_GetTick() - tickstart) < timeout)
  {
    uint16_t available = ESP_DMA_GetAvailable();
    if(available > 0)
    {
      uint16_t read = ESP_DMA_Read(
          (uint8_t*) esp_rx_buffer + strlen(esp_rx_buffer),
          (available < 256) ? available : 256);

      size_t len = strlen(esp_rx_buffer);
      if(len < sizeof(esp_rx_buffer) - 1)
      {
        esp_rx_buffer[len] = '\0';
      }

      if(ack && strstr(esp_rx_buffer, ack))
      {
        USART2_SendString("[OK] ");
        USART2_SendString((char*) ack);
        USART2_SendString("\r\n");
        return ESP8266_OK;
      }
    }
    DWT_Delay_ms(10);
  }

  USART2_SendString("[ERR] Timeout: ");
  USART2_SendString((char*) ack);
  USART2_SendString("\r\n");
  return ESP8266_TIMEOUT;
}

/* ================= ESP INIT ================= */
ESP8266_Status ESP_Init(void)
{
  USART2_SendString("ESP Init...\r\n");
  DWT_Delay_ms(1000);

  ESP_DMA_Flush();

  if(ESP_SendCommand("AT\r\n", "OK", 2000) != ESP8266_OK)
    return ESP8266_ERROR;

  if(ESP_SendCommand("ATE0\r\n", "OK", 2000) != ESP8266_OK)
    return ESP8266_ERROR;

  USART2_SendString("ESP OK\r\n");
  return ESP8266_OK;
}

/* ================= WIFI + IP ================= */
static ESP8266_Status ESP_GetIP(char *ip_buffer, uint16_t buffer_len)
{
  for(int attempt = 1; attempt <= 3; attempt++)
  {
    ESP_DMA_Flush();

    ESP8266_Status result = ESP_SendCommand("AT+CIFSR\r\n", "OK", 5000);
    if(result != ESP8266_OK)
      continue;

    char *search = esp_rx_buffer;
    char *last_ip = NULL;

    while((search = strstr(search, "STAIP,\"")) != NULL)
    {
      search += 7;
      char *end = strchr(search, '"');
      if(end && ((end - search) < buffer_len))
      {
        last_ip = search;
        int len = end - search;
        strncpy(ip_buffer, search, len);
        ip_buffer[len] = '\0';
      }
      search++;
    }

    if(last_ip)
    {
      if(strcmp(ip_buffer, "0.0.0.0") == 0)
      {
        ESP_ConnState = ESP8266_CONNECTED_NO_IP;
        DWT_Delay_ms(1000);
        continue;
      }

      ESP_ConnState = ESP8266_CONNECTED_IP;
      return ESP8266_OK;
    }

    DWT_Delay_ms(500);
  }

  ESP_ConnState = ESP8266_CONNECTED_NO_IP;
  return ESP8266_ERROR;
}

ESP8266_Status ESP_ConnectWiFi(const char *ssid, const char *password, char *ip_buffer, uint16_t buffer_len)
{
  char cmd[128];

  ESP_SendCommand("AT+CWMODE=1\r\n", "OK", 2000);

  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);

  if(ESP_SendCommand(cmd, "WIFI CONNECTED", 15000) != ESP8266_OK)
    return ESP8266_ERROR;

  return ESP_GetIP(ip_buffer, buffer_len);
}

ESP8266_ConnectionState ESP_GetConnectionState(void)
{
  return ESP_ConnState;
}

/* ================= OTA FLASH HELPERS ================= */
static void erase_ota_flash(void)
{
  USART2_SendString("Erasing W25Q64...\r\n");
  W25Q64_Erase(OTA_FLASH_OFFSET, 1024 * 1024);
  USART2_SendString("Flash erased\r\n");
}

static uint8_t ota_write(uint32_t addr, uint8_t *buf, size_t len)
{
  uint8_t verify_buf[512];

  if(len == 0)
    return 1;

  // Pre-check: verify area is erased
  W25Q64_FastRead(addr, verify_buf, len);
  for(size_t i = 0; i < len; i++)
  {
    if(verify_buf[i] != 0xFF)
    {
      USART2_SendString("ERROR: Flash not erased!\r\n");
      return 0;
    }
  }

  // Write to flash
  W25Q64_Write(addr, buf, len);
  DWT_Delay_ms(1);

  // Verify write
  W25Q64_FastRead(addr, verify_buf, len);
  if(memcmp(buf, verify_buf, len) != 0)
  {
    USART2_SendString("ERROR: Write verify failed!\r\n");
    return 0;
  }

  return 1;
}

/* ================= OTA PULL DATA ================= */
static int ota_pull_data(uint8_t *buf, uint16_t maxlen)
{
  uint16_t available = USART1_DMA_GetAvailable();

  // Debug: Print available bytes
  if(available > 0)
  {
    USART2_SendString("DMA available: ");
    USART2_SendNumber(available);
    USART2_SendString("\r\n");
  }

  if(available == 0)
    return 0;

  uint16_t to_read = (available < maxlen) ? available : maxlen;
  uint16_t read = USART1_DMA_Read(buf, to_read);

  USART2_SendString("Read ");
  USART2_SendNumber(read);
  USART2_SendString(" bytes from DMA\r\n");

  return read;
}

/* ================= OTA PROCESS RX ================= */
static void ota_process_rx(uint8_t *data, uint16_t len)
{
  if(len == 0)
    return;

  USART2_SendString("RX: ");
  USART2_SendNumber(len);
  USART2_SendString(" bytes\r\n");

  uint16_t i = 0;

  while(i < len)
  {
    // Not in payload → look for +IPD or close messages
    if(!in_ipd_payload)
    {
      // Check for connection closed
      if(len - i >= 6)
      {
        if(memcmp(&data[i], "CLOSED", 6) == 0 || memcmp(&data[i], "0,CLOSED", 8) == 0
            || memcmp(&data[i], "ERROR", 5) == 0)
        {
          USART2_SendString("Connection closed by server\r\n");

          if(bytes_received == 0)
          {
            ota_state = OTA_ERROR;
          }
          if(bytes_received + 512 >= ota_hdr.image_size + OTA_HEADER_SIZE)
          {
            USART2_SendString("Finalizing OTA\r\n");
            ota_state = OTA_COMPLETED;
            ota_active = 0;
          }
          return;
        }
      }

      // Look for +IPD
      if(len - i >= 5 && memcmp(&data[i], "+IPD,", 5) == 0)
      {
        i += 5;

        // Skip optional link ID
        if(i < len && data[i] >= '0' && data[i] <= '4' && i + 1 < len && data[i + 1] == ',')
        {
          i += 2;
        }

        // Parse length
        ipd_remaining = 0;
        while(i < len && data[i] >= '0' && data[i] <= '9')
        {
          ipd_remaining = ipd_remaining * 10 + (data[i] - '0');
          i++;
        }

        if(i >= len || data[i] != ':')
        {
          USART2_SendString("Malformed +IPD\r\n");
          i++;
          continue;
        }

        i++;  // skip ':'
        in_ipd_payload = 1;
        USART2_SendString("+IPD len=");
        USART2_SendNumber(ipd_remaining);
        USART2_SendString("\r\n");
      }
      else
      {
        i++;
      }
    }
    else  // in payload
    {
      uint16_t can_take = len - i;
      if(can_take > ipd_remaining)
        can_take = ipd_remaining;

      if(can_take == 0)
      {
        in_ipd_payload = 0;
        continue;
      }

      if(!flash_erased)
      {
        erase_ota_flash();
        flash_erased = 1;
      }

      uint32_t prev_received = bytes_received;
      uint32_t offset = 0;

      if(!header_received)
      {
        uint32_t need = OTA_HEADER_SIZE - bytes_received;

        if(can_take >= need)
        {
          memcpy(((uint8_t*) &ota_hdr) + bytes_received, &data[i], need);
          ota_write(flash_offset, &data[i], need);

          offset += need;
          bytes_received += need;
          flash_offset += need;

          if(bytes_received >= OTA_HEADER_SIZE)
          {
            header_received = 1;
            if(ota_hdr.magic != APP_MAGIC)
            {
              USART2_SendString("ERROR: Invalid magic!\r\n");
              ota_state = OTA_ERROR;
              return;
            }
            USART2_SendString("Header OK - Size: ");
            USART2_SendNumber(ota_hdr.image_size);
            USART2_SendString(" bytes, Version: ");
            USART2_SendNumber(ota_hdr.version);
            USART2_SendString("\r\n");
          }
        }
        else
        {
          memcpy(((uint8_t*) &ota_hdr) + bytes_received, &data[i], can_take);
          ota_write(flash_offset, &data[i], can_take);
          flash_offset += can_take;
          bytes_received += can_take;
          bytes_in_chunk += can_take;
          USART2_SendString("Partial header, bytes_received: ");
          USART2_SendNumber(bytes_received);
          USART2_SendString("\r\n");
          ipd_remaining -= can_take;
          return;
        }
      }

      uint32_t fw_bytes_received = bytes_received - OTA_HEADER_SIZE;
      uint32_t fw_bytes_to_write = can_take - offset;

      if(fw_bytes_received + fw_bytes_to_write > ota_hdr.image_size)
        fw_bytes_to_write = ota_hdr.image_size - fw_bytes_received;

      if(fw_bytes_to_write > 0)
      {
        ota_write(flash_offset, &data[i + offset], fw_bytes_to_write);
        flash_offset += fw_bytes_to_write;
        bytes_received += fw_bytes_to_write;
      }

      bytes_in_chunk += (bytes_received - prev_received);

      USART2_SendString("bytes_received: ");
      USART2_SendNumber(bytes_received);
      USART2_SendString(", bytes_in_chunk: ");
      USART2_SendNumber(bytes_in_chunk);
      USART2_SendString("\r\n");

      // Send ACK when threshold reached
      if(bytes_in_chunk >= OTA_ACK_CHUNK)
      {
        USART2_SendString("[ACK] Sending ACK...\r\n");

        if(ESP_SendCommand("AT+CIPSEND=1\r\n", ">", 1500) == ESP8266_OK)
        {
          USART2_SendString("[ACK] Got '>', sending 'A'\r\n");
          USART1_SendString("A");

          if(ESP_SendCommand("", "SEND OK", 2500) == ESP8266_OK)
          {
            USART2_SendString("[ACK] ACK sent successfully!\r\n");
          }
          else
          {
            USART2_SendString("[ACK] SEND OK timeout\r\n");
          }
        }
        else
        {
          USART2_SendString("[ACK] CIPSEND timeout\r\n");
        }

        bytes_in_chunk = 0;
      }

      // Final check
      if(bytes_received >= ota_hdr.image_size + OTA_HEADER_SIZE)
      {
        USART2_SendString("OTA Complete! Received: ");
        USART2_SendNumber(bytes_received);
        USART2_SendString(" bytes\r\n");
        ota_state = OTA_COMPLETED;
        ota_active = 0;
        return;
      }

      i += can_take;
      ipd_remaining -= can_take;

      if(ipd_remaining == 0)
      {
        in_ipd_payload = 0;
      }
    }
  }

  if(bytes_received > last_bytes_received)
  {
    last_progress_tick = DWT_GetTick();
    last_bytes_received = bytes_received;
  }
}

/* ================= OTA TASK ================= */
void ESP8266_OTA_Task(void)
{
  static uint32_t last_debug = 0;

  if(!ota_active)
    return;

  // Periodic debug every 2 seconds
  if((DWT_GetTick() - last_debug) > 2000)
  {
    last_debug = DWT_GetTick();
    USART2_SendString("OTA Task - Checking for data...\r\n");
    ESP_SendCommand("AT+CIPRECVDATA?\r\n", "OK", 1000);
  }

  int len = ota_pull_data(ota_rx_buf, OTA_RX_BUF_SIZE);
  if(len > 0)
    ota_process_rx(ota_rx_buf, len);

  uint32_t now = DWT_GetTick();
  if(ota_active && (now - last_progress_tick > 8000))
  {
    if(bytes_received == last_bytes_received && bytes_received > 0)
    {
      USART2_SendString("ERROR: OTA stalled!\r\n");
      ota_state = OTA_ERROR;
      ota_active = 0;
      return;
    }
    last_bytes_received = bytes_received;
    last_progress_tick = now;
  }
}

/* ================= OTA START ================= */
void ota_start(void)
{
  char ip[16];
  char cmd[128];

  USART2_SendString("\r\n========== OTA START ==========\r\n");

  // Initialize W25Q64
  W25Q64_Reset();
  uint32_t id = W25Q64_ReadID();
  USART2_SendString("W25Q64 ID: 0x");
  USART2_SendHex((id >> 16) & 0xFF);
  USART2_SendHex((id >> 8) & 0xFF);
  USART2_SendHex(id & 0xFF);
  USART2_SendString("\r\n");

  if(id != 0xEF4017)
  {
    USART2_SendString("ERROR: W25Q64 not detected!\r\n");
    return;
  }

  // Initialize ESP8266
  if(ESP_Init() != ESP8266_OK)
    return;

  // Connect to WiFi
  USART2_SendString("Connecting to WiFi...\r\n");
  if(ESP_ConnectWiFi(WiFi_ssid, WiFi_pssd, ip, sizeof(ip)) != ESP8266_OK)
    return;

  USART2_SendString("WiFi Connected! IP: ");
  USART2_SendString(ip);
  USART2_SendString("\r\n");

  // Wait for IP to be ready
  uint32_t tickStart = DWT_GetTick();
  while((ESP_ConnState != ESP8266_CONNECTED_IP) && ((DWT_GetTick() - tickStart) < 5000));
  DWT_Delay_ms(1000);

  // Disable multiple connections
  if(ESP_SendCommand("AT+CIPMUX=0\r\n", "OK", 2000) != ESP8266_OK)
    return;

  // Connect to TCP server
  USART2_SendString("Connecting to server...\r\n");
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", SERVER_IP, SERVER_PORT);
  if(ESP_SendCommand(cmd, "CONNECT", 10000) != ESP8266_OK)
    return;

  USART2_SendString("Connected to server!\r\n");

  // Reset OTA state
  flash_offset = OTA_FLASH_OFFSET;
  bytes_received = 0;
  header_received = 0;
  flash_erased = 0;
  in_ipd_payload = 0;
  ipd_remaining = 0;
  bytes_in_chunk = 0;
  last_progress_tick = DWT_GetTick();
  last_bytes_received = 0;
  ota_state = OTA_RECEIVING;
  ota_active = 1;

  // Request firmware from server
  USART2_SendString("Requesting firmware...\r\n");
  const char req[] = "START\n";
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", strlen(req));

  if(ESP_SendCommand(cmd, ">", 2000) != ESP8266_OK)
  {
    USART2_SendString("ERROR: CIPSEND failed!\r\n");
    ota_state = OTA_ERROR;
    ota_active = 0;
    return;
  }

  if(ESP_SendCommand((char*) req, "SEND OK", 5000) != ESP8266_OK)
  {
    USART2_SendString("ERROR: Send failed!\r\n");
    ota_state = OTA_ERROR;
    ota_active = 0;
    return;
  }

  USART2_SendString("Receiving firmware...\r\n");

  // Debug: Check connection status
  USART2_SendString("Checking connection status...\r\n");
  ESP_SendCommand("AT+CIPSTATUS\r\n", "OK", 2000);

  last_progress_tick = DWT_GetTick();
}

/* ================= OTA STATUS ================= */
uint8_t ESP8266_OTA_IsComplete(void)
{
  return (ota_state == OTA_COMPLETED);
}

uint8_t ESP8266_OTA_HasError(void)
{
  return (ota_state == OTA_ERROR);
}

/* ================= TEST FUNCTION ================= */
void ESP8266_Test_Receive(void)
{
  char ip[16];
  char cmd[128];

  USART2_SendString("\r\n========== TEST RECEIVE MODE ==========\r\n");

  // Initialize ESP8266
  if(ESP_Init() != ESP8266_OK)
  {
    USART2_SendString("ESP8266 init failed!\r\n");
    return;
  }

  // Connect to WiFi
  USART2_SendString("Connecting to WiFi...\r\n");
  if(ESP_ConnectWiFi(WiFi_ssid, WiFi_pssd, ip, sizeof(ip)) != ESP8266_OK)
  {
    USART2_SendString("WiFi connection failed!\r\n");
    return;
  }

  USART2_SendString("WiFi Connected! IP: ");
  USART2_SendString(ip);
  USART2_SendString("\r\n");

  // Connect to server
  USART2_SendString("Connecting to server...\r\n");
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", SERVER_IP, SERVER_PORT);
  if(ESP_SendCommand(cmd, "CONNECT", 10000) != ESP8266_OK)
  {
    USART2_SendString("Server connection failed!\r\n");
    return;
  }

  USART2_SendString("Connected to server!\r\n");

  // Send START command
  USART2_SendString("Sending START...\r\n");
  const char req[] = "START\n";
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", strlen(req));

  if(ESP_SendCommand(cmd, ">", 2000) != ESP8266_OK)
  {
    USART2_SendString("CIPSEND failed!\r\n");
    return;
  }

  if(ESP_SendCommand((char*) req, "SEND OK", 5000) != ESP8266_OK)
  {
    USART2_SendString("Send failed!\r\n");
    return;
  }

  USART2_SendString("START sent. Waiting for test messages...\r\n");
  USART2_SendString("========================================\r\n");

  // Read and display all incoming data for 15 seconds
  uint32_t start = DWT_GetTick();
  uint32_t timeout = 15000; // 15 seconds
  uint8_t buffer[512];

  while((DWT_GetTick() - start) < timeout)
  {
    uint16_t available = USART1_DMA_GetAvailable();
    if(available > 0)
    {
      uint16_t read = USART1_DMA_Read(buffer, (available < 512) ? available : 512);

      USART2_SendString(">>> ");
      for(uint16_t i = 0; i < read; i++)
      {
        // Print printable characters
        if(buffer[i] >= 32 && buffer[i] <= 126)
        {
          USART2_SendChar(buffer[i]);
        }
        // Print newline as visible
        else if(buffer[i] == '\n')
        {
          USART2_SendString("[LF]");
        }
        else if(buffer[i] == '\r')
        {
          USART2_SendString("[CR]");
        }
        else
        {
          // Print hex for non-printable
          USART2_SendString("[");
          USART2_SendHex(buffer[i]);
          USART2_SendString("]");
        }
      }
      USART2_SendString("\r\n");
    }
    DWT_Delay_ms(100);

    // Show heartbeat every 2 seconds
    if(((DWT_GetTick() - start) % 2000) < 100)
    {
      USART2_SendString(".");
    }
  }

  USART2_SendString("\r\n========================================\r\n");
  USART2_SendString("Test complete!\r\n");

  // Close connection
  ESP_SendCommand("AT+CIPCLOSE\r\n", "OK", 2000);
}
