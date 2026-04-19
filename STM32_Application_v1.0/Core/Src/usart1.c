/*
 * usart1.c
 *
 *  Created on: Apr 19, 2026
 *      Author: Rubin Khadka
 */

#include "main.h"
#include "usart1.h"

#define USART1_RX_DMA_SIZE 512    // DMA buffer for firmware reception
#define USART1_TX_BUF_SIZE 256    // TX ring buffer for commands

/* DMA buffer for RX */
static uint8_t usart1_rx_dma_buffer[USART1_RX_DMA_SIZE];
static volatile uint16_t usart1_rx_read_pos = 0;

/* TX ring buffer */
static uint8_t USART1_txbuf_storage[USART1_TX_BUF_SIZE];
volatile USART1_Buffer_t usart1_tx_buf;

static volatile uint8_t usart1_rx_chunk_ready = 0;

void USART1_Init(void)
{
  // Enable clocks
  RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
  RCC->AHBENR |= RCC_AHBENR_DMA1EN;  // Enable DMA1 clock

  // PA9 as TX (Alternate function push-pull)
  GPIOA->CRH &= ~(GPIO_CRH_CNF9 | GPIO_CRH_MODE9);
  GPIOA->CRH |= GPIO_CRH_CNF9_1 | GPIO_CRH_MODE9;

  // PA10 as RX (Floating input)
  GPIOA->CRH &= ~(GPIO_CRH_CNF10 | GPIO_CRH_MODE10);
  GPIOA->CRH |= GPIO_CRH_CNF10_0;

  // Disable USART
  USART1->CR1 &= ~USART_CR1_UE;

  // 115200 baud @ 72MHz
  USART1->BRR = 0x271;

  // Clear status
  USART1->SR = 0;

  // Initialize TX Buffer
  UART1_BufferInit(&usart1_tx_buf, USART1_txbuf_storage, USART1_TX_BUF_SIZE);

  // Configure DMA for RX (Circular Mode)
  DMA1_Channel5->CCR &= ~DMA_CCR_EN;

  // Configure DMA
  DMA1_Channel5->CPAR = (uint32_t) &(USART1->DR);            // Peripheral address
  DMA1_Channel5->CMAR = (uint32_t) usart1_rx_dma_buffer;   // Memory address
  DMA1_Channel5->CNDTR = USART1_RX_DMA_SIZE;   // Number of bytes

  DMA1_Channel5->CCR = 0;   // Reset Register First
  DMA1_Channel5->CCR = DMA_CCR_MINC |         // Memory increment
      DMA_CCR_CIRC |        // Circular mode
      DMA_CCR_PL;           // Highest priority

  // Enable DMA channel
  DMA1_Channel5->CCR |= DMA_CCR_EN;

  // Enable USART DMA RX
  USART1->CR3 |= USART_CR3_DMAR;

  // Configure USART
  USART1->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_UE;

  // Enable Interrupts
  NVIC_EnableIRQ(DMA1_Channel5_IRQn);   // DMA
  NVIC_EnableIRQ(USART1_IRQn);          // TX
}

// Get current DMA write position
uint16_t USART1_DMA_GetWritePos(void)
{
  return USART1_RX_DMA_SIZE - DMA1_Channel5->CNDTR;
}

bool USART1_DMA_WaitForChunk(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  while(!usart1_rx_chunk_ready)
  {
    if((HAL_GetTick() - start) > timeout_ms)
    {
      return false;  // Timeout
    }
  }
  usart1_rx_chunk_ready = 0;
  return true;
}

// Get number of bytes available in DMA buffer
uint16_t USART1_DMA_GetAvailable(void)
{
  uint16_t write_pos = USART1_DMA_GetWritePos();
  uint16_t read_pos = usart1_rx_read_pos;

  if(write_pos >= read_pos)
  {
    return write_pos - read_pos;
  }
  else
  {
    return (USART1_RX_DMA_SIZE - read_pos) + write_pos;
  }
}

// Read data directly from DMA buffer
uint16_t USART1_DMA_Read(uint8_t *buffer, uint16_t max_len)
{
  uint16_t available = USART1_DMA_GetAvailable();
  uint16_t to_read = (available < max_len) ? available : max_len;
  uint16_t read_pos = usart1_rx_read_pos;

  for(uint16_t i = 0; i < to_read; i++)
  {
    buffer[i] = usart1_rx_dma_buffer[read_pos];
    read_pos++;
    if(read_pos >= USART1_RX_DMA_SIZE)
    {
      read_pos = 0;
    }
  }

  usart1_rx_read_pos = read_pos;
  return to_read;
}

