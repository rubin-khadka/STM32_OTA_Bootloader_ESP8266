/*
 * usart1.h
 *
 *  Created on: Apr 19, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_USART1_H_
#define INC_USART1_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
  uint8_t *buffer;
  uint16_t size;
  uint16_t head;
  uint16_t tail;
  uint16_t count;
} USART1_Buffer_t;

// Initialization
void USART1_Init(void);
void UART1_BufferInit(volatile USART1_Buffer_t *buff, uint8_t *storage, uint16_t size);

// DMA RX functions (for firmware reception)
bool USART1_DMA_WaitForChunk(uint32_t timeout_ms);
uint16_t USART1_DMA_GetAvailable(void);
uint16_t USART1_DMA_Read(uint8_t *buffer, uint16_t max_len);
uint16_t USART1_DMA_ReadChunk(uint8_t *buffer);
bool USART1_DMA_IsChunkReady(void);

// TX functions (for sending AT commands)
void USART1_SendChar(char c);
void USART1_SendString(const char *str);
void USART1_SendNumber(uint32_t num);
void USART1_SendHex(uint8_t value);

// Buffer operations (for TX only)
bool USART1_BufferWrite(volatile USART1_Buffer_t *buff, uint8_t data);
uint8_t USART1_BufferRead(volatile USART1_Buffer_t *buff);
bool USART1_BufferEmpty(volatile USART1_Buffer_t *buff);
bool USART1_BufferFull(volatile USART1_Buffer_t *buff);

#endif /* INC_USART1_H_ */
