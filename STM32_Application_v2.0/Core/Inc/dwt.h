/*
 * dwt.h
 *
 *  Created on: Feb 25, 2026
 *      Author: Rubin Khadka
 */

#ifndef DWT_H_
#define DWT_H_

#include "stdint.h"

#ifndef DWT_CPU_MHZ
#define DWT_CPU_MHZ 72
#endif

// Function Definitions
void DWT_Init(void);
void DWT_Delay_us(uint32_t us);
void DWT_Delay_ms(uint32_t ms);

#endif /* DWT_H_ */
