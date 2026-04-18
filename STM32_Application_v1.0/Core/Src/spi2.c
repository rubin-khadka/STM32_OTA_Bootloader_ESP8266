/*
 * spi2.c
 *
 *  Created on: Apr 18, 2026
 *      Author: Rubin Khadka
 */

#include "stm32f103xb.h"

void SPI2_Init(void)
{
  // Enable SPI2
  SPI2->CR1 |= SPI_CR1_SPE;
}

uint8_t SPI2_Transfer(uint8_t data)
{
  // Wait for TX buffer empty
  while(!(SPI2->SR & SPI_SR_TXE));

  // Send data
  SPI2->DR = data;

  // Wait for RX buffer not empty
  while(!(SPI2->SR & SPI_SR_RXNE));

  // Read data
  uint8_t received = SPI2->DR;

  return received;
}
