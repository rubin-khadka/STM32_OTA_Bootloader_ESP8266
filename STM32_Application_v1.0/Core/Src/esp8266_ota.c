/*
 * esp8266_ota.c
 *
 *  Created on: Apr 19, 2026
 *      Author: Rubin Khadka
 */

#include "esp8266_ota.h"
#include "flash_layout.h"
#include "flash_operations.h"
#include "app_ota.h"
#include "W25Q64.h"

#include "string.h"
#include "stdio.h"
#include "stdlib.h"

/* ================= CONFIG ================= */
#define ESP_DMA_RX_BUF_SIZE     1024
#define OTA_RX_BUF_SIZE         512
#define OTA_ACK_CHUNK     512
#define OTA_FLASH_OFFSET    0

#define WiFi_ssid               "mynoobu"
#define WiFi_pssd               "Sarah159!"
#define SERVER_IP               "10.100.193.65"
#define SERVER_PORT             5678

/* ================================== */
ESP8266_ConnectionState ESP_ConnState = ESP8266_DISCONNECTED;
volatile uint8_t ota_active = 0;   // OTA running flag

uint8_t esp_dma_rx_buf[ESP_DMA_RX_BUF_SIZE];
volatile uint16_t esp_dma_rx_head = 0;

char esp_rx_buffer[2048];          // AT command buffer

#define OTA_HEADER_SIZE         sizeof(ota_image_hdr_t)

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
static uint32_t actually_written = 0;
static uint8_t in_ipd_payload = 0;
static uint32_t ipd_remaining = 0;
static uint32_t last_progress_tick = 0;
static uint32_t last_bytes_received = 0;
static uint32_t bytes_in_chunk = 0;

static void ota_finalize(void);
static void ota_error(void);

static ota_image_hdr_t ota_hdr;

/* ================= DMA HELPERS ================= */
uint16_t ESP_DMA_GetWritePos(void)
{
  return ESP_DMA_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(ESP_UART.hdmarx);
}

void ESP_DMA_Flush(void)
{
  esp_dma_rx_head = ESP_DMA_GetWritePos();
  memset(esp_rx_buffer, 0, sizeof(esp_rx_buffer));
}

/* ================= ESP INIT ================= */
ESP8266_Status ESP_Init(void)
{
  USER_LOG("Initializing ESP8266...");
  HAL_Delay(1000);

  HAL_UART_Receive_DMA(&ESP_UART, esp_dma_rx_buf, ESP_DMA_RX_BUF_SIZE);
  ESP_DMA_Flush();

  if(ESP_SendCommand("AT\r\n", "OK", 2000) != ESP8266_OK)
    return ESP8266_ERROR;

  if(ESP_SendCommand("ATE0\r\n", "OK", 2000) != ESP8266_OK)
    return ESP8266_ERROR;

  USER_LOG("ESP8266 Initialized Successfully...");
  return ESP8266_OK;
}

/* ================= SEND COMMAND ================= */
ESP8266_Status ESP_SendCommand(char *cmd, const char *ack, uint32_t timeout)
{
  uint32_t tickstart = HAL_GetTick();
  ESP_DMA_Flush();

  if(cmd && *cmd)
  {
    DEBUG_LOG("Sending: %s", cmd);
    HAL_UART_Transmit(&ESP_UART, (uint8_t*) cmd, strlen(cmd), HAL_MAX_DELAY);
  }

  while((HAL_GetTick() - tickstart) < timeout)
  {
    uint16_t head = ESP_DMA_GetWritePos();
    while(esp_dma_rx_head != head)
    {
      char c = esp_dma_rx_buf[esp_dma_rx_head++];
      if(esp_dma_rx_head >= ESP_DMA_RX_BUF_SIZE)
        esp_dma_rx_head = 0;

      size_t len = strlen(esp_rx_buffer);
      if(len < sizeof(esp_rx_buffer) - 1)
      {
        esp_rx_buffer[len] = c;
        esp_rx_buffer[len + 1] = '\0';
      }

      if(ack && strstr(esp_rx_buffer, ack))
      {
        DEBUG_LOG("Matched ACK: %s", ack);
        return ESP8266_OK;
      }
    }
  }

  DEBUG_LOG("Timeout. Buffer: %s", esp_rx_buffer);
  return ESP8266_TIMEOUT;
}