// Read exact 512-byte chunk
uint16_t USART1_DMA_ReadChunk(uint8_t *buffer)
{
  // Wait for full 512-byte chunk
  while(USART1_DMA_GetAvailable() < 512);

  // Read exactly 512 bytes
  return USART1_DMA_Read(buffer, 512);
}

// Check if 512-byte chunk is ready (non-blocking)
bool USART1_DMA_IsChunkReady(void)
{
  return (USART1_DMA_GetAvailable() >= 512);
}

// DMA Interrupt Handler
void DMA1_Channel5_IRQHandler(void)
{
  if(DMA1->ISR & DMA_ISR_TCIF5)
  {
    // Full 512-byte transfer complete
    DMA1->IFCR |= DMA_IFCR_CTCIF5;
    usart1_rx_chunk_ready = 1;
  }

  if(DMA1->ISR & DMA_ISR_HTIF5)
  {
    DMA1->IFCR |= DMA_IFCR_CHTIF5;
  }
}

// ========== TX Buffer Functions ==========

void UART1_BufferInit(volatile USART1_Buffer_t *buff, uint8_t *storage, uint16_t size)
{
  buff->buffer = storage;
  buff->size = size;
  buff->head = 0;
  buff->tail = 0;
  buff->count = 0;
}

bool USART1_BufferEmpty(volatile USART1_Buffer_t *buff)
{
  return (buff->count == 0);
}

bool USART1_BufferFull(volatile USART1_Buffer_t *buff)
{
  return (buff->count >= buff->size);
}

bool USART1_BufferWrite(volatile USART1_Buffer_t *buff, uint8_t data)
{
  __disable_irq();

  if(USART1_BufferFull(buff))
  {
    __enable_irq();
    return false;
  }

  buff->buffer[buff->head] = data;
  buff->head = (buff->head + 1) % buff->size;
  buff->count++;

  __enable_irq();
  return true;
}

uint8_t USART1_BufferRead(volatile USART1_Buffer_t *buff)
{
  uint8_t data = 0;

  __disable_irq();

  if(!USART1_BufferEmpty(buff))
  {
    data = buff->buffer[buff->tail];
    buff->tail = (buff->tail + 1) % buff->size;
    buff->count--;
  }

  __enable_irq();
  return data;
}

// Send character using TX buffer (for commands)
void USART1_SendChar(char c)
{
  while(USART1_BufferFull(&usart1_tx_buf));

  __disable_irq();
  USART1_BufferWrite(&usart1_tx_buf, (uint8_t) c);
  USART1->CR1 |= USART_CR1_TXEIE;
  __enable_irq();
}

// Send string using TX buffer
void USART1_SendString(const char *str)
{
  while(*str)
  {
    USART1_SendChar(*str++);
  }
}

// Send number as string
void USART1_SendNumber(uint32_t num)
{
  char buffer[16];
  int i = 0;

  if(num == 0)
  {
    USART1_SendChar('0');
    return;
  }

  while(num > 0)
  {
    buffer[i++] = '0' + (num % 10);
    num /= 10;
  }

  while(i > 0)
  {
    USART1_SendChar(buffer[--i]);
  }
}

// Send hex byte
void USART1_SendHex(uint8_t value)
{
  char hex[3];
  hex[0] = "0123456789ABCDEF"[value >> 4];
  hex[1] = "0123456789ABCDEF"[value & 0x0F];
  hex[2] = 0;
  USART1_SendString(hex);
}

// USART1 TX Interrupt Handler
void USART1_IRQHandler(void)
{
  // Handle TX interrupt
  if((USART1->CR1 & USART_CR1_TXEIE) && (USART1->SR & USART_SR_TXE))
  {
    if(!USART1_BufferEmpty(&usart1_tx_buf))
    {
      USART1->DR = USART1_BufferRead(&usart1_tx_buf);
    }

    if(USART1_BufferEmpty(&usart1_tx_buf))
    {
      USART1->CR1 &= ~USART_CR1_TXEIE;
    }
  }
}

