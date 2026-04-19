/*
 * w25q64.c
 *
 *  Created on: Feb 27, 2026
 *      Author: Rubin Khadka
 */

#include "main.h"
#include "W25q64.h"
#include "spi2.h"
#include "dwt.h"
#include "USART2.h"

// W25Q64 Commands
#define W25Q64_CMD_RESET            0x99
#define W25Q64_CMD_RESET_ENABLE     0x66
#define W25Q64_CMD_READ_ID          0x9F
#define W25Q64_CMD_READ_STATUS      0x05
#define W25Q64_CMD_WRITE_ENABLE     0x06
#define W25Q64_CMD_WRITE_DISABLE    0x04
#define W25Q64_CMD_READ_DATA        0x03
#define W25Q64_CMD_FAST_READ        0x0B
#define W25Q64_CMD_PAGE_PROGRAM     0x02
#define W25Q64_CMD_SECTOR_ERASE     0x20

#define W25Q64_PAGE_SIZE    256
#define W25Q64_SECTOR_SIZE  4096

static void W25Q64_WaitForReady(void)
{
  uint8_t status;
  do
  {
    SPI2_CS_LOW();
    SPI2_Transfer(W25Q64_CMD_READ_STATUS);
    status = SPI2_Transfer(0xFF);
    SPI2_CS_HIGH();

    if(status & 0x01)
      DWT_Delay_ms(1);
  }
  while(status & 0x01);
}

static void W25Q64_WriteEnable(void)
{
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_WRITE_ENABLE);
  SPI2_CS_HIGH();
  DWT_Delay_us(5);
}

static void W25Q64_WriteDisable(void)
{
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_WRITE_DISABLE);
  SPI2_CS_HIGH();
  DWT_Delay_us(5);
}

/* ================= PUBLIC API (Byte Address) ================= */

// Initialization and Identification
void W25Q64_Init(void)
{
  uint8_t manufacturer_id = 0;
  uint8_t memory_type = 0;
  uint8_t capacity = 0;

  // Read full JEDEC ID
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_READ_ID);  // 0x9F

  manufacturer_id = SPI2_Transfer(0xFF);
  memory_type = SPI2_Transfer(0xFF);
  capacity = SPI2_Transfer(0xFF);

  SPI2_CS_HIGH();

  // Check manufacturer ID (should be 0xEF for Winbond)
  if(manufacturer_id == 0xEF)
  {
    // UART output
    USART2_SendString("W25Q64 Found! JEDEC ID: ");
    USART2_SendHex(manufacturer_id);
    USART2_SendString(" ");
    USART2_SendHex(memory_type);
    USART2_SendString(" ");
    USART2_SendHex(capacity);
    USART2_SendString("\r\n");

    // Check if it's really W25Q64 (memory type 0x40, capacity 0x17)
    if(memory_type == 0x40 && capacity == 0x17)
    {
      USART2_SendString("W25Q64 Detected (64Mbit)\r\n");
    }
  }
  else
  {
    USART2_SendString("W25Q64 Error! Manufacturer ID: 0x");
    USART2_SendHex(manufacturer_id);
    USART2_SendString("\r\n");
  }
}

void W25Q64_Reset(void)
{
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_RESET_ENABLE);
  SPI2_Transfer(W25Q64_CMD_RESET);
  while(SPI2->SR & SPI_SR_BSY);
  SPI2_CS_HIGH();
  DWT_Delay_ms(100);
}

uint32_t W25Q64_ReadID(void)
{
  uint8_t rData[3];

  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_READ_ID);
  rData[0] = SPI2_Transfer(0xFF);
  rData[1] = SPI2_Transfer(0xFF);
  rData[2] = SPI2_Transfer(0xFF);
  SPI2_CS_HIGH();

  return ((rData[0] << 16) | (rData[1] << 8) | rData[2]);
}

uint8_t W25Q64_ReadStatus(void)
{
  uint8_t status;
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_READ_STATUS);
  status = SPI2_Transfer(0xFF);
  SPI2_CS_HIGH();
  return status;
}

void W25Q64_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_READ_DATA);
  SPI2_Transfer((addr >> 16) & 0xFF);
  SPI2_Transfer((addr >> 8) & 0xFF);
  SPI2_Transfer(addr & 0xFF);

  for(uint32_t i = 0; i < len; i++)
  {
    data[i] = SPI2_Transfer(0xFF);
  }

  SPI2_CS_HIGH();
  DWT_Delay_us(5);
}

void W25Q64_FastRead(uint32_t addr, uint8_t *data, uint32_t len)
{
  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_FAST_READ);
  SPI2_Transfer((addr >> 16) & 0xFF);
  SPI2_Transfer((addr >> 8) & 0xFF);
  SPI2_Transfer(addr & 0xFF);
  SPI2_Transfer(0x00);  // Dummy byte

  for(uint32_t i = 0; i < len; i++)
  {
    data[i] = SPI2_Transfer(0xFF);
  }

  SPI2_CS_HIGH();
  DWT_Delay_us(5);
}

void W25Q64_EraseSector(uint32_t addr)
{
  W25Q64_WriteEnable();

  SPI2_CS_LOW();
  SPI2_Transfer(W25Q64_CMD_SECTOR_ERASE);
  SPI2_Transfer((addr >> 16) & 0xFF);
  SPI2_Transfer((addr >> 8) & 0xFF);
  SPI2_Transfer(addr & 0xFF);
  while(SPI2->SR & SPI_SR_BSY);
  SPI2_CS_HIGH();

  W25Q64_WaitForReady();
  W25Q64_WriteDisable();
}

void W25Q64_Erase(uint32_t start_addr, uint32_t size)
{
  uint32_t first_sector = start_addr / W25Q64_SECTOR_SIZE;
  uint32_t last_sector = (start_addr + size - 1) / W25Q64_SECTOR_SIZE;

  for(uint32_t sector = first_sector; sector <= last_sector; sector++)
  {
    W25Q64_EraseSector(sector * W25Q64_SECTOR_SIZE);
  }
}

void W25Q64_Write(uint32_t addr, uint8_t *data, uint32_t len)
{
  uint32_t to_write;
  uint32_t page_remain;

  while(len > 0)
  {
    page_remain = W25Q64_PAGE_SIZE - (addr % W25Q64_PAGE_SIZE);
    to_write = (len < page_remain) ? len : page_remain;

    W25Q64_WriteEnable();

    SPI2_CS_LOW();
    SPI2_Transfer(W25Q64_CMD_PAGE_PROGRAM);
    SPI2_Transfer((addr >> 16) & 0xFF);
    SPI2_Transfer((addr >> 8) & 0xFF);
    SPI2_Transfer(addr & 0xFF);

    for(uint32_t i = 0; i < to_write; i++)
    {
      SPI2_Transfer(data[i]);
    }

    while(SPI2->SR & SPI_SR_BSY);
    SPI2_CS_HIGH();

    W25Q64_WaitForReady();
    W25Q64_WriteDisable();

    addr += to_write;
    data += to_write;
    len -= to_write;
  }
}
