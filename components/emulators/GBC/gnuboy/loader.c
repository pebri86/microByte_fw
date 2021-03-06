/*********************
*      INCLUDES
*********************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "esp_system.h"

#include "esp_timer.h"

#include "sd_storage.h"
#include "system_manager.h"
#include "esp_log.h"

#include "gnuboy.h"
#include "defs.h"
#include "regs.h"
#include "mem.h"
#include "hw.h"
#include "lcd.h"
#include "rtc.h"
#include "rc.h"
#include "sound.h"

/**********************
 *  STATIC VARIABLES
 **********************/

static int mbc_table[256] =
{
	0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3,
	3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, MBC_RUMBLE, MBC_RUMBLE, MBC_RUMBLE, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MBC_HUC3, MBC_HUC1
};

static int rtc_table[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static int batt_table[256] =
{
	0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0
};

static int romsize_table[256] =
{
	2, 4, 8, 16, 32, 64, 128, 256, 512,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 128, 128, 128
	/* 0, 0, 72, 80, 96  -- actual values but bad to use these! */
};

static int ramsize_table[256] =
{
	1, 1, 1, 4, 16,
	4 /* FIXME - what value should this be?! */
};


// Weird stuff for the emulator, I think it's useless but it's ok.
static char *romfile=NULL;
static char *sramfile=NULL;
static char *rtcfile=NULL;
static char *saveprefix=NULL;

static char *savename=NULL;
static char *savedir=NULL;

static int saveslot=0;

static int forcebatt=0, nobatt=0;
static int forcedmg=0, gbamode=0;

static int memfill = 0, memrand = -1;

static const char *TAG = "GNUBoy Loader";

/**********************
 *  STATIC PROTOTYPES 
 **********************/

static void initmem(void *mem, int size);



/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool gbc_rom_load(const char *game_name, uint8_t console){

	// Game directory in realtion with the console that we wan to use
	char rom_name[300];
	if(console == GAMEBOY) sprintf(rom_name,"/sdcard/GameBoy/%s",game_name);
	else{
		sprintf(rom_name,"/sdcard/GameBoy_Color/%s",game_name);
	}
	

	size_t game_size = sd_file_size(rom_name);

	/*It's only available 3MB of RAM, so, the games with a higher size,
	* will be save on flash memory. The disadvantage is that the load 
	* process is way slower but it's a temporary workaround */

	char * data = NULL; //Pointer to the memory region where the game will be saved.

	if(game_size > 3*1024*1024){
		ESP_LOGW(TAG,"Loading game on flash memory, this process could take several minutes.");
		data = sd_get_file_flash(rom_name);
	}
	else{
		//Allocate the size of the game.
		ESP_LOGW(TAG,"Loading game on RAM memory");
		data = malloc(game_size);
		sd_get_file(rom_name,data);
	}


	ESP_LOGI(TAG,"Initialized. ROM@%p\n", data);
	const char  * header = data;

	memcpy(rom.name, header+0x0134, 16);
	
	rom.name[16] = 0;
	ESP_LOGI(TAG,"ROM Name = '%s'\n", rom.name);

	//Get ROM data.
	int tmp = *((int*)(header + 0x0144));
	byte c = (tmp >> 24) & 0xff;
	mbc.type = mbc_table[c];
	mbc.batt = (batt_table[c] && !nobatt) || forcebatt;
	rtc.batt = rtc_table[c];

	tmp = *((int*)(header + 0x0148));
	mbc.romsize = romsize_table[(tmp & 0xff)];
	mbc.ramsize = ramsize_table[((tmp >> 8) & 0xff)];

	if (!mbc.romsize){
		ESP_LOGE(TAG,"unknown ROM size %02X\n", header[0x0148]);
		return false;
	}
	if (!mbc.ramsize){
		ESP_LOGE(TAG,"unknown SRAM size %02X\n", header[0x0149]);
		return false;
	}

	const char* mbcName;
	switch (mbc.type)
	{
		case MBC_NONE:
			mbcName = "MBC_NONE";
			break;

		case MBC_MBC1:
			mbcName = "MBC_MBC1";
			break;

		case MBC_MBC2:
			mbcName = "MBC_MBC2";
			break;

		case MBC_MBC3:
			mbcName = "MBC_MBC3";
			break;

		case MBC_MBC5:
			mbcName = "MBC_MBC5";
			break;

		case MBC_RUMBLE:
			mbcName = "MBC_RUMBLE";
			break;

		case MBC_HUC1:
			mbcName = "MBC_HUC1";
			break;

		case MBC_HUC3:
			mbcName = "MBC_HUC3";
			break;

		default:
			mbcName = "(unknown)";
			break;
	}

	uint32_t rlen = 16384 * mbc.romsize;
	uint32_t sram_length = 8192 * mbc.ramsize;

	ESP_LOGI(TAG,"ROM DATA:\nMBC type = %s\nROM Size = %d (%dK)\nRAM size = %d (%dK)", mbcName, mbc.romsize, rlen / 1024, mbc.ramsize, sram_length / 1024);

	// ROM
	rom.bank = (byte *)data;
	rom.length = rlen;

	// SRAM
	ram.sram_dirty = 1;
	ram.sbank = malloc(sram_length); //Allocate the required SRAM
	if (!ram.sbank){
		if (rlen <= (0x100000 * 3) && sram_length <= 0x100000){
			ram.sbank = data + (0x100000 * 3);
			ESP_LOGW(TAG,"Error allocating the required SRAM, triying to force allocation on PSRAM.");
		}
		else{
			ESP_LOGE(TAG,"SRAM allocation fail.");
			return false;
		}
	}

	//Initialize memory
	initmem(ram.sbank, 8192 * mbc.ramsize);
	initmem(ram.ibank, 4096 * 8);

	mbc.rombank = 1;
	mbc.rambank = 0;

	tmp = *((int*)(header + 0x0140));
	c = tmp >> 24;
	hw.cgb = ((c == 0x80) || (c == 0xc0)) && !forcedmg;
	hw.gba = (hw.cgb && gbamode);
	//mem_updatemap();
	return true;

}

// TODO: Implement SRAM save and load
int gbc_sram_load()
{
	if (!mbc.batt) return -1;

	/* Consider sram loaded at this point, even if file doesn't exist */
	ram.loaded = 1;

	/*
	const esp_partition_t* part;
	spi_flash_mmap_handle_t hrom;
	esp_err_t err;

	part=esp_partition_find_first(0x40, 2, NULL);
	if (part==0)
	{
		printf("esp_partition_find_first (save) failed.\n");
		//abort();
	}
	else
	{
		err = esp_partition_read(part, 0, ram.sbank, mbc.ramsize * 8192);
		if (err != ESP_OK)
		{
			printf("esp_partition_read failed. (%d)\n", err);
		}
		else
		{
			printf("sram_load: sram load OK.\n");
			ram.sram_dirty = 0;
		}
	}*/

	return 0;
}


int gbc_sram_save()
{
	/* If we crash before we ever loaded sram, DO NOT SAVE! */
	if (!mbc.batt || !ram.loaded || !mbc.ramsize)
		return -1;

	/*const esp_partition_t* part;
	spi_flash_mmap_handle_t hrom;
	esp_err_t err;

	part=esp_partition_find_first(0x40, 2, NULL);
	if (part==0)
	{
		printf("esp_partition_find_first (save) failed.\n");
		//abort();
	}
	else
	{
		err = esp_partition_erase_range(part, 0, mbc.ramsize * 8192);
		if (err!=ESP_OK)
		{
			printf("esp_partition_erase_range failed. (%d)\n", err);
			abort();
		}

		err = esp_partition_write(part, 0, ram.sbank, mbc.ramsize * 8192);
		if (err != ESP_OK)
		{
			printf("esp_partition_write failed. (%d)\n", err);
		}
		else
		{
				printf("sram_load: sram save OK.\n");
		}
	}*/

	return 0;
}


bool gbc_state_save(const char *game_name, uint8_t console){
	char rom_name[300];
	if(console == GAMEBOY) sprintf(rom_name,"/sdcard/GameBoy/Save_Data/%s.sav",game_name);
	else{
		sprintf(rom_name,"/sdcard/GameBoy_Color/Save_Data/%s.sav",game_name);
	}
	
	FILE *f = fopen(rom_name, "w");
	
	if (f != NULL){
		savestate(f);
		fclose(f);
		ESP_LOGI(TAG,"%s SAVE.",game_name);
		return true;
	}
	else{
		ESP_LOGE(TAG,"Fail to create game save file.");
		return false;
	}
}


bool gbc_state_load(const char *game_name, uint8_t console){
	char rom_name[300];
	if(console == GAMEBOY) sprintf(rom_name,"/sdcard/GameBoy/Save_Data/%s.sav",game_name);
	else{
		sprintf(rom_name,"/sdcard/GameBoy_Color/Save_Data/%s.sav",game_name);
	}
	
	FILE *f = fopen(rom_name, "r");

	if (f != NULL){
		loadstate(f);
		fclose(f);
		vram_dirty();
		pal_dirty();
		sound_dirty();
		mem_updatemap();
		ESP_LOGI(TAG,"%s LOAD.",game_name);
		return true;
	}
	else{
		ESP_LOGE(TAG,"Fail to load save data.");
		return false;
	}

}

void rtc_save()
{
	FILE *f;
	if (!rtc.batt) return;
	if (!(f = fopen(rtcfile, "wb"))) return;
	rtc_save_internal(f);
	fclose(f);
}

void rtc_load()
{
	//FILE *f;
	if (!rtc.batt) return;
	//if (!(f = fopen(rtcfile, "r"))) return;
	//rtc_load_internal(f);
	//fclose(f);
}


void loader_unload()
{
	// TODO: unmap flash
//	sram_save();
	/*if (romfile){
		printf("romfiles\r\n");
		free(romfile);
	}
	if (sramfile){
		printf("ramfiles\r\n");
		free(sramfile);
	}*/
	//if (saveprefix) free(saveprefix);
	mem_updatemap();
	if (rom.bank){
		printf("Free ROM\r\n");
		//heap_caps_free(*rom.bank);
		
	}
	if (ram.sbank){
		printf("Free RAM\r\n");
		//free(ram.sbank);
		//free(ram.ibank);
	}
	free(ram.sbank);
	//free(rom.bank);
	romfile = sramfile = saveprefix = 0;
	rom.bank = 0;
	ram.sbank = 0;
	//mbc.type = mbc.romsize = mbc.ramsize = mbc.batt = 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void initmem(void *mem, int size){
	char *p = mem;
	//memset(p, 0xff /*memfill*/, size);
	if (memfill >= 0)
		memset(p, memfill, size);
}

/* basename/dirname like function */
static char *base(char *s)
{
	char *p;
	p = (char *) strrchr((unsigned char)s, DIRSEP_CHAR);
	if (p) return p+1;
	return s;
}

static char *ldup(char *s)
{
	int i;
	char *n, *p;
	p = n = malloc(strlen(s));
	for (i = 0; s[i]; i++) if (isalnum((unsigned char)s[i])) *(p++) = tolower((unsigned char)s[i]);
	*p = 0;
	return n;
}

static void cleanup()
{
	gbc_sram_save();
	rtc_save();
	/* IDEA - if error, write emergency savestate..? */
}


rcvar_t loader_exports[] =
{
	RCV_STRING("savedir", &savedir),
	RCV_STRING("savename", &savename),
	RCV_INT("saveslot", &saveslot),
	RCV_BOOL("forcebatt", &forcebatt),
	RCV_BOOL("nobatt", &nobatt),
	RCV_BOOL("forcedmg", &forcedmg),
	RCV_BOOL("gbamode", &gbamode),
	RCV_INT("memfill", &memfill),
	RCV_INT("memrand", &memrand),
	RCV_END
};
