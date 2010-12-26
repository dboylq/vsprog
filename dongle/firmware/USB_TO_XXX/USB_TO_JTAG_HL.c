/**************************************************************************
 *  Copyright (C) 2008 - 2010 by Simon Qian                               *
 *  SimonQian@SimonQian.com                                               *
 *                                                                        *
 *  Project:    Versaloon                                                 *
 *  File:       USB_TO_JTAG_HL.c                                          *
 *  Author:     SimonQian                                                 *
 *  Versaion:   See changelog                                             *
 *  Purpose:    implementation file for USB_TO_JTAG_HL                    *
 *  License:    See license                                               *
 *------------------------------------------------------------------------*
 *  Change Log:                                                           *
 *      YYYY-MM-DD:     What(by Who)                                      *
 *      2008-11-07:     created(by SimonQian)                             *
 **************************************************************************/

#include "app_cfg.h"
#if USB_TO_JTAG_HL_EN

#include "USB_TO_XXX.h"
#include "interfaces.h"

void USB_TO_JTAG_HL_ProcessCmd(uint8* dat, uint16 len)
{
	uint16 index, device_idx, length;
	uint8 command;
	
	uint16 cur_dat_len, i, len_tmp;
	uint16_t rindex;
	bool fail;
	
	index = 0;
	while(index < len)
	{
		command = dat[index] & USB_TO_XXX_CMDMASK;
		device_idx = dat[index] & USB_TO_XXX_IDXMASK;
		length = GET_LE_U16(&dat[index + 1]);
		index += 3;
		
		switch(command)
		{
		case USB_TO_XXX_INIT:
			if (ERROR_OK == interfaces->jtag_hl.init(device_idx))
			{
				buffer_reply[rep_len++] = USB_TO_XXX_OK;
			}
			else
			{
				buffer_reply[rep_len++] = USB_TO_XXX_FAILED;
			}
			break;
		case USB_TO_XXX_CONFIG:
			if (ERROR_OK == interfaces->jtag_hl.config(device_idx, GET_LE_U16(&dat[index]), 
								dat[index + 2], dat[index + 3], 
								GET_LE_U16(&dat[index + 4]), GET_LE_U16(&dat[index + 6])))
			{
				buffer_reply[rep_len++] = USB_TO_XXX_OK;
			}
			else
			{
				buffer_reply[rep_len++] = USB_TO_XXX_FAILED;
			}
			break;
		case USB_TO_XXX_FINI:
			if (ERROR_OK == interfaces->jtag_hl.fini(device_idx))
			{
				buffer_reply[rep_len++] = USB_TO_XXX_OK;
			}
			else
			{
				buffer_reply[rep_len++] = USB_TO_XXX_FAILED;
			}
			break;
		case USB_TO_JTAG_HL_IR_DR:
			fail = false;
			rindex = rep_len++;
			len_tmp = 0;
			
			while(len_tmp < length)
			{
				i = GET_LE_U16(&dat[index + len_tmp]);					// in bit
				cur_dat_len = i & 0x7FFF;
				
				if(i & 0x8000)
				{
					if (ERROR_OK == interfaces->jtag_hl.ir(device_idx, &dat[index + len_tmp + 3], 
										cur_dat_len, dat[index + len_tmp + 2], 1))
					{
						memcpy(&buffer_reply[rep_len], &dat[index + len_tmp + 3], (cur_dat_len + 7) >> 3);
					}
					else
					{
						fail = true;
						break;
					}
				}
				else
				{
					if (ERROR_OK == interfaces->jtag_hl.dr(device_idx, &dat[index + len_tmp + 3], 
										cur_dat_len, dat[index + len_tmp + 2], 1))
					{
						memcpy(&buffer_reply[rep_len], &dat[index + len_tmp + 3], (cur_dat_len + 7) >> 3);
					}
					else
					{
						fail = true;
						break;
					}
				}
				
				cur_dat_len = (cur_dat_len + 7) >> 3;
				rep_len += cur_dat_len;
				len_tmp += cur_dat_len + 3;
			}
			if (fail)
			{
				buffer_reply[rindex] = USB_TO_XXX_FAILED;
			}
			else
			{
				buffer_reply[rindex] = USB_TO_XXX_OK;
			}
			break;
		case USB_TO_JTAG_HL_TMS:
			if (ERROR_OK == interfaces->jtag_hl.tms(device_idx, &dat[index + 1], 
														dat[index + 0] + 1))
			{
				buffer_reply[rep_len++] = USB_TO_XXX_OK;
			}
			else
			{
				buffer_reply[rep_len++] = USB_TO_XXX_FAILED;
			}
			break;
		default:
			buffer_reply[rep_len++] = USB_TO_XXX_CMD_NOT_SUPPORT;
			break;
		}
		index += length;
	}
}

#endif