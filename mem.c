#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mem.h"
#include "rom.h"
#include "lcd.h"
#include "mbc.h"
#include "interrupt.h"
#include "timer.h"
#include "sdl.h"
#include "cpu.h"

static unsigned const char bootrom[256] =
{
    0x31, 0xFE, 0xFF, 0xAF, 0x21, 0xFF, 0x9F, 0x32, 0xCB, 0x7C, 0x20, 0xFB, 0x21, 0x26, 0xFF, 0x0E,
    0x11, 0x3E, 0x80, 0x32, 0xE2, 0x0C, 0x3E, 0xF3, 0xE2, 0x32, 0x3E, 0x77, 0x77, 0x3E, 0xFC, 0xE0,
    0x47, 0x11, 0x04, 0x01, 0x21, 0x10, 0x80, 0x1A, 0xCD, 0x95, 0x00, 0xCD, 0x96, 0x00, 0x13, 0x7B,
    0xFE, 0x34, 0x20, 0xF3, 0x11, 0xD8, 0x00, 0x06, 0x08, 0x1A, 0x13, 0x22, 0x23, 0x05, 0x20, 0xF9,
    0x3E, 0x19, 0xEA, 0x10, 0x99, 0x21, 0x2F, 0x99, 0x0E, 0x0C, 0x3D, 0x28, 0x08, 0x32, 0x0D, 0x20,
    0xF9, 0x2E, 0x0F, 0x18, 0xF3, 0x67, 0x3E, 0x64, 0x57, 0xE0, 0x42, 0x3E, 0x91, 0xE0, 0x40, 0x04,
    0x1E, 0x02, 0x0E, 0x0C, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x0D, 0x20, 0xF7, 0x1D, 0x20, 0xF2,
    0x0E, 0x13, 0x24, 0x7C, 0x1E, 0x83, 0xFE, 0x62, 0x28, 0x06, 0x1E, 0xC1, 0xFE, 0x64, 0x20, 0x06,
    0x7B, 0xE2, 0x0C, 0x3E, 0x87, 0xE2, 0xF0, 0x42, 0x90, 0xE0, 0x42, 0x15, 0x20, 0xD2, 0x05, 0x20,
    0x4F, 0x16, 0x20, 0x18, 0xCB, 0x4F, 0x06, 0x04, 0xC5, 0xCB, 0x11, 0x17, 0xC1, 0xCB, 0x11, 0x17,
    0x05, 0x20, 0xF5, 0x22, 0x23, 0x22, 0x23, 0xC9, 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E, 0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C,
    0x21, 0x04, 0x01, 0x11, 0xA8, 0x00, 0x1A, 0x13, 0xBE, 0x20, 0xFE, 0x23, 0x7D, 0xFE, 0x34, 0x20,
    0xF5, 0x06, 0x19, 0x78, 0x86, 0x23, 0x05, 0x20, 0xFB, 0x86, 0x20, 0xFE, 0x3E, 0x01, 0xE0, 0x50
};

#ifdef DEBUG
static int bootrom_enabled = 0;
#else
static int bootrom_enabled = 1;
#endif

static unsigned char *mem;

static unsigned int DMA_pending;
static unsigned short DMA_src;
static int DMA_copied;

static int joypad_select_buttons, joypad_select_directions;

void mem_bank_switch(unsigned int n)
{
	unsigned char *b = rom_getbytes();

	if(!rom_bank_valid(n))
	{
		printf("Bank switch to illegal bank %d, ignoring.\n", n);
		return;
	}

	memcpy(&mem[0x4000], &b[n * 0x4000], 0x4000);
}

/* LCD's access to VRAM */
unsigned char mem_get_raw(unsigned short p)
{
	return mem[p];
}

unsigned char mem_get_byte(unsigned short i)
{
	unsigned char mask = 0;

	if(i < 0x100 && bootrom_enabled)
		return bootrom[i];

	if(DMA_pending && cpu_get_cycles() >= DMA_pending)
	{
		unsigned long elapsed;

		if(!DMA_copied)
		{
			memcpy(&mem[0xFE00], &mem[DMA_src], 0xA0);
			DMA_copied = 1;
		}

		elapsed = cpu_get_cycles() - DMA_pending;

		if(elapsed >= 160)
			DMA_pending = 0;
		else if(i < 0xFF00)
		{
			/* Reading from OAM during DMA always gives 0xFF */
			if(i >= 0xFE00 && i <= 0xFEA0)
				return 0xFF;

			/* !A ^ B checks if A and B are both 0 or both 1 */
			if(!(((DMA_src >> 13) == 4) ^ ((i >> 13) == 4)))
				return mem[0xFE00+elapsed];
		}
	}

	switch(i)
	{
		case 0xFF00:	/* Joypad */
			if(!joypad_select_buttons)
				mask = sdl_get_buttons();
			if(!joypad_select_directions)
				mask = sdl_get_directions();
			return 0xC0 | (0xF^mask) | (joypad_select_buttons | joypad_select_directions);
		break;
		case 0xFF04:
			return timer_get_div();
		break;
		case 0xFF05:
			return timer_get_counter();
		break;
		case 0xFF06:
			return timer_get_modulo();
		break;
		case 0xFF07:
			return timer_get_tac();
		break;
		case 0xFF0F:
			return interrupt_get_IF();
		break;
		case 0xFF41:
			return lcd_get_stat();
		break;
		case 0xFF44:
			return lcd_get_line();
		break;
		case 0xFF45:
			return lcd_get_ly_compare();
		break;
		case 0xFF4D:	/* GBC speed switch */
			return 0xFF;
		break;
		case 0xFFFF:
			return interrupt_get_mask();
		break;
	}

	if(i >= 0xE000 && i <= 0xFDFF)
		i -= 0x2000;

	if(i >= 0x8000 && i <= 0x9FFF && (lcd_get_stat() & 3) == 3)
		return 0xFF;

	return mem[i];
}

