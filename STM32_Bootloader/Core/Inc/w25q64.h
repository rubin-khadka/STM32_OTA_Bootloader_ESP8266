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

// Page and sector sizes
#define W25Q64_PAGE_SIZE    256
#define W25Q64_SECTOR_SIZE  4096

// Function Prototypes - Byte Address Interface
void W25Q64_Reset(void);
uint32_t W25Q64_ReadID(void);
uint8_t W25Q64_ReadStatus(void);

// Read functions
void W25Q64_Read(uint32_t addr, uint8_t *data, uint32_t len);
void W25Q64_FastRead(uint32_t addr, uint8_t *data, uint32_t len);

// Erase functions
void W25Q64_EraseSector(uint32_t addr);
void W25Q64_Erase(uint32_t start_addr, uint32_t size);

// Write function
void W25Q64_Write(uint32_t addr, uint8_t *data, uint32_t len);

#endif /* W25Q64_H_ */
