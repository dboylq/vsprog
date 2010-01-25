/***************************************************************************
 *   Copyright (C) 2009 by Simon Qian <SimonQian@SimonQian.com>            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app_cfg.h"
#include "app_type.h"
#include "app_err.h"
#include "app_log.h"
#include "prog_interface.h"

#include "memlist.h"
#include "pgbar.h"

#include "vsprog.h"
#include "programmer.h"
#include "target.h"

#include "psoc1.h"
#include "psoc1_internal.h"

#define CUR_TARGET_STRING			PSOC1_STRING

const struct program_area_map_t psoc1_program_area_map[] = 
{
	{APPLICATION_CHAR, 1, 0, 0, 0, AREA_ATTR_EW | AREA_ATTR_NP},
	{APPLICATION_CHKSUM_CHAR, 1, 0, 0, 0, AREA_ATTR_R},
	{LOCK_CHAR, 1, 0, 0, 0, AREA_ATTR_W | AREA_ATTR_NP},
	{0, 0, 0, 0, 0, 0}
};

const struct program_mode_t psoc1_program_mode[] = 
{
	{'r', "", ISSP},
	{'p', "", ISSP},
	{0, NULL, 0}
};

RESULT psoc1_enter_program_mode(struct program_context_t *context);
RESULT psoc1_leave_program_mode(struct program_context_t *context, 
								uint8_t success);
RESULT psoc1_erase_target(struct program_context_t *context, char area, 
							uint32_t addr, uint32_t page_size);
RESULT psoc1_write_target(struct program_context_t *context, char area, 
							uint32_t addr, uint8_t *buff, uint32_t size);
RESULT psoc1_read_target(struct program_context_t *context, char area, 
							uint32_t addr, uint8_t *buff, uint32_t size);
const struct program_functions_t psoc1_program_functions = 
{
	NULL,			// execute
	psoc1_enter_program_mode, 
	psoc1_leave_program_mode, 
	psoc1_erase_target, 
	psoc1_write_target, 
	psoc1_read_target
};


#define VECTORS_NUM				17
#define VECTORS_TABLE_SIZE		128

void psoc1_usage(void)
{
	printf("\
Usage of %s:\n\
  -m,  --mode <MODE>                        set mode<r|p>\n\n", 
			CUR_TARGET_STRING);
}

RESULT psoc1_parse_argument(char cmd, const char *argu)
{
	argu = argu;
	
	switch (cmd)
	{
	case 'h':
		psoc1_usage();
		break;
	default:
		return ERROR_FAIL;
		break;
	}
	
	return ERROR_OK;
}






static struct programmer_info_t *p = NULL;

#define PSOC1_SSC_CMD_SWBootReset			0x00
#define PSOC1_SSC_CMD_ReadBlock				0x01
#define PSOC1_SSC_CMD_WriteBlock			0x02
#define PSOC1_SSC_CMD_EraseBlock			0x03
#define PSOC1_SSC_CMD_ProtectBlock			0x04
#define PSOC1_SSC_CMD_EraseAll				0x05
#define PSOC1_SSC_CMD_TableRead				0x06
#define PSOC1_SSC_CMD_CheckSum				0x07
#define PSOC1_SSC_CMD_Calibrate0			0x08
#define PSOC1_SSC_CMD_Calibrate1			0x09

#define get_target_voltage(v)				p->get_target_voltage(v)

#define issp_init()							p->issp_init()
#define issp_fini()							p->issp_fini()
#define issp_enter_program_mode(mode)		p->issp_enter_program_mode(mode)
#define issp_leave_program_mode(mode)		p->issp_leave_program_mode(mode)
#define issp_wait_and_poll()				p->issp_wait_and_poll()
#define issp_commit()						p->issp_commit()

#define issp_0s()							\
							p->issp_vector(ISSP_VECTOR_0S, 0x00, 0x00, NULL)
#define issp_read_sram(addr, buf)			\
			p->issp_vector(ISSP_VECTOR_READ_SRAM, (uint8_t)(addr), 0x00, (buf))
#define issp_write_sram(addr, data)			\
	p->issp_vector(ISSP_VECTOR_WRITE_SRAM,(uint8_t)(addr),(uint8_t)(data),NULL)
#define issp_write_reg(addr, data)			\
	p->issp_vector(ISSP_VECTOR_WRITE_REG, (uint8_t)(addr), (uint8_t)(data),NULL)

#define issp_set_cup_a(cmd)					issp_write_reg(0xF0, (cmd))
#define issp_set_cup_sp(sp)					issp_write_reg(0xF6, (sp))
#define issp_set_cpu_f(f)					issp_write_reg(0xF7, (f))

#define issp_ssc_set_key1()					issp_write_sram(0xF8, 0x3A)
#define issp_ssc_set_key2(key2)				issp_write_sram(0xF9, (key2))
#define issp_ssc_set_blockid(id)			issp_write_sram(0xFA, (id))
#define issp_ssc_set_pointer(p)				issp_write_sram(0xFB, (p))
#define issp_ssc_set_clock(c)				issp_write_sram(0xFC, (c))
#define issp_ssc_set_delay(dly)				issp_write_sram(0xFE, (dly))
#define issp_ssc_set_cmd(cmd)				issp_set_cup_a(cmd)
#define issp_ssc_execute()					issp_write_reg(0xFF, 0x12)

#define issp_sel_reg_bank(xio)				issp_set_cpu_f((xio) ? 0x10 : 0x00)
#define issp_set_flash_bank(bank)			issp_write_reg(0xFA, (bank) & 0x03)

#define PSOC1_ISSP_SSC_DEFAULT_SP			0x08
#define PSOC1_ISSP_SSC_DEFAULT_POINTER		0x80
#define PSOC1_ISSP_SSC_DEFAULT_CLOCK_ERASE	0x15
#define PSOC1_ISSP_SSC_DEFAULT_CLOCK_FLASH	0x54
#define PSOC1_ISSP_SSC_DEFAULT_DELAY		0x56
#define PSOC1_ISSP_SSC_RETURN_OK			0x00

RESULT issp_wait_and_poll_with_ret(uint8_t *buf, uint8_t want_ssc_return_value)
{
	uint8_t i;

#ifdef PARAM_CHECK
	if (want_ssc_return_value > 8)
	{
		LOG_BUG(_GETTEXT(ERRMSG_INVALID_PARAMETER), __FUNCTION__);
		return ERRCODE_INVALID_PARAMETER;
	}
#endif
	
	if (ERROR_OK != issp_wait_and_poll())
	{
		return ERROR_FAIL;
	}
	
	for (i = 0; i < want_ssc_return_value; i++)
	{
		if (ERROR_OK != issp_read_sram(0xF8 + i, buf + i))
		{
			return ERROR_FAIL;
		}
	}
	if (ERROR_OK != issp_commit())
	{
		return ERROR_FAIL;
	}
	
	return ERROR_OK;
}

RESULT issp_init3_half(uint8_t f9_1, uint8_t f9_2)
{
	issp_write_reg(0xF7, 0x00);
	issp_write_reg(0xF4, 0x03);
	issp_write_reg(0xF5, 0x00);
	issp_write_reg(0xF6, 0x08);
	issp_write_reg(0xF8, 0x51);
	issp_write_reg(0xF9, f9_1);
	issp_write_reg(0xFA, 0x30);
	issp_write_reg(0xFF, 0x12);
	issp_0s();
	
	issp_write_reg(0xF7, 0x00);
	issp_write_reg(0xF4, 0x03);
	issp_write_reg(0xF5, 0x00);
	issp_write_reg(0xF6, 0x08);
	issp_write_reg(0xF8, 0x60);
	issp_write_reg(0xF9, f9_2);
	issp_write_reg(0xFA, 0x30);
	issp_write_reg(0xF7, 0x10);
	issp_write_reg(0xFF, 0x12);
	issp_0s();
	
	return ERROR_OK;
}

RESULT issp_call_ssc(uint8_t cmd, uint8_t id, uint8_t poll_ready, uint8_t * buf, 
					 uint8_t want_return)
{
	issp_sel_reg_bank(0x00);
	issp_set_cup_sp(PSOC1_ISSP_SSC_DEFAULT_SP);
	issp_ssc_set_key1();
	issp_ssc_set_key2(PSOC1_ISSP_SSC_DEFAULT_SP + 3);
	issp_write_reg(0xF5, 0x00);
	issp_write_reg(0xF4, 0x03);
	issp_ssc_set_pointer(0x80);
	issp_write_reg(0xF9, 0x30);
	issp_write_reg(0xFA, 0x40);
	issp_ssc_set_blockid(id);
	issp_ssc_set_cmd(cmd);
	issp_write_reg(0xF8, 0x00);
	issp_ssc_execute();
	
	if (poll_ready > 0)
	{
		return issp_wait_and_poll_with_ret(buf, want_return);
	}
	else
	{
		return issp_commit();
	}
}

RESULT psoc1_enter_program_mode(struct program_context_t *context)
{
	struct program_info_t *pi = context->pi;
	uint16_t voltage;
	
	p = context->prog;
	
	if (ERROR_OK != get_target_voltage(&voltage))
	{
		return ERROR_FAIL;
	}
	
	// ISSP Init
	if (ERROR_OK != issp_init())
	{
		LOG_ERROR(_GETTEXT(ERRMSG_FAILURE_OPERATION), "initialize issp");
		return ERRCODE_FAILURE_OPERATION;
	}
	
	// enter program mode
	switch (pi->mode)
	{
	case PSOC1_RESET_MODE:
		if (ERROR_OK != issp_enter_program_mode(ISSP_PM_RESET))
		{
			return ERRCODE_FAILURE_OPERATION;
		}
		break;
	case PSOC1_POWERON_MODE:
		if (voltage > 2000)
		{
			LOG_ERROR(_GETTEXT("Target should power off in power-on mode\n"));
			return ERROR_FAIL;
		}
		if (ERROR_OK != issp_enter_program_mode(ISSP_PM_POWER_ON))
		{
			return ERRCODE_FAILURE_OPERATION;
		}
		break;
	default:
		return ERROR_FAIL;
		break;
	}
	
	// init1 call_calibrate
	// call calibrate1
	if (ERROR_OK != issp_call_ssc(PSOC1_SSC_CMD_Calibrate1, 0, 1, NULL, 0))
	{
		LOG_ERROR(_GETTEXT(ERRMSG_FAILURE_OPERATION), 
					"call calibrate1");
		return ERRCODE_FAILURE_OPERATION;
	}
	// init2 read table no.1
	if (ERROR_OK != issp_call_ssc(PSOC1_SSC_CMD_TableRead, 1, 1, NULL, 0))
	{
		LOG_ERROR(_GETTEXT(ERRMSG_FAILURE_OPERATION), 
					"read table no.1");
		return ERRCODE_FAILURE_OPERATION;
	}
	// init3 do the hell
	if (voltage < 4000)
	{
		// 3.3V
		issp_init3_half(0xF8, 0xEA);
		issp_init3_half(0xF9, 0xE8);
	}
	else
	{
		// 5V
		issp_init3_half(0xFC, 0xEA);
		issp_init3_half(0xFD, 0xE8);
	}
	
	// init sys_clock
	issp_sel_reg_bank(1);
	issp_write_reg(0xE0, 0x02);
	return issp_commit();
}
RESULT psoc1_leave_program_mode(struct program_context_t *context, 
								uint8_t success)
{
	struct program_info_t *pi = context->pi;
	
	REFERENCE_PARAMETER(success);
	
	switch (pi->mode)
	{
	case PSOC1_RESET_MODE:
		if (ERROR_OK != issp_leave_program_mode(ISSP_PM_RESET))
		{
			return ERRCODE_FAILURE_OPERATION;
		}
		break;
	case PSOC1_POWERON_MODE:
		if (ERROR_OK != issp_leave_program_mode(ISSP_PM_POWER_ON))
		{
			return ERRCODE_FAILURE_OPERATION;
		}
		break;
	default:
		return ERROR_FAIL;
		break;
	}
	issp_fini();
	return issp_commit();
}

RESULT psoc1_erase_target(struct program_context_t *context, char area, 
							uint32_t addr, uint32_t page_size)
{
	uint8_t tmp8;
	RESULT ret;
	
	REFERENCE_PARAMETER(context);
	REFERENCE_PARAMETER(area);
	REFERENCE_PARAMETER(addr);
	REFERENCE_PARAMETER(page_size);
	
	issp_ssc_set_clock(PSOC1_ISSP_SSC_DEFAULT_CLOCK_ERASE);
	issp_ssc_set_delay(PSOC1_ISSP_SSC_DEFAULT_DELAY);
	
	ret = issp_call_ssc(PSOC1_SSC_CMD_EraseAll, 0, 1, &tmp8, 1);
	if ((ret != ERROR_OK) || (tmp8 != PSOC1_ISSP_SSC_RETURN_OK))
	{
		return ERRCODE_FAILURE_OPERATION;
	}
	return ERROR_OK;
}
RESULT psoc1_write_target(struct program_context_t *context, char area, 
							uint32_t addr, uint8_t *buff, uint32_t size)
{
	struct chip_param_t *param = context->param;
	uint8_t bank, bank_num;
	uint16_t block;
	uint32_t page_num, page_size;
	uint32_t i;
	uint8_t tmp8;
	RESULT ret = ERROR_OK;
	
	REFERENCE_PARAMETER(size);
	REFERENCE_PARAMETER(addr);
	
	page_size = param->chip_areas[APPLICATION_IDX].page_size;
	page_num = param->chip_areas[APPLICATION_IDX].page_num;
	bank_num = (uint8_t)param->param[PSOC1_PARAM_BANK_NUM];
	switch (area)
	{
	case APPLICATION_CHAR:
		for (bank = 0; bank < bank_num; bank++)
		{
			// select bank by write xio in fls_pr1(in reg_bank 1)
			if (param->param[PSOC1_PARAM_BANK_NUM] > 1)
			{
				issp_sel_reg_bank(1);
				issp_set_flash_bank(bank);
				issp_sel_reg_bank(0);
			}
			
			for (block = 0; block < page_num; block++)
			{
				uint32_t block_num = bank * page_num + block;
				uint32_t block_addr = block_num * page_size;
				
				// write data into sram
				for (i = 0; i < page_size; i++)
				{
					issp_write_sram(PSOC1_ISSP_SSC_DEFAULT_POINTER + i, 
									buff[block_addr + i]);
				}
				issp_ssc_set_clock(PSOC1_ISSP_SSC_DEFAULT_CLOCK_FLASH);
				issp_ssc_set_delay(PSOC1_ISSP_SSC_DEFAULT_DELAY);
				
				ret = issp_call_ssc(PSOC1_SSC_CMD_WriteBlock, 
									(uint8_t)(block & 0xFF), 1, &tmp8, 1);
				if ((ret != ERROR_OK) || (tmp8 != PSOC1_ISSP_SSC_RETURN_OK))
				{
					ret = ERRCODE_FAILURE_OPERATION;
					break;
				}
				pgbar_update(page_size);
			}
			if (ret != ERROR_OK)
			{
				break;
			}
		}
		break;
	case LOCK_CHAR:
		for (bank = 0; bank < bank_num; bank++)
		{
			uint32_t lock_bank_addr = bank * (page_num >> 2);
			
			if (bank_num > 1)
			{
				issp_sel_reg_bank(1);
				issp_set_flash_bank(bank);
				issp_sel_reg_bank(0);
			}
			for (i = 0; i < (page_num >> 2); i++)
			{
				issp_write_sram(PSOC1_ISSP_SSC_DEFAULT_POINTER + i, 
								buff[lock_bank_addr + i]);
			}
			issp_ssc_set_clock(PSOC1_ISSP_SSC_DEFAULT_CLOCK_FLASH);
			issp_ssc_set_delay(PSOC1_ISSP_SSC_DEFAULT_DELAY);
			
			ret = issp_call_ssc(PSOC1_SSC_CMD_ProtectBlock, 0, 1, &tmp8, 1);
			if ((ret != ERROR_OK) || (tmp8 != PSOC1_ISSP_SSC_RETURN_OK))
			{
				ret = ERRCODE_FAILURE_OPERATION;
				break;
			}
			
			pgbar_update(1);
		}
		break;
	}
	return ret;
}

RESULT psoc1_read_target(struct program_context_t *context, char area, 
							uint32_t addr, uint8_t *buff, uint32_t size)
{
	struct chip_param_t *param = context->param;
	struct program_info_t *pi = context->pi;
	uint8_t bank, bank_num;
	uint16_t block;
	uint32_t page_num, page_size;
	uint32_t i;
	uint8_t tmp8;
	RESULT ret = ERROR_OK;
	
	REFERENCE_PARAMETER(size);
	REFERENCE_PARAMETER(addr);
	
	page_size = param->chip_areas[APPLICATION_IDX].page_size;
	page_num = param->chip_areas[APPLICATION_IDX].page_num;
	bank_num = (uint8_t)param->param[PSOC1_PARAM_BANK_NUM];
	switch (area)
	{
	case CHIPID_CHAR:
		// call table_read no.0 and read 2 bytes from 0xF8 in sram
		pi->chip_id = 0;
		ret = issp_call_ssc(PSOC1_SSC_CMD_TableRead, 0, 1, (uint8_t*)&pi->chip_id, 2);
		if (ret != ERROR_OK)
		{
			ret = ERRCODE_FAILURE_OPERATION;
			break;
		}
		break;
	case APPLICATION_CHAR:
		for (bank = 0; bank < bank_num; bank++)
		{
			if (bank_num > 1)
			{
				issp_sel_reg_bank(1);
				issp_set_flash_bank(bank);
				issp_sel_reg_bank(0);
			}
			
			for (block = 0; block < page_num; block++)
			{
				uint32_t block_num = bank * page_num + block;
				uint32_t block_addr = block_num * page_size;
				
				ret = issp_call_ssc(PSOC1_SSC_CMD_ReadBlock, 
									(uint8_t)(block & 0xFF), 1, &tmp8, 1);
				if ((ret != ERROR_OK) || (tmp8 != PSOC1_ISSP_SSC_RETURN_OK))
				{
					ret = ERRCODE_FAILURE_OPERATION;
					break;
				}
				
				for (i = 0; i < page_size; i++)
				{
					issp_read_sram(PSOC1_ISSP_SSC_DEFAULT_POINTER + i, 
									buff + block_addr + i);
				}
				
				// commit
				if (ERROR_OK != issp_commit())
				{
					ret = ERROR_FAIL;
					break;
				}
				
				pgbar_update(page_size);
			}
			if (ret != ERROR_OK)
			{
				break;
			}
		}
		break;
	}
	return ret;
}