/* ================= WIFI + IP ================= */
static ESP8266_Status ESP_GetIP(char *ip_buffer, uint16_t buffer_len)
{
  USER_LOG("Fetching IP Address...");

  for(int attempt = 1; attempt <= 3; attempt++)
  {
    ESP_DMA_Flush();  // clear old data

    // Send CIFSR command
    ESP8266_Status result = ESP_SendCommand("AT+CIFSR\r\n", "OK", 5000);
    if(result != ESP8266_OK)
    {
      DEBUG_LOG("CIFSR failed on attempt %d", attempt);
      continue;
    }

    // Now esp_rx_buffer should contain +CIFSR output
    char *search = esp_rx_buffer;
    char *last_ip = NULL;

    while((search = strstr(search, "STAIP,\"")) != NULL)
    {
      search += 7; // move past STAIP,"
      char *end = strchr(search, '"');
      if(end && ((end - search) < buffer_len))
      {
        last_ip = search;
        int len = end - search;
        strncpy(ip_buffer, search, len);
        ip_buffer[len] = '\0';
      }
      search++; // keep scanning
    }

    if(last_ip)
    {
      if(strcmp(ip_buffer, "0.0.0.0") == 0)
      {
        DEBUG_LOG("Attempt %d: IP not ready yet (0.0.0.0). Retrying...", attempt);
        ESP_ConnState = ESP8266_CONNECTED_NO_IP;
        HAL_Delay(1000);
        continue;
      }

      USER_LOG("Got IP: %s", ip_buffer);
      ESP_ConnState = ESP8266_CONNECTED_IP;
      return ESP8266_OK;
    }

    DEBUG_LOG("Attempt %d: Failed to parse STAIP.", attempt);
    HAL_Delay(500);
  }

  USER_LOG("Failed to fetch IP after retries.");
  ESP_ConnState = ESP8266_CONNECTED_NO_IP;
  return ESP8266_ERROR;
}

ESP8266_Status ESP_ConnectWiFi(const char *ssid, const char *password, char *ip_buffer, uint16_t buffer_len)
{
  char cmd[128];

  ESP_SendCommand("AT+CWMODE=1\r\n", "OK", 2000);

  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);

  if(ESP_SendCommand(cmd, "WIFI CONNECTED", 10000) != ESP8266_OK)
    return ESP8266_ERROR;

  return ESP_GetIP(ip_buffer, buffer_len);
}

/* ================= OTA HELPERS ================= */
static void erase_ota_flash(void)
{
  W25Q_Erase(OTA_FLASH_OFFSET, APP_MAX_SIZE);
}

static uint8_t ota_write(uint32_t addr, uint8_t *buf, size_t len)
{
  uint8_t rBuf[512];

  if(len == 0)
    return 1;  // success (nothing to do)

  // Quick pre-check: make sure area is erased
  W25Q_FastRead(addr, rBuf, len);
  for(size_t i = 0; i < len; i++)
  {
    if(rBuf[i] != 0xFF)
    {
      USER_LOG("ERROR: addr 0x%08lX not erased before write!", addr);
      USER_LOG("Check W25Q_Erase() Function \n\n");
      ota_error();
      return -1;
    }
  }

  W25Q_Write(addr, buf, len);

  // Small delay if needed (some flashes need time after write)
  HAL_Delay(1);

  /****************** Verify the data written ************************/
  W25Q_FastRead(addr, rBuf, len);

  int cmp = memcmp(buf, rBuf, len);
  if(cmp != 0)
  {
    USER_LOG("!!! FLASH WRITE VERIFY FAILED at addr=0x%08lX, len=%u !!!\n", addr, (unsigned) len);

    // Find first mismatch
    for(size_t k = 0; k < len; k++)
    {
      if(buf[k] != rBuf[k])
      {
        USER_LOG("First mismatch at offset %u: wrote 0x%02X, read 0x%02X\n", (unsigned) k, buf[k], rBuf[k]);
        break;
      }
    }

    ota_error();
    return -2;  // failed
  }

  return 0;  // success
}

