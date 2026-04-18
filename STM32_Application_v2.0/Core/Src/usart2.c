/*
 * usart2.c
 *
 *  Created on: Mar 10, 2026
 *      Author: Rubin Khadka
 */

#include "stm32f103xb.h"
#include "usart2.h"

// Buffer sizes
#define USART2_RX_BUF_SIZE 64
#define USART2_TX_BUF_SIZE 128

/* Global buffer instances */
static uint8_t USART2_rxbuf_storage[USART2_RX_BUF_SIZE];
static uint8_t USART2_txbuf_storage[USART2_TX_BUF_SIZE];
volatile USART2_Buffer_t usart2_rx_buf;
volatile USART2_Buffer_t usart2_tx_buf;

// Helper function declarations
static void UART2_BufferInit(volatile USART2_Buffer_t *buff, uint8_t *storage, uint16_t size);
static bool USART2_BufferEmpty(volatile USART2_Buffer_t *buff);
static bool USART2_BufferFull(volatile USART2_Buffer_t *buff);
static bool USART2_BufferWrite(volatile USART2_Buffer_t *buff, uint8_t data);
static uint8_t USART2_BufferRead(volatile USART2_Buffer_t *buff);

void USART2_Init(void)
{
  // Enable clocks for USART2 (APB1) and GPIOA
  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;      // GPIOA clock
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;    // USART2 clock

  // Configure PA2 as TX (Alternate function push-pull)
  GPIOA->CRL &= ~(GPIO_CRL_CNF2 | GPIO_CRL_MODE2);
  GPIOA->CRL |= GPIO_CRL_CNF2_1 | GPIO_CRL_MODE2;  // AF Push-pull, 50MHz

  // Configure PA3 as RX (Floating input)
  GPIOA->CRL &= ~(GPIO_CRL_CNF3 | GPIO_CRL_MODE3);
  GPIOA->CRL |= GPIO_CRL_CNF3_0;  // Floating input

  // Disable USART2 during configuration
  USART2->CR1 &= ~USART_CR1_UE;

  USART2->BRR = 0x139;  // 72MHz/115200 = 625 = 0x271

  // Clear status
  USART2->SR = 0;

  // Initialize buffers
  UART2_BufferInit(&usart2_rx_buf, USART2_rxbuf_storage, USART2_RX_BUF_SIZE);
  UART2_BufferInit(&usart2_tx_buf, USART2_txbuf_storage, USART2_TX_BUF_SIZE);

  // Configure USART2: enable RX, TX, RX interrupt
  USART2->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE | USART_CR1_UE;

  // Enable interrupt in NVIC
  NVIC_EnableIRQ(USART2_IRQn);
  NVIC_SetPriority(USART2_IRQn, 2);  // Lower priority than USART1 (which is handling ESP8266)
}

static void UART2_BufferInit(volatile USART2_Buffer_t *buff, uint8_t *storage, uint16_t size)
{
  buff->buffer = storage;
  buff->size = size;
  buff->head = 0;
  buff->tail = 0;
  buff->count = 0;
}

static bool USART2_BufferEmpty(volatile USART2_Buffer_t *buff)
{
  return (buff->count == 0);
}

static bool USART2_BufferFull(volatile USART2_Buffer_t *buff)
{
  return (buff->count >= buff->size);
}

static bool USART2_BufferWrite(volatile USART2_Buffer_t *buff, uint8_t data)
{
  __disable_irq();

  if(USART2_BufferFull(buff))
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

static uint8_t USART2_BufferRead(volatile USART2_Buffer_t *buff)
{
  uint8_t data = 0;

  __disable_irq();

  if(!USART2_BufferEmpty(buff))
  {
    data = buff->buffer[buff->tail];
    buff->tail = (buff->tail + 1) % buff->size;
    buff->count--;
  }

  __enable_irq();
  return data;
}

bool USART2_DataAvailable(void)
{
  return !USART2_BufferEmpty(&usart2_rx_buf);
}

uint8_t USART2_GetChar(void)
{
  return USART2_BufferRead(&usart2_rx_buf);
}

void USART2_SendChar(char c)
{
  // Wait if TX buffer is full
  while(USART2_BufferFull(&usart2_tx_buf));

  __disable_irq();

  // Write to TX buffer
  USART2_BufferWrite(&usart2_tx_buf, (uint8_t) c);

  // Enable TX interrupt
  USART2->CR1 |= USART_CR1_TXEIE;

  __enable_irq();
}

void USART2_SendString(const char *str)
{
  while(*str)
  {
    USART2_SendChar(*str++);
  }
}

void USART2_SendNumber(uint32_t num)
{
  char buffer[16];
  int i = 0;

  if(num == 0)
  {
    USART2_SendChar('0');
    return;
  }

  while(num > 0)
  {
    buffer[i++] = '0' + (num % 10);
    num /= 10;
  }

  while(i > 0)
  {
    USART2_SendChar(buffer[--i]);
  }
}

void USART2_SendHex(uint8_t value)
{
  char hex[3];
  hex[0] = "0123456789ABCDEF"[value >> 4];
  hex[1] = "0123456789ABCDEF"[value & 0x0F];
  hex[2] = 0;
  USART2_SendString(hex);
}

// USART2 Interrupt Handler
void USART2_IRQHandler(void)
{
  // Handle received data
  if(USART2->SR & USART_SR_RXNE)
  {
    uint8_t data = USART2->DR;
    USART2_BufferWrite(&usart2_rx_buf, data);
  }

  // Handle transmit
  if((USART2->CR1 & USART_CR1_TXEIE) && (USART2->SR & USART_SR_TXE))
  {
    if(!USART2_BufferEmpty(&usart2_tx_buf))
    {
      USART2->DR = USART2_BufferRead(&usart2_tx_buf);
    }

    // Disable TX interrupt if buffer is empty
    if(USART2_BufferEmpty(&usart2_tx_buf))
    {
      USART2->CR1 &= ~USART_CR1_TXEIE;
    }
  }
}
