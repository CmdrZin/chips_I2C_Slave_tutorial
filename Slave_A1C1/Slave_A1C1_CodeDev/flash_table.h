/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2016 Nels D. "Chip" Pearson (aka CmdrZin)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * flash_table.h
 *
 * Created: 8/08/2015	0.01	ndp
 *  Author: Chip
 *
 * revision: 01/13/2016	0.02	ndp		Add copy table to SRAM.
 */ 


#ifndef FLASH_TABLE_H_
#define FLASH_TABLE_H_

#include "sysdefs.h"

uint8_t flash_get_mod_access_id(uint8_t index);
MOD_FUNCTION_ENTRY* flash_get_mod_function_table(uint8_t index);

uint16_t flash_get_access_cmd(uint8_t index, MOD_FUNCTION_ENTRY* table);
uint16_t flash_get_access_func(uint8_t index, MOD_FUNCTION_ENTRY* table);

void flash_copy8(uint16_t index, const ICON_DATA* table, uint8_t* sram);

#endif /* FLASH_TABLE_H_ */