static void ota_process_rx(uint8_t *data, uint16_t len)
{
  if(len == 0)
    return;

  DEBUG_LOG("RX: %u bytes", len);

  uint16_t i = 0;

  while(i < len)
  {

    // ───────────────────────────────────────────────
    // Not in payload → look for +IPD or close messages
    // ───────────────────────────────────────────────
    if(!in_ipd_payload)
    {
      // Check for connection closed (common ESP8266 unsolicited)
      if(len - i >= 6)
      {
        if(memcmp(&data[i], "CLOSED", 6) == 0 || memcmp(&data[i], "0,CLOSED", 8) == 0
            || memcmp(&data[i], "ERROR", 5) == 0)
        {
          USER_LOG(
              "Connection closed by server. Received %lu / %lu expected",
              bytes_received,
              ota_hdr.image_size + OTA_HEADER_SIZE);

          if(bytes_received == 0)
          {
            ota_error();
          }
          if(bytes_received + 512 >= ota_hdr.image_size + OTA_HEADER_SIZE)
          {
            USER_LOG("Size close enough → finalizing OTA\n");
            ota_finalize();
          }
          else
          {
            USER_LOG("Incomplete transfer - aborting\n");
            // optional: ota_state = OTA_ERROR;
          }
          return;
        }
      }

      // Look for +IPD
      if(len - i >= 5 && memcmp(&data[i], "+IPD,", 5) == 0)
      {
        i += 5;

        // Skip optional link ID (single digit + comma)
        if(i < len && data[i] >= '0' && data[i] <= '4' && i + 1 < len && data[i + 1] == ',')
        {
          i += 2;
        }

        // Parse length
        ipd_remaining = 0;
        uint8_t digits = 0;
        while(i < len && data[i] >= '0' && data[i] <= '9')
        {
          ipd_remaining = ipd_remaining * 10 + (data[i] - '0');
          i++;
          digits++;
        }

        if(digits == 0 || i >= len || data[i] != ':')
        {
          USER_LOG("Malformed +IPD - skipping byte");
          i++;
          continue;
        }

        i++;  // skip ':'
        in_ipd_payload = 1;

        DEBUG_LOG("Got +IPD len=%lu, payload starts at i=%u", ipd_remaining, i);
      }
      else
      {
        // Junk byte - discard
        i++;
      }
    }
    else  // ────────────────────────────────────────── in payload
    {
      uint16_t can_take = len - i;
      if(can_take > ipd_remaining)
        can_take = ipd_remaining;

      if(can_take == 0)
      {
        in_ipd_payload = 0;
        continue;
      }

      // ───────────── OTA WRITE LOGIC ────────────────
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
          actually_written = bytes_received - prev_received;

          if(bytes_received >= OTA_HEADER_SIZE)
          {
            header_received = 1;
            if(ota_hdr.magic != APP_MAGIC)
            {
              USER_LOG("Invalid Header Received: magic=0x%08lX", ota_hdr.magic);
              USER_LOG("Aborting OTA...\n\n");
              ota_error();
            }
            else
            {
              USER_LOG(
                  "Header complete: magic=0x%08lX size=%lu version=%lu",
                  ota_hdr.magic,
                  ota_hdr.image_size,
                  ota_hdr.version);
            }

          }
        }

        else
        {
          memcpy(((uint8_t*) &ota_hdr) + bytes_received, &data[i], can_take);

          /* WRITE PARTIAL HEADER TO FLASH */
          ota_write(flash_offset, &data[i], can_take);
          flash_offset += can_take;
          bytes_received += can_take;
          actually_written = bytes_received - prev_received;
          bytes_in_chunk += actually_written;
          USER_LOG("bytes_received so far = %lu, Actually Written = %lu", bytes_received, actually_written);
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

      actually_written = bytes_received - prev_received;
      bytes_in_chunk += actually_written;
      USER_LOG("bytes_received so far = %lu, Actually Written = %lu", bytes_received, actually_written);
      // Ack when threshold reached
      if(bytes_in_chunk >= OTA_ACK_CHUNK)
      {
        DEBUG_LOG("Sending ack (chunk bytes >= %d, total rx=%lu)", OTA_ACK_CHUNK, bytes_received);

        ESP8266_Status s1 = ESP_SendCommand("AT+CIPSEND=1\r\n", ">", 1500);
        if(s1 == ESP8266_OK)
        {
          ESP8266_Status s2 = ESP_SendCommand("A", "SEND OK", 2500);
          if(s2 == ESP8266_OK)
          {
            USER_LOG("Ack sent successfully\n\n");
          }
          else
          {
            USER_LOG("Ack send failed!");
          }
        }
        else
        {
          DEBUG_LOG("CIPSEND command failed!");
        }

        bytes_in_chunk = 0;
      }

      // Final check
      if(bytes_received >= ota_hdr.image_size + OTA_HEADER_SIZE)
      {
        USER_LOG("Full OTA received (%lu bytes) → finalizing", bytes_received);
        ota_finalize();
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

  // Update stall timer only if we actually processed something
  if(bytes_received > last_bytes_received)
  {
    last_progress_tick = HAL_GetTick();
    last_bytes_received = bytes_received;
  }
}

/* ================= OTA PULL ================= */
int ota_pull_data(uint8_t *buf, uint16_t maxlen)
{
  uint16_t len = 0;

  // Parse +IPD from DMA buffer
  for(;;)
  {
    uint16_t head = ESP_DMA_GetWritePos();
    if(esp_dma_rx_head == head)
      break;

    char c = esp_dma_rx_buf[esp_dma_rx_head++];
    if(esp_dma_rx_head >= ESP_DMA_RX_BUF_SIZE)
      esp_dma_rx_head = 0;

    // Copy to buf
    if(len < maxlen)
      buf[len++] = c;

    // Stop on first +IPD chunk complete
    if(len >= maxlen)
      break;
  }

  if(len > 0)
    DEBUG_LOG("Pulled %d bytes from DMA", len);
  return len;
}

/* ================= OTA TASK ================= */
void ESP8266_OTA_Task(void)
{
  if(!ota_active)
    return;

  int len = ota_pull_data(ota_rx_buf, OTA_RX_BUF_SIZE);
  if(len > 0)
    ota_process_rx(ota_rx_buf, len);

  // ───────────────────────────────────────────────
  // Stall / no-progress timeout detection
  // ───────────────────────────────────────────────
  uint32_t now = HAL_GetTick();
  if(ota_active && (now - last_progress_tick > 8000))
  {  // 8 seconds no change&& (last_bytes_received>0)
    if(bytes_received == last_bytes_received)
    {
      USER_LOG(
          "OTA stalled - no progress for 8s. Received %lu / %lu expected",
          bytes_received,
          ota_hdr.image_size + OTA_HEADER_SIZE);
      ota_error();  // set error state
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

  if(ESP_Init() != ESP8266_OK)
    return;

  char cmd[128];
  USER_LOG("Connecting to Wifi..");
  if(ESP_ConnectWiFi(WiFi_ssid, WiFi_pssd, ip, sizeof(ip)) != ESP8266_OK)
    return;

  // wait to obtain the IP address
  USER_LOG("Obtaining IP address..");
  uint32_t tickStart = HAL_GetTick();
  while((ESP_ConnState != ESP8266_CONNECTED_IP) && ((HAL_GetTick() - tickStart) < 5000));

  // Disable Multiple connections
  if(ESP_SendCommand("AT+CIPMUX=0\r\n", "OK", 2000) != ESP8266_OK)
    return;

  // Connect to the Server
  USER_LOG("Connecting to TCP SERVER");
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", SERVER_IP, SERVER_PORT);

  if(ESP_SendCommand(cmd, "CONNECT", 5000) != ESP8266_OK)
    return;

  ota_active = 1;

  USER_LOG("Initializing W25Q Flash");
  W25Q_Reset();

  flash_offset = OTA_FLASH_OFFSET;
  bytes_received = 0;
  header_received = 0;
  flash_erased = 0;
  actually_written = 0;
  in_ipd_payload = 0;
  ipd_remaining = 0;
  last_progress_tick = 0;
  last_bytes_received = 0;
  bytes_in_chunk = 0;

  // Request server to start sending data
  USER_LOG("Starting Reception..");
  const char req[] = "GET firmware\r\n";
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", strlen(req));
  if(ESP_SendCommand(cmd, ">", 2000) != ESP8266_OK)
  {
    ota_state = OTA_ERROR;
    return;
  }

  if(ESP_SendCommand((char*) req, "SEND OK", 2000) != ESP8266_OK)
  {
    ota_state = OTA_ERROR;
    return;
  }

  ota_state = OTA_RECEIVING;

  last_progress_tick = HAL_GetTick();
}

/* ================= OTA FINALIZE ================= */
static void ota_finalize(void)
{
  USER_LOG("OTA Written Succesfull, Data verified. Rebooting Now..\n\n");
  ESP_SendCommand("AT+CIPCLOSE\r\n", "OK", 2000);
  ota_active = 0;
  ota_state = OTA_COMPLETED;
  enable_ota_request();
}

static void ota_error(void)
{
  USER_LOG("OTA Failed.. Try again");
  ESP_SendCommand("AT+CIPCLOSE\r\n", "OK", 2000);
  ota_active = 0;
  ota_state = OTA_ERROR;
  printf("Application will resume\n");
}
