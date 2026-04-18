/*
 * w25q64.h
 *
 *  Created on: Feb 25, 2026
 *      Author: Rubin Khadka
 */

#ifndef W25Q64_H_
#define W25Q64_H_

#define W25Q64_OK       0
#define W25Q64_ERROR    1
#define W25Q64_BUSY     2

// Function Prototypes
void W25Q64_Init(void);

// Status functions
void W25Q64_Reset(void);
uint8_t W25Q64_ReadStatus(void);
void W25Q64_WriteEnable(void);
void W25Q64_WriteDisable(void);
uint32_t bytestowrite(uint32_t size, uint16_t offset);

// Read functions
void W25Q64_Read(uint32_t startPage, uint8_t offset, uint32_t size, uint8_t *rData);
void W25Q64_FastRead(uint32_t startPage, uint8_t offset, uint32_t size, uint8_t *rData);

// Erase function
uint8_t W25Q64_EraseSector(uint32_t sector_num);

// Write function
void W25Q64_WritePage(uint32_t page, uint16_t offset, uint32_t size, uint8_t *data);

#endif /* W25Q64_H_ */
