/*
 * spi2.h
 *
 *  Created on: Apr 18, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_SPI2_H_
#define INC_SPI2_H_

// CS Control for W25Q64 (PB6)
#define SPI2_CS_LOW()       (GPIOB->BRR = GPIO_BRR_BR12)
#define SPI2_CS_HIGH()      (GPIOB->BSRR = GPIO_BSRR_BS12)

// Function Prototypes
void SPI2_Init(void);
uint8_t SPI2_Transfer(uint8_t data);

#endif /* INC_SPI2_H_ */
