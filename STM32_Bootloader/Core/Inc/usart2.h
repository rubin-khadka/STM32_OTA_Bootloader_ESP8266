/*
 * usart2.h
 *
 *  Created on: Mar 10, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_USART2_H_
#define INC_USART2_H_

#include <stdint.h>
#include <stdbool.h>

// Buffer structure
typedef struct
{
  uint8_t *buffer;
  uint16_t size;
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint16_t count;
} USART2_Buffer_t;

// Public functions
void USART2_Init(void);
void USART2_SendChar(char c);
void USART2_SendString(const char *str);
void USART2_SendNumber(uint32_t num);
void USART2_SendHex(uint8_t value);
bool USART2_DataAvailable(void);
uint8_t USART2_GetChar(void);

#endif /* INC_USART2_H_ */