unsigned short mem_get_word(unsigned short i)
{
	return mem_get_byte(i) | (mem_get_byte(i+1)<<8);
}

void mem_write_byte(unsigned short d, unsigned char i)
{
	unsigned int filtered = 0;

	if(DMA_pending && DMA_pending <= cpu_get_cycles())
	{
		long elapsed;

		if(!DMA_copied)
		{
			memcpy(&mem[0xFE00], &mem[DMA_src], 0xA0);
			DMA_copied = 1;
		}

		elapsed = cpu_get_cycles() - DMA_pending;

		if(elapsed >= 160)
			DMA_pending = 0;
		else if(d >= 0xFE00 && d <= 0xFEA0)
			return;

		else if(!(((DMA_src >> 13) == 4) ^ ((d >> 13) == 4)))
			return;
	}

	switch(rom_get_mapper())
	{
		case NROM:
			if(d < 0x8000)
				filtered = 1;
		break;
		case MBC2:
		case MBC3:
		case MBC5:
			filtered = MBC3_write_byte(d, i);
		break;
		case MBC1:
			filtered = MBC1_write_byte(d, i);
		break;
	}

	if(filtered)
		return;

	switch(d)
	{
		case 0xFF00:	/* Joypad */
			joypad_select_buttons = i&0x20;
			joypad_select_directions = i&0x10;
		break;
		case 0xFF01: /* Link port data */
		break;
		case 0xFF04:
			timer_set_div(i);
		break;
		case 0xFF05:
			timer_set_counter(i);
		break;
		case 0xFF06:
			timer_set_modulo(i);
		break;
		case 0xFF07:
			timer_set_tac(i);
		break;
		case 0xFF0F:
			interrupt_set_IF(i);
		break;
		case 0xFF40:
			lcd_write_control(i);
		break;
		case 0xFF41:
			lcd_write_stat(i);
		break;
		case 0xFF42:
			lcd_write_scroll_y(i);
		break;
		case 0xFF43:
			lcd_write_scroll_x(i);
		break;
		case 0xFF45:
			lcd_set_ly_compare(i);
		break;
		case 0xFF46: /* OAM DMA */
			/* This is the dead cycle where OAM DMA is busy, can't restart */
			if(DMA_pending == cpu_get_cycles())
				break;

			/* Start or restart OAM DMA */
			DMA_pending = cpu_get_cycles()+2;
			DMA_src = i * 0x100;
			DMA_copied = 0;
		break;
		case 0xFF47:
			lcd_write_bg_palette(i);
		break;
		case 0xFF48:
			lcd_write_spr_palette1(i);
		break;
		case 0xFF49:
			lcd_write_spr_palette2(i);
		break;
		case 0xFF4A:
			lcd_set_window_y(i);
		break;
		case 0xFF4B:
			lcd_set_window_x(i);
		break;
		case 0xFF50:
			bootrom_enabled = 0;
		break;
		case 0xFFFF:
			interrupt_set_mask(i);
			return;
		break;
	}

#ifndef DEBUG
	if(d > 0x8000 && d < 0x9FFF && (lcd_get_stat() & 3) == 3)
		return;
#endif

	/* RAM mirror */
	if(d >= 0xE000 && d <= 0xFDFF)
		d -= 0x2000;

	mem[d] = i;
}

void mem_write_word(unsigned short d, unsigned short i)
{
	mem_write_byte(d, i&0xFF);
	mem_write_byte(d+1, i>>8);
}

void mem_init(void)
{
	unsigned char *bytes = rom_getbytes();

	mem = calloc(1, 0x10000);

	memcpy(&mem[0x0000], &bytes[0x0000], 0x4000);
	memcpy(&mem[0x4000], &bytes[0x4000], 0x4000);

	mem[0xFF02] = 0x7E;
	mem[0xFF10] = 0x80;
	mem[0xFF11] = 0xBF;
	mem[0xFF12] = 0xF3;
	mem[0xFF14] = 0xBF;
	mem[0xFF16] = 0x3F;
	mem[0xFF19] = 0xBF;
	mem[0xFF1A] = 0x7F;
	mem[0xFF1B] = 0xFF;
	mem[0xFF1C] = 0x9F;
	mem[0xFF1E] = 0xBF;
	mem[0xFF20] = 0xFF;
	mem[0xFF23] = 0xBF;
	mem[0xFF24] = 0x77;
	mem[0xFF25] = 0xF3;
	mem[0xFF26] = 0xF1;
	mem[0xFF40] = 0x91;
	mem[0xFF47] = 0xFC;
	mem[0xFF48] = 0xFF;
	mem[0xFF49] = 0xFF;
}
