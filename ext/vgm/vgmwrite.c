/*
    vgmwrite.c

    VGM output module

    Written by Valley Bell

*/

#include "vgmwrite.h"

#define DYNAMIC_HEADER_SIZE

typedef uint16_t gd3char_t;	// UTF-16 character, for VGM GD3 tag

static uint8_t LOG_VGM_FILE = 0x00;

typedef struct _vgm_file_header VGM_HEADER;
struct _vgm_file_header
{
	uint32_t fccVGM;
	uint32_t lngEOFOffset;
	uint32_t lngVersion;
	uint32_t lngHzPSG;
	uint32_t lngHz2413;
	uint32_t lngGD3Offset;
	uint32_t lngTotalSamples;
	uint32_t lngLoopOffset;
	uint32_t lngLoopSamples;
	uint32_t lngRate;
	uint16_t shtPSG_Feedback;
	uint8_t bytPSG_SRWidth;
	uint8_t bytPSG_Flags;
	uint32_t lngHz2612;
	uint32_t lngHz2151;
	uint32_t lngDataOffset;
	uint32_t lngHzSPCM;
	uint32_t lngSPCMIntf;
	
	uint32_t lngHzRF5C68;
	uint32_t lngHz2203;
	uint32_t lngHz2608;
	uint32_t lngHz2610;
	uint32_t lngHz3812;
	uint32_t lngHz3526;
	uint32_t lngHz8950;
	uint32_t lngHz262;
	uint32_t lngHz278B;
	uint32_t lngHz271;
	uint32_t lngHz280B;
	uint32_t lngHzRF5C164;
	uint32_t lngHzPWM;
	uint32_t lngHzAY8910;
	uint8_t lngAYType;
	uint8_t lngAYFlags;
	uint8_t lngAYFlagsYM2203;
	uint8_t lngAYFlagsYM2608;
	uint8_t bytModifiers[0x04];
	
	uint32_t lngHzGBDMG;		// part of the LR35902 (GB Main CPU)
	uint32_t lngHzNESAPU;		// part of the N2A03 (NES Main CPU)
	uint32_t lngHzMultiPCM;
	uint32_t lngHzUPD7759;
	uint32_t lngHzOKIM6258;
	uint8_t bytOKI6258Flags;
	uint8_t bytK054539Flags;
	uint8_t bytC140Type;
	uint8_t bytReservedFlags;
	uint32_t lngHzOKIM6295;
	uint32_t lngHzK051649;
	uint32_t lngHzK054539;
	uint32_t lngHzHuC6280;
	uint32_t lngHzC140;
	uint32_t lngHzK053260;
	uint32_t lngHzPokey;
	uint32_t lngHzQSound;
	uint32_t lngHzSCSP;
	uint32_t lngExtraOfs;
	
	uint32_t lngHzWSwan;
	uint32_t lngHzVSU;
	uint32_t lngHzSAA1099;
	uint32_t lngHzES5503;
	uint32_t lngHzES5506;
	uint8_t bytES5503Chns;
	uint8_t bytES5506Chns;
	uint8_t bytC352ClkDiv;
	uint8_t bytESReserved;
	uint32_t lngHzX1_010;
	uint32_t lngHzC352;
	
	uint32_t lngHzGA20;
	uint8_t bytReserved[0x1C];
};	// -> 0x100 Bytes
typedef struct _vgm_gd3_tag GD3_TAG;
struct _vgm_gd3_tag
{
	uint32_t fccGD3;
	uint32_t lngVersion;
	uint32_t lngTagLength;
	gd3char_t strTrackNameE[0x70];
	gd3char_t strTrackNameJ[0x10];	// Japanese Names are not used
	gd3char_t strGameNameE[0x70];
	gd3char_t strGameNameJ[0x10];
	gd3char_t strSystemNameE[0x30];
	gd3char_t strSystemNameJ[0x10];
	gd3char_t strAuthorNameE[0x30];
	gd3char_t strAuthorNameJ[0x10];
	gd3char_t strReleaseDate[0x10];
	gd3char_t strCreator[0x20];
	gd3char_t strNotes[0x50];
};	// -> 0x200 Bytes

typedef struct _vgm_rom_data_block VGM_ROM_DATA;
struct _vgm_rom_data_block
{
	uint8_t Type;
	uint8_t dstart_msb;
	uint32_t DataSize;
	const void* Data;
};
typedef struct _vgm_rom_init_command VGM_INIT_CMD;
struct _vgm_rom_init_command
{
	uint8_t CmdLen;
	uint8_t Data[0x08];
};
typedef struct _vgm_file_inf VGM_INF;
struct _vgm_file_inf
{
	FILE* hFile;
	VGM_HEADER Header;
	uint8_t WroteHeader;
	uint32_t HeaderBytes;
	uint32_t BytesWrt;
	uint32_t SmplsWrt;
	uint32_t EvtDelay;
	
	uint32_t DataCount;
	VGM_ROM_DATA DataBlk[0x20];
	//uint32_t CmdAlloc;
	uint32_t CmdCount;
	VGM_INIT_CMD Commands[0x100];
	
	uint8_t NesMemEmpty;
	uint8_t NesMem[0x4000];
};
typedef struct _vgm_chip VGM_CHIP;
typedef struct _vgm_chip_pcmcache VGM_PCMCACHE;
struct _vgm_chip
{
	uint16_t VgmID;
	uint8_t ChipType;
	uint8_t HadWrite;
	VGM_PCMCACHE* PCMCache;
};
struct _vgm_chip_pcmcache
{
	uint32_t Start;
	uint32_t Next;
	uint32_t Pos;
	uint32_t CacheSize;
	uint8_t* CacheData;
};


#define MAX_VGM_FILES	0x10
#define MAX_VGM_CHIPS	0x80
static char vgm_namebase[0x80];
static VGM_INF VgmFile[MAX_VGM_FILES];
static VGM_CHIP VgmChip[MAX_VGM_CHIPS];
static VGM_PCMCACHE VgmPCache[MAX_VGM_CHIPS];
static GD3_TAG VgmTag;

// Function Prototypes
static void vgmfile_callback(int param);
static size_t str2utf16(gd3char_t* dststr, const char* srcstr, size_t max);
static size_t wcs2utf16(gd3char_t* dststr, const wchar_t* srcstr, size_t max);
static size_t utf16len(const gd3char_t* str);
static void vgm_header_postwrite(uint16_t vgm_id);
static void vgm_header_sizecheck(uint16_t vgm_id, uint32_t MinVer, uint32_t MinSize);
static void vgm_header_clear(uint16_t vgm_id);
static void vgm_setup_pcmcache(VGM_PCMCACHE* TempPC, uint32_t Size);
static void vgm_close(uint16_t vgm_id);
static void vgm_write_delay(uint16_t vgm_id);
static uint8_t vgm_nes_ram_check(VGM_INF* VI, uint32_t datasize, uint32_t* value1, uint32_t* value2, const uint8_t* data);
static void vgm_flush_pcm(VGM_CHIP* VC);


static size_t str2utf16(gd3char_t* dststr, const char* srcstr, size_t max)
{
	size_t srclen;
	size_t wlen;
	wchar_t* wstr;
	
	srclen = strlen(srcstr);
	wstr = (wchar_t*)calloc(srclen, sizeof(wchar_t));
	
	mbstowcs(wstr, srcstr, srclen + 1);
	wlen = wcs2utf16(dststr, wstr, max);
	
	free(wstr);
	return wlen;
}

static size_t wcs2utf16(gd3char_t* dststr, const wchar_t* srcstr, size_t max)
{
	size_t wlen;
	
	max --;
	wlen = wcslen(srcstr);
	if (wlen > max)
		wlen = max;
	
	if (sizeof(gd3char_t) == sizeof(wchar_t))
	{
		wcsncpy((wchar_t*)dststr, srcstr, wlen + 1);
		return wlen;
	}
	else
	{
		size_t idxSrc;
		size_t idxDst;
		
		// return number of copied bytes
		idxDst = 0;
		for (idxSrc = 0; idxSrc < wlen && idxDst < max; idxSrc ++)
		{
			if (srcstr[idxSrc] >= 0x10000)
			{
				gd3char_t X, W, HiSurrogate, LoSurrogate;
#define HI_SURROGATE_START	0xD800
#define LO_SURROGATE_START	0xDC00
				// surrogate conversion from http://unicode.org/faq/utf_bom.html
				
				X = (gd3char_t)srcstr[idxSrc];
				W = ((srcstr[idxSrc] >> 16) & 0x1F) - 1;
				HiSurrogate = HI_SURROGATE_START | (W << 6) | (X >> 10);
				LoSurrogate = LO_SURROGATE_START | (X & 0x3FF);
				
				dststr[idxDst] = HiSurrogate;	idxDst ++;
				dststr[idxDst] = LoSurrogate;	idxDst ++;
			}
			else
			{
				// just truncate/copy
				dststr[idxDst] = (gd3char_t)srcstr[idxSrc];	idxDst ++;
			}
		}
		dststr[idxDst] = (gd3char_t)L'\0';
		return idxDst;
	}
}

static size_t utf16len(const gd3char_t* str)
{
	const gd3char_t* end_ptr = str;
	
	while(*end_ptr != 0)
		end_ptr ++;
	return end_ptr - str;
}

void vgm_start(struct RunningMachine *machine)
{
	uint16_t curvgm;
	const struct GameDriver* gamedrv;
#ifdef MESS
	device_image_interface* devimg;
#endif
	wchar_t vgmNotes[0x50];
	
	LOG_VGM_FILE = pmoptions.vgmwrite;
	logerror("VGM logging mode: %02X\n", LOG_VGM_FILE);
	
	// Reset all files
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		VgmFile[curvgm].hFile = NULL;
		VgmFile[curvgm].DataCount = 0x00;
		VgmFile[curvgm].CmdCount = 0x00;
		VgmFile[curvgm].NesMemEmpty = 0x01;
	}
	for (curvgm = 0x00; curvgm < MAX_VGM_CHIPS; curvgm ++)
	{
		VgmChip[curvgm].ChipType = 0xFF;
		VgmPCache[curvgm].CacheSize = 0x00;
		VgmPCache[curvgm].CacheData = NULL;
	}
	
	// start the timer
	// (done here because it makes save states with vgmwrite off compatible with
	//  saves that have it on)
	timer_pulse(TIME_IN_HZ(44100), 0, vgmfile_callback);
			//44.1 KHz VGM pulse timer
	
	if (! LOG_VGM_FILE)
		return;
	
	// Get the Game Information and write the GD3 Tag
	gamedrv = machine->gamedrv;
#ifdef MESS
	devimg = image_interface_iterator(machine->root_device()).first();
#endif

	if (gamedrv)
		strcpy(vgm_namebase, gamedrv->name);
	else
		strcpy(vgm_namebase, "vgmlog");
	strcat(vgm_namebase, "_");
	
	VgmTag.fccGD3 = 0x20336447;	// 'Gd3 '
	VgmTag.lngVersion = 0x00000100;
	wcs2utf16(VgmTag.strTrackNameE, L"", 0x70);
	wcs2utf16(VgmTag.strTrackNameJ, L"", 0x10);
	wcs2utf16(VgmTag.strGameNameE, L"", 0x70);
	wcs2utf16(VgmTag.strGameNameJ, L"", 0x10);
	wcs2utf16(VgmTag.strSystemNameE, L"", 0x30);
	wcs2utf16(VgmTag.strSystemNameJ, L"", 0x10);
	wcs2utf16(VgmTag.strAuthorNameE, L"", 0x30);
	wcs2utf16(VgmTag.strAuthorNameJ, L"", 0x10);
	wcs2utf16(VgmTag.strReleaseDate, L"", 0x10);
	wcs2utf16(VgmTag.strCreator, L"", 0x20);
	wcs2utf16(VgmTag.strNotes, L"", 0x50);
	
#ifdef MESS
	if (gamedrv->flags & MACHINE_TYPE_ARCADE)
	{
#endif
		wcs2utf16(VgmTag.strSystemNameE, L"Arcade Machine", 0x30);
		str2utf16(VgmTag.strGameNameE, gamedrv->description, 0x70);
		str2utf16(VgmTag.strAuthorNameE, gamedrv->manufacturer, 0x30);
		str2utf16(VgmTag.strReleaseDate, gamedrv->year, 0x10);
#ifdef MESS
	}
	else
	{
		str2utf16(VgmTag.strSystemNameE, gamedrv->type.fullname(), 0x30);
		if (devimg != NULL && devimg->exists())
		{
			if (devimg->longname().length() > 0)
				str2utf16(VgmTag.strGameNameE, devimg->longname().c_str(), 0x70);
			else
				str2utf16(VgmTag.strGameNameE, devimg->basename_noext(), 0x70);
			str2utf16(VgmTag.strAuthorNameE, devimg->manufacturer().c_str(), 0x30);
			str2utf16(VgmTag.strReleaseDate, devimg->year().c_str(), 0x10);
		}
	}
#endif
	
	swprintf(vgmNotes, sizeof(vgmNotes), L"Generated by %hs %hs",
			APPNAME, build_version);
	wcs2utf16(VgmTag.strNotes, vgmNotes, 0x50);
	
	VgmTag.lngTagLength = utf16len(VgmTag.strTrackNameE) + 1 +
				utf16len(VgmTag.strTrackNameJ) + 1 +
				utf16len(VgmTag.strGameNameE) + 1 +
				utf16len(VgmTag.strGameNameJ) + 1 +
				utf16len(VgmTag.strSystemNameE) + 1 +
				utf16len(VgmTag.strSystemNameJ) + 1 +
				utf16len(VgmTag.strAuthorNameE) + 1 +
				utf16len(VgmTag.strAuthorNameJ) + 1 +
				utf16len(VgmTag.strReleaseDate) + 1 +
				utf16len(VgmTag.strCreator) + 1 +
				utf16len(VgmTag.strNotes) + 1;
	VgmTag.lngTagLength *= sizeof(gd3char_t);	// String Length -> Byte Length
	
	logerror("VGM logging started ...\n");
	
	return;
}

void vgm_stop(void)
{
	uint16_t curchip;
	uint16_t chip_unused;
	uint16_t curvgm;
	uint32_t clock_mask;
	VGM_HEADER* VH;
	VGM_CHIP* VC;
	
	if (! LOG_VGM_FILE)
		return;
	
	chip_unused = 0x00;
	for (curchip = 0x00; curchip < MAX_VGM_CHIPS; curchip ++)
	{
		VC = &VgmChip[curchip];
		if (VC->ChipType == 0xFF)
			break;
		
		if (! VC->HadWrite)
		{
			chip_unused ++;
			curvgm = VC->VgmID;
			VH = &VgmFile[curvgm].Header;
			// clock_mask - remove either the dual-chip bit or the entire clock
			clock_mask = (VC->ChipType & 0x80) ? ~0x40000000 : 0x00000000;
			
			switch(VC->ChipType & 0x7F)
			{
			case VGMC_SN76496:
				VH->lngHzPSG &= clock_mask;
				if (! clock_mask)
				{
					VH->shtPSG_Feedback = 0x0000;
					VH->bytPSG_SRWidth = 0x00;
					VH->bytPSG_Flags = 0x00;
				}
				break;
			case VGMC_YM2413:
				VH->lngHz2413 &= clock_mask;
				break;
			case VGMC_YM2612:
				VH->lngHz2612 &= clock_mask;
				break;
			case VGMC_YM2151:
				VH->lngHz2151 &= clock_mask;
				break;
			case VGMC_SEGAPCM:
				VH->lngHzSPCM &= clock_mask;
				break;
			case VGMC_RF5C68:
				VH->lngHzRF5C68 &= clock_mask;
				break;
			case VGMC_YM2203:
				VH->lngHz2203 &= clock_mask;
				if (! clock_mask)
					VH->lngAYFlagsYM2203 = 0x00;
				break;
			case VGMC_YM2608:
				VH->lngHz2608 &= clock_mask;
				if (! clock_mask)
					VH->lngAYFlagsYM2608 = 0x00;
				break;
			case VGMC_YM2610:
				VH->lngHz2610 &= clock_mask;
				break;
			case VGMC_YM3812:
				VH->lngHz3812 &= clock_mask;
				break;
			case VGMC_YM3526:
				VH->lngHz3526 &= clock_mask;
				break;
			case VGMC_Y8950:
				VH->lngHz8950 &= clock_mask;
				break;
			case VGMC_YMF262:
				VH->lngHz262 &= clock_mask;
				break;
			case VGMC_YMF278B:
				VH->lngHz278B &= clock_mask;
				break;
			case VGMC_YMF271:
				VH->lngHz271 &= clock_mask;
				break;
			case VGMC_YMZ280B:
				VH->lngHz280B &= clock_mask;
				break;
			case VGMC_T6W28:
				clock_mask = 0x00000000;
				VH->lngHzPSG &= clock_mask;
				if (! clock_mask)
				{
					VH->shtPSG_Feedback = 0x0000;
					VH->bytPSG_SRWidth = 0x00;
					VH->bytPSG_Flags = 0x00;
				}
				break;
			case VGMC_RF5C164:
				VH->lngHzRF5C164 &= clock_mask;
				break;
			case VGMC_PWM:
				VH->lngHzPWM &= clock_mask;
				break;
			case VGMC_AY8910:
				VH->lngHzAY8910 &= clock_mask;
				if (! clock_mask)
				{
					VH->lngAYFlags = 0x00;
					VH->lngAYType = 0x00;
				}
				break;
			case VGMC_GBSOUND:
				VH->lngHzGBDMG &= clock_mask;
				break;
			case VGMC_NESAPU:
				VH->lngHzNESAPU &= clock_mask;
				break;
			case VGMC_MULTIPCM:
				VH->lngHzMultiPCM &= clock_mask;
				break;
			case VGMC_UPD7759:
				VH->lngHzUPD7759 &= clock_mask;
				break;
			case VGMC_OKIM6258:
				VH->lngHzOKIM6258 &= clock_mask;
				if (! clock_mask)
					VH->bytOKI6258Flags = 0x00;
				break;
			case VGMC_OKIM6295:
				VH->lngHzOKIM6295 &= clock_mask;
				break;
			case VGMC_K051649:
				VH->lngHzK051649 &= clock_mask;
				break;
			case VGMC_K054539:
				VH->lngHzK054539 &= clock_mask;
				if (! clock_mask)
					VH->bytK054539Flags = 0x00;
				break;
			case VGMC_C6280:
				VH->lngHzHuC6280 &= clock_mask;
				break;
			case VGMC_C140:
				VH->lngHzC140 &= clock_mask;
				if (! clock_mask)
					VH->bytC140Type = 0x00;
				break;
			case VGMC_K053260:
				VH->lngHzK053260 &= clock_mask;
				break;
			case VGMC_POKEY:
				VH->lngHzPokey &= clock_mask;
				break;
			case VGMC_QSOUND:
				VH->lngHzQSound &= clock_mask;
				break;
			case VGMC_SCSP:
				VH->lngHzSCSP &= clock_mask;
				break;
			case VGMC_WSWAN:
				VH->lngHzWSwan &= clock_mask;
				break;
			case VGMC_VSU:
				VH->lngHzVSU &= clock_mask;
				break;
			case VGMC_SAA1099:
				VH->lngHzSAA1099 &= clock_mask;
				break;
			case VGMC_ES5503:
				VH->lngHzES5503 &= clock_mask;
				if (! clock_mask)
					VH->bytES5503Chns = 0x00;
				break;
			case VGMC_ES5506:
				VH->lngHzES5506 &= clock_mask;
				if (! clock_mask)
					VH->bytES5506Chns = 0x00;
				break;
			case VGMC_X1_010:
				VH->lngHzX1_010 &= clock_mask;
				break;
			case VGMC_C352:
				VH->lngHzC352 &= clock_mask;
				if (! clock_mask)
					VH->bytC352ClkDiv = 0x00;
				break;
			case VGMC_GA20:
				VH->lngHzGA20 &= clock_mask;
				break;
		//	case VGMC_OKIM6376:
		//		VH->lngHzOKIM6376 &= clock_mask;
		//		break;
			}
		}
		if (VC->PCMCache != NULL)
		{
			VC->PCMCache->CacheSize = 0x00;
			free(VC->PCMCache->CacheData);
			VC->PCMCache->CacheData = NULL;
			VC->PCMCache = NULL;
		}
	}
	if (chip_unused)
		logerror("Header Data of %hu unused Chips removed.\n", chip_unused);
	
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].hFile != NULL)
			vgm_close(curvgm);
	}
	logerror("VGM stopped.\n");
	
	return;
}

static void vgmfile_callback(int param)
{
	uint16_t curvgm;
	
	if (! LOG_VGM_FILE)
		return;
	
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].hFile != NULL)
			VgmFile[curvgm].EvtDelay ++;
	}
	
	return;
}

static void vgm_header_postwrite(uint16_t vgm_id)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	VGM_ROM_DATA* VR;
	VGM_INIT_CMD* VC;
	uint32_t curcmd;
	uint32_t blocksize;
	uint32_t templng;
	
	if (VgmFile[vgm_id].WroteHeader)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	
	logerror("Writing VGM Header ...\n");
	fseek(VI->hFile, 0x00, SEEK_SET);
	VI->BytesWrt = 0x00;
	
	fwrite(Header, 0x01, VI->HeaderBytes, VI->hFile);
	VI->BytesWrt += VI->HeaderBytes;
	
	for (curcmd = 0x00; curcmd < VI->DataCount; curcmd ++)
	{
		VR = &VI->DataBlk[curcmd];
		blocksize = 0x08;
		if (VR->Data != NULL)
			blocksize += VR->DataSize;
		VR->DataSize &= 0x7FFFFFFF;
		
		fputc(0x67, VI->hFile);
		fputc(0x66, VI->hFile);
		fputc(VR->Type, VI->hFile);
		fwrite(&blocksize, 0x04, 0x01, VI->hFile);		// Data Block Size
		fwrite(&VR->DataSize, 0x04, 0x01, VI->hFile);	// ROM Size
		templng = 0x00;
		templng |= (VR->dstart_msb << 24);
		fwrite(&templng, 0x04, 0x01, VI->hFile);		// Data Base Address
		if (VR->Data != NULL)
			fwrite(VR->Data, 0x01, VR->DataSize, VI->hFile);
		VI->BytesWrt += 0x07 + (blocksize & 0x7FFFFFFF);
	}
	for (curcmd = 0x00; curcmd < VI->CmdCount; curcmd ++)
	{
		VC = &VI->Commands[curcmd];
		fwrite(VC->Data, 0x01, VC->CmdLen, VI->hFile);
		VI->BytesWrt += VC->CmdLen;
	}
	VI->WroteHeader = 0x01;
	
	return;
}

static void vgm_header_sizecheck(uint16_t vgm_id, uint32_t MinVer, uint32_t MinSize)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
	if (VgmFile[vgm_id].hFile == NULL)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	
	if (Header->lngVersion < MinVer)
		Header->lngVersion = MinVer;
	if (VI->HeaderBytes < MinSize)
		VI->HeaderBytes = MinSize;
	
	return;
}

static void vgm_header_clear(uint16_t vgm_id)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
	if (VgmFile[vgm_id].hFile == NULL)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	memset(Header, 0x00, sizeof(VGM_HEADER));
	Header->fccVGM = 0x206D6756;	// 'Vgm '
	Header->lngEOFOffset = 0x00000000;
	Header->lngVersion = 0x00000151;
	//Header->lngGD3Offset = 0x00000000;
	//Header->lngTotalSamples = 0;
	//Header->lngLoopOffset = 0x00000000;
	//Header->lngLoopSamples = 0;
#ifdef DYNAMIC_HEADER_SIZE
	VI->HeaderBytes = 0x38;
	VI->WroteHeader = 0x00;
#else
	VI->HeaderBytes = sizeof(VGM_HEADER);
	VI->WroteHeader = 0x01;
#endif
	Header->lngDataOffset = VI->HeaderBytes - 0x34;
	
	//fseek(VI->hFile, 0x00, SEEK_SET);
	//fwrite(Header, 0x01, sizeof(VGM_HEADER), VI->hFile);
	//VI->BytesWrt += sizeof(VGM_HEADER);
	VI->BytesWrt = 0x00;
	
	return;
}

uint16_t vgm_open(uint8_t chip_type, int clock)
{
	uint16_t chip_id;
	uint16_t chip_file;
	uint16_t curvgm;
	uint32_t chip_val;
	char vgm_name[0x20];
	uint8_t use_two;
	
	logerror("vgm_open - Chip Type %02X, Clock %u\n", chip_type, clock);
	if (! LOG_VGM_FILE || chip_type == 0xFF)
		return 0xFFFF;
	
	chip_id = 0xFFFF;
	for (curvgm = 0x00; curvgm < MAX_VGM_CHIPS; curvgm ++)
	{
		if (VgmChip[curvgm].ChipType == 0xFF)
		{
			chip_id = curvgm;
			break;
		}
	}
	if (chip_id == 0xFFFF)
		return 0xFFFF;
	
	if (LOG_VGM_FILE != 0xDD)
	{
		// prevent it from logging chips with known audible errors in VGM logs
		if (chip_type == VGMC_ES5506)
			return 0xFFFF;	// logging needs to be reworked for this one
		if (chip_type == VGMC_SCSP)
			return 0xFFFF;	// timing is horribly off (see Fighting Vipers)
		else if (chip_type == VGMC_WSWAN)
			return 0xFFFF;	// WonderSwan emulation is horrible in general - even the logs are screwed
	}
	
	chip_file = 0xFFFF;
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].hFile == NULL)
			continue;
		
		use_two = 0x01;
		switch(chip_type)
		{
		case VGMC_SN76496:
			chip_val = VgmFile[curvgm].Header.lngHzPSG;
			break;
		case VGMC_YM2413:
			chip_val = VgmFile[curvgm].Header.lngHz2413;
			break;
		case VGMC_YM2612:
			chip_val = VgmFile[curvgm].Header.lngHz2612;
			break;
		case VGMC_YM2151:
			chip_val = VgmFile[curvgm].Header.lngHz2151;
			break;
		case VGMC_SEGAPCM:
			chip_val = VgmFile[curvgm].Header.lngHzSPCM;
			break;
		case VGMC_RF5C68:
			chip_val = VgmFile[curvgm].Header.lngHzRF5C68;
			use_two = 0x00;
			break;
		case VGMC_YM2203:
			chip_val = VgmFile[curvgm].Header.lngHz2203;
			break;
		case VGMC_YM2608:
			chip_val = VgmFile[curvgm].Header.lngHz2608;
			break;
		case VGMC_YM2610:
			chip_val = VgmFile[curvgm].Header.lngHz2610;
			break;
		case VGMC_YM3812:
			chip_val = VgmFile[curvgm].Header.lngHz3812;
			break;
		case VGMC_YM3526:
			chip_val = VgmFile[curvgm].Header.lngHz3526;
			break;
		case VGMC_Y8950:
			chip_val = VgmFile[curvgm].Header.lngHz8950;
			break;
		case VGMC_YMF262:
			chip_val = VgmFile[curvgm].Header.lngHz262;
			break;
		case VGMC_YMF278B:
			chip_val = VgmFile[curvgm].Header.lngHz278B;
			break;
		case VGMC_YMF271:
			chip_val = VgmFile[curvgm].Header.lngHz271;
			break;
		case VGMC_YMZ280B:
			chip_val = VgmFile[curvgm].Header.lngHz280B;
			break;
		case VGMC_T6W28:
			chip_val = VgmFile[curvgm].Header.lngHzPSG;
			use_two = 0x00;
			break;
		case VGMC_RF5C164:
			chip_val = VgmFile[curvgm].Header.lngHzRF5C164;
			use_two = 0x00;
			break;
		case VGMC_PWM:
			chip_val = VgmFile[curvgm].Header.lngHzPWM;
			use_two = 0x00;
			break;
		case VGMC_AY8910:
			chip_val = VgmFile[curvgm].Header.lngHzAY8910;
			break;
		case VGMC_GBSOUND:
			chip_val = VgmFile[curvgm].Header.lngHzGBDMG;
			break;
		case VGMC_NESAPU:
			chip_val = VgmFile[curvgm].Header.lngHzNESAPU;
			break;
		case VGMC_MULTIPCM:
			chip_val = VgmFile[curvgm].Header.lngHzMultiPCM;
			break;
		case VGMC_UPD7759:
			chip_val = VgmFile[curvgm].Header.lngHzUPD7759;
			break;
		case VGMC_OKIM6258:
			chip_val = VgmFile[curvgm].Header.lngHzOKIM6258;
			break;
		case VGMC_OKIM6295:
			chip_val = VgmFile[curvgm].Header.lngHzOKIM6295;
			break;
		case VGMC_K051649:
			chip_val = VgmFile[curvgm].Header.lngHzK051649;
			break;
		case VGMC_K054539:
			chip_val = VgmFile[curvgm].Header.lngHzK054539;
			break;
		case VGMC_C6280:
			chip_val = VgmFile[curvgm].Header.lngHzHuC6280;
			break;
		case VGMC_C140:
			chip_val = VgmFile[curvgm].Header.lngHzC140;
			break;
		case VGMC_K053260:
			chip_val = VgmFile[curvgm].Header.lngHzK053260;
			break;
		case VGMC_POKEY:
			chip_val = VgmFile[curvgm].Header.lngHzPokey;
			break;
		case VGMC_QSOUND:
			chip_val = VgmFile[curvgm].Header.lngHzQSound;
			use_two = 0x00;
			break;
		case VGMC_SCSP:
			chip_val = VgmFile[curvgm].Header.lngHzSCSP;
			//use_two = 0x00;
			break;
		case VGMC_WSWAN:
			chip_val = VgmFile[curvgm].Header.lngHzWSwan;
			break;
		case VGMC_VSU:
			chip_val = VgmFile[curvgm].Header.lngHzVSU;
			break;
		case VGMC_SAA1099:
			chip_val = VgmFile[curvgm].Header.lngHzSAA1099;
			break;
		case VGMC_ES5503:
			chip_val = VgmFile[curvgm].Header.lngHzES5503;
			break;
		case VGMC_ES5506:
			chip_val = VgmFile[curvgm].Header.lngHzES5506;
			break;
		case VGMC_X1_010:
			chip_val = VgmFile[curvgm].Header.lngHzX1_010;
			break;
		case VGMC_C352:
			chip_val = VgmFile[curvgm].Header.lngHzC352;
			use_two = 0x00;
			break;
		case VGMC_GA20:
			chip_val = VgmFile[curvgm].Header.lngHzGA20;
			break;
	//	case VGMC_OKIM6376:
	//		chip_val = VgmFile[curvgm].Header.lngHzOKIM6376;
	//		use_two = 0x00;
	//		break;
		default:
			return 0xFFFF;	// unknown chip - don't log
		}
		if (! chip_val)
		{
			chip_file = curvgm;
			break;
		}
		else if (use_two)
		{
			if (! (chip_val & 0x40000000) && (LOG_VGM_FILE & 0x01))
			{
				if (clock != chip_val)
					logerror("VGM Log: Warning - 2-chip mode, but chip clocks different!\n");
				chip_file = curvgm;
				clock = 0x40000000 | chip_val;
				chip_type |= 0x80;
				break;
			}
		}
	}	// end for(curvgm)
	if (chip_file == 0xFFFF)
	{
		for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
		{
			if (VgmFile[curvgm].hFile == NULL)
			{
				sprintf(vgm_name, "%s%hX.vgm", vgm_namebase, curvgm);
				logerror("Opening %s ...\t", vgm_name);
				VgmFile[curvgm].hFile = fopen(vgm_name, "wb");
				if (VgmFile[curvgm].hFile)
				{
					logerror("OK\n");
					chip_file = curvgm;
					VgmFile[curvgm].BytesWrt = 0;
					VgmFile[curvgm].SmplsWrt = 0;
					VgmFile[curvgm].EvtDelay = 0;
					vgm_header_clear(curvgm);
				}
				else
				{
					logerror("Failed to create the file!\n");
				}
				break;
			}
		}
	}
	if (chip_file == 0xFFFF)
		return 0xFFFF;
	
	VgmChip[chip_id].VgmID = chip_file;
	VgmChip[chip_id].ChipType = chip_type;
	VgmChip[chip_id].HadWrite = 0x00;
	VgmChip[chip_id].PCMCache = NULL;
	
	switch(chip_type & 0x7F)
	{
	case VGMC_SN76496:
		VgmFile[chip_file].Header.lngHzPSG = clock;
		break;
	case VGMC_YM2413:
		VgmFile[chip_file].Header.lngHz2413 = clock;
		break;
	case VGMC_YM2612:
		VgmFile[chip_file].Header.lngHz2612 = clock;
		break;
	case VGMC_YM2151:
		VgmFile[chip_file].Header.lngHz2151 = clock;
		break;
	case VGMC_SEGAPCM:
		VgmFile[chip_file].Header.lngHzSPCM = clock;
		break;
	case VGMC_RF5C68:
		VgmFile[chip_file].Header.lngHzRF5C68 = clock;
		VgmChip[chip_id].PCMCache = &VgmPCache[chip_file];
		vgm_setup_pcmcache(VgmChip[chip_id].PCMCache, 0x400);
		break;
	case VGMC_YM2203:
		VgmFile[chip_file].Header.lngHz2203 = clock;
		break;
	case VGMC_YM2608:
		VgmFile[chip_file].Header.lngHz2608 = clock;
		break;
	case VGMC_YM2610:
		VgmFile[chip_file].Header.lngHz2610 = clock;
		break;
	case VGMC_YM3812:
		VgmFile[chip_file].Header.lngHz3812 = clock;
		break;
	case VGMC_YM3526:
		VgmFile[chip_file].Header.lngHz3526 = clock;
		break;
	case VGMC_Y8950:
		VgmFile[chip_file].Header.lngHz8950 = clock;
		break;
	case VGMC_YMF262:
		VgmFile[chip_file].Header.lngHz262 = clock;
		break;
	case VGMC_YMF278B:
		VgmFile[chip_file].Header.lngHz278B = clock;
		break;
	case VGMC_YMF271:
		VgmFile[chip_file].Header.lngHz271 = clock;
		break;
	case VGMC_YMZ280B:
		VgmFile[chip_file].Header.lngHz280B = clock;
		break;
	case VGMC_T6W28:
		VgmFile[chip_file].Header.lngHzPSG = clock | 0xC0000000;	// Cheat to use 2 SN76489 chips
		break;
	case VGMC_RF5C164:
		VgmFile[chip_file].Header.lngHzRF5C164 = clock;
		VgmChip[chip_id].PCMCache = &VgmPCache[chip_file];
		vgm_setup_pcmcache(VgmChip[chip_id].PCMCache, 0x400);
		break;
	case VGMC_PWM:
		VgmFile[chip_file].Header.lngHzPWM = clock;
		break;
	case VGMC_AY8910:
		VgmFile[chip_file].Header.lngHzAY8910 = clock;
		break;
	case VGMC_GBSOUND:
		VgmFile[chip_file].Header.lngHzGBDMG = clock;
		break;
	case VGMC_NESAPU:
		VgmFile[chip_file].Header.lngHzNESAPU = clock;
		break;
	case VGMC_MULTIPCM:
		VgmFile[chip_file].Header.lngHzMultiPCM = clock;
		break;
	case VGMC_UPD7759:
		VgmFile[chip_file].Header.lngHzUPD7759 = clock;
		break;
	case VGMC_OKIM6258:
		VgmFile[chip_file].Header.lngHzOKIM6258 = clock;
		break;
	case VGMC_OKIM6295:
		VgmFile[chip_file].Header.lngHzOKIM6295 = clock;
		break;
	case VGMC_K051649:
		VgmFile[chip_file].Header.lngHzK051649 = clock;
		break;
	case VGMC_K054539:
		VgmFile[chip_file].Header.lngHzK054539 = clock;
		break;
	case VGMC_C6280:
		VgmFile[chip_file].Header.lngHzHuC6280 = clock;
		break;
	case VGMC_C140:
		VgmFile[chip_file].Header.lngHzC140 = clock;
		break;
	case VGMC_K053260:
		VgmFile[chip_file].Header.lngHzK053260 = clock;
		break;
	case VGMC_POKEY:
		VgmFile[chip_file].Header.lngHzPokey = clock;
		break;
	case VGMC_QSOUND:
		VgmFile[chip_file].Header.lngHzQSound = clock;
		break;
	case VGMC_SCSP:
		VgmFile[chip_file].Header.lngHzSCSP = clock;
		VgmChip[chip_id].PCMCache = &VgmPCache[chip_file];
		vgm_setup_pcmcache(VgmChip[chip_id].PCMCache, 0x4000);
		break;
	case VGMC_WSWAN:
		VgmFile[chip_file].Header.lngHzWSwan = clock;
		break;
	case VGMC_VSU:
		VgmFile[chip_file].Header.lngHzVSU = clock;
		break;
	case VGMC_SAA1099:
		VgmFile[chip_file].Header.lngHzSAA1099 = clock;
		break;
	case VGMC_ES5503:
		VgmFile[chip_file].Header.lngHzES5503 = clock;
		VgmChip[chip_id].PCMCache = &VgmPCache[chip_file];
		vgm_setup_pcmcache(VgmChip[chip_id].PCMCache, 0x1000);
		break;
	case VGMC_ES5506:
		VgmFile[chip_file].Header.lngHzES5506 = clock;
		break;
	case VGMC_X1_010:
		VgmFile[chip_file].Header.lngHzX1_010 = clock;
		break;
	case VGMC_C352:
		VgmFile[chip_file].Header.lngHzC352 = clock;
		break;
	case VGMC_GA20:
		VgmFile[chip_file].Header.lngHzGA20 = clock;
		break;
//	case VGMC_OKIM6376:
//		VgmFile[chip_file].Header.lngHzOKIM6376 = clock;
//		break;
	}
	
	switch(chip_type & 0x7F)
	{
	case VGMC_SN76496:
	case VGMC_YM2413:
	case VGMC_YM2612:
	case VGMC_YM2151:
	case VGMC_SEGAPCM:
	case VGMC_T6W28:
		vgm_header_sizecheck(chip_file, 0x00000151, 0x40);
		break;
	case VGMC_RF5C68:
	case VGMC_YM2203:
	case VGMC_YM2608:
	case VGMC_YM2610:
	case VGMC_YM3812:
	case VGMC_YM3526:
	case VGMC_Y8950:
	case VGMC_YMF262:
	case VGMC_YMF278B:
	case VGMC_YMF271:
	case VGMC_YMZ280B:
	case VGMC_RF5C164:
	case VGMC_PWM:
	case VGMC_AY8910:
		vgm_header_sizecheck(chip_file, 0x00000151, 0x80);
		break;
	case VGMC_GBSOUND:
	case VGMC_NESAPU:
	case VGMC_MULTIPCM:
	case VGMC_UPD7759:
	case VGMC_OKIM6258:
	case VGMC_OKIM6295:
	case VGMC_K051649:
	case VGMC_K054539:
	case VGMC_C6280:
	case VGMC_C140:
	case VGMC_K053260:
	case VGMC_POKEY:
	case VGMC_QSOUND:
//	case VGMC_OKIM6376:
		vgm_header_sizecheck(chip_file, 0x00000161, 0xC0);
		break;
	case VGMC_SCSP:
		vgm_header_sizecheck(chip_file, 0x00000171, 0xC0);
		break;
	case VGMC_WSWAN:
	case VGMC_VSU:
	case VGMC_SAA1099:
	case VGMC_ES5503:
	case VGMC_ES5506:
	case VGMC_X1_010:
	case VGMC_C352:
		vgm_header_sizecheck(chip_file, 0x00000171, 0xE0);
		break;
	case VGMC_GA20:
		vgm_header_sizecheck(chip_file, 0x00000171, 0x100);
		break;
	}
	
	return chip_id;
}

static void vgm_setup_pcmcache(VGM_PCMCACHE* TempPC, uint32_t Size)
{
	TempPC->CacheSize = Size;
	if (TempPC->CacheData != NULL)
		free(TempPC->CacheData);
	TempPC->CacheData = (uint8_t*)malloc(Size);
	
	return;
}

void vgm_header_set(uint16_t chip_id, uint8_t attr, uint32_t data)
{
	VGM_HEADER* VH;
	uint8_t bitcnt;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VH = &VgmFile[VgmChip[chip_id].VgmID].Header;
	switch(VgmChip[chip_id].ChipType & 0x7F)	// Write the Header data
	{
	case VGMC_SN76496:
	case VGMC_T6W28:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Shift Register Width (Feedback Mask)
			bitcnt = 0x00;	// Convert the BitMask to BitCount
			while(data)
			{
				data >>= 1;
				bitcnt ++;
			}
			VH->bytPSG_SRWidth = bitcnt;
			break;
		case 0x02:	// Feedback Pattern (White Noise Tap #1)
			VH->shtPSG_Feedback = (uint16_t)data;
			break;
		case 0x03:	// Feedback Pattern (White Noise Tap #2)
			// must be called after #1
			VH->shtPSG_Feedback |= (uint16_t)data;
			break;
		case 0x04:	// Negate Channels Flag
			VH->bytPSG_Flags &= ~(0x01 << 1);
			VH->bytPSG_Flags |= (data & 0x01) << 1;
			break;
		case 0x05:	// Stereo Flag (On/Off)
			// 0 is Stereo and 1 is mono
			VH->bytPSG_Flags &= ~(0x01 << 2);
			VH->bytPSG_Flags |= (~data & 0x01) << 2;
			break;
		case 0x06:	// Clock Divider (On/Off)
			VH->bytPSG_Flags &= ~(0x01 << 3);
			bitcnt = (data == 1) ? 0x01 : 0x00;
			VH->bytPSG_Flags |= (bitcnt & 0x01) << 3;
			break;
		case 0x07:	// Freq 0 is Max
			VH->bytPSG_Flags &= ~(0x01 << 0);
			VH->bytPSG_Flags |= (data & 0x01) << 0;
			break;
		}
		break;
	case VGMC_YM2413:
		break;
	case VGMC_YM2612:
		break;
	case VGMC_YM2151:
		break;
	case VGMC_SEGAPCM:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Sega PCM Interface
			VH->lngSPCMIntf = data;
			break;
		}
		break;
	case VGMC_RF5C68:
		break;
	case VGMC_YM2203:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Flags
			VH->lngAYFlagsYM2203 = data & 0xFF;
			break;
		}
		break;
	case VGMC_YM2608:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Flags
			VH->lngAYFlagsYM2608 = data & 0xFF;
			break;
		}
		break;
	case VGMC_YM2610:
		switch(attr)
		{
		case 0x00:	// Chip Type (set YM2610B mode)
			VH->lngHz2610 = (VH->lngHz2610 & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_YM3812:	
		break;
	case VGMC_YM3526:
		break;
	case VGMC_Y8950:
		break;
	case VGMC_YMF262:
		switch(attr)
		{
		case 0x00:	// is Part of OPL4
			if (data)
			{
				VgmChip[chip_id].ChipType = VGMC_YMF278B;
				VH->lngHz262 = 0x00;
			}
			break;
		}
		break;
	case VGMC_YMF278B:
		break;
	case VGMC_YMF271:
		break;
	case VGMC_YMZ280B:
		break;
	case VGMC_RF5C164:
		break;
	case VGMC_PWM:
		break;
	case VGMC_AY8910:
		switch(attr)
		{
		case 0x00:	// Device Type
			VH->lngAYType = data & 0xFF;
			break;
		case 0x01:	// Flags
			VH->lngAYFlags = data & 0xFF;
			break;
		case 0x10:	// Resistor Loads
		case 0x11:
		case 0x12:
			logerror("AY8910: Resistor Load %hu = %u\n", attr & 0x0F, data);
			break;
		}
		break;
	case VGMC_GBSOUND:
		break;
	case VGMC_NESAPU:
		switch(attr)
		{
		case 0x00:	// FDS Enable
			VH->lngHzNESAPU = (VH->lngHzNESAPU & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_MULTIPCM:
		break;
	case VGMC_UPD7759:
		switch(attr)
		{
		case 0x00:	// Chip Type (set master/slave mode)
			VH->lngHzUPD7759 = (VH->lngHzUPD7759 & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_OKIM6258:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Clock Divider
			VH->bytOKI6258Flags &= ~(0x03 << 0);
			VH->bytOKI6258Flags |= (data & 0x03) << 0;
			break;
		case 0x02:	// ADPCM Type
			VH->bytOKI6258Flags &= ~(0x01 << 2);
			VH->bytOKI6258Flags |= (data & 0x01) << 2;
			break;
		case 0x03:	// 12-Bit Output
			VH->bytOKI6258Flags &= ~(0x01 << 3);
			VH->bytOKI6258Flags |= (data & 0x01) << 3;
			break;
		}
		break;
	case VGMC_OKIM6295:
		switch(attr)
		{
		case 0x00:	// Chip Type (pin 7 state)
			VH->lngHzOKIM6295 = (VH->lngHzOKIM6295 & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_K051649:
		switch(attr)
		{
		case 0x00:	// Chip Type (K051649/SCC or K052539/SCC+)
			VH->lngHzK051649 = (VH->lngHzK051649 & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_K054539:
		switch(attr)
		{
		case 0x01:	// Control Flags
			VH->bytK054539Flags = data;
			break;
		}
		break;
	case VGMC_C6280:
		break;
	case VGMC_C140:
		switch(attr)
		{
		case 0x01:	// Banking Type
			VH->bytC140Type = data;
			break;
		}
		break;
	case VGMC_K053260:
		break;
	case VGMC_POKEY:
		break;
	case VGMC_QSOUND:
		break;
	case VGMC_SCSP:
		break;
	case VGMC_WSWAN:
		break;
	case VGMC_VSU:
		break;
	case VGMC_SAA1099:
		break;
	case VGMC_ES5503:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Channels
			VH->bytES5503Chns = data & 0xFF;
			break;
		}
		break;
	case VGMC_ES5506:
		switch(attr)
		{
		case 0x00:	// Chip Type (5505/5506)
			VH->lngHzES5506 = (VH->lngHzES5506 & 0x7FFFFFFF) | (data << 31);
			break;
		case 0x01:	// Channels
			VH->bytES5506Chns = data & 0xFF;
			break;
		}
		break;
	case VGMC_X1_010:
		break;
	case VGMC_C352:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Clock Divider
			VH->bytC352ClkDiv = (data / 4) & 0xFF;
			break;
		}
		break;
	case VGMC_GA20:
		break;
//	case VGMC_OKIM6376:
//		break;
	}
	
	return;
}

static void vgm_close(uint16_t vgm_id)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
	if (! LOG_VGM_FILE || vgm_id == 0xFFFF)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	
	if (! VI->WroteHeader)
	{
		fclose(VI->hFile);
		VI->hFile = NULL;
		return;
	}
	
	vgm_write_delay(vgm_id);
	fputc(0x66, VI->hFile);	// Write EOF Command
	VI->BytesWrt ++;
	
	// GD3 Tag
	Header->lngGD3Offset = VI->BytesWrt - 0x00000014;
	fwrite(&VgmTag.fccGD3, 0x04, 0x01, VI->hFile);
	fwrite(&VgmTag.lngVersion, 0x04, 0x01, VI->hFile);
	fwrite(&VgmTag.lngTagLength, 0x04, 0x01, VI->hFile);
	fwrite(VgmTag.strTrackNameE, sizeof(gd3char_t), utf16len(VgmTag.strTrackNameE) + 1, VI->hFile);
	fwrite(VgmTag.strTrackNameJ, sizeof(gd3char_t), utf16len(VgmTag.strTrackNameJ) + 1, VI->hFile);
	fwrite(VgmTag.strGameNameE, sizeof(gd3char_t), utf16len(VgmTag.strGameNameE) + 1, VI->hFile);
	fwrite(VgmTag.strGameNameJ, sizeof(gd3char_t), utf16len(VgmTag.strGameNameJ) + 1, VI->hFile);
	fwrite(VgmTag.strSystemNameE, sizeof(gd3char_t), utf16len(VgmTag.strSystemNameE) + 1, VI->hFile);
	fwrite(VgmTag.strSystemNameJ, sizeof(gd3char_t), utf16len(VgmTag.strSystemNameJ) + 1, VI->hFile);
	fwrite(VgmTag.strAuthorNameE, sizeof(gd3char_t), utf16len(VgmTag.strAuthorNameE) + 1, VI->hFile);
	fwrite(VgmTag.strAuthorNameJ, sizeof(gd3char_t), utf16len(VgmTag.strAuthorNameJ) + 1, VI->hFile);
	fwrite(VgmTag.strReleaseDate, sizeof(gd3char_t), utf16len(VgmTag.strReleaseDate) + 1, VI->hFile);
	fwrite(VgmTag.strCreator, sizeof(gd3char_t), utf16len(VgmTag.strCreator) + 1, VI->hFile);
	fwrite(VgmTag.strNotes, sizeof(gd3char_t), utf16len(VgmTag.strNotes) + 1, VI->hFile);
	VI->BytesWrt += 0x0C + VgmTag.lngTagLength;
	
	// Rewrite Header
	Header->lngTotalSamples = VI->SmplsWrt;
	Header->lngEOFOffset = VI->BytesWrt - 0x00000004;
	Header->lngDataOffset = VI->HeaderBytes - 0x34;
	
	fseek(VI->hFile, 0x00, SEEK_SET);
	fwrite(Header, 0x01, VI->HeaderBytes, VI->hFile);
	
	fclose(VI->hFile);
	VI->hFile = NULL;
	
	logerror("VGM %02hX closed. %u Bytes, %u Samples written.\n", vgm_id, VI->BytesWrt, VI->SmplsWrt);
	
	return;
}

static void vgm_write_delay(uint16_t vgm_id)
{
	VGM_INF* VI;
	uint16_t delaywrite;
	
	VI = &VgmFile[vgm_id];
	if (! VI->WroteHeader && VI->EvtDelay)
		vgm_header_postwrite(vgm_id);	// write post-header data
	
	if (VI->EvtDelay)
	{
		for (delaywrite = 0x00; delaywrite < MAX_VGM_CHIPS; delaywrite ++)
		{
			if (VgmChip[delaywrite].ChipType != 0xFF)
				vgm_flush_pcm(&VgmChip[delaywrite]);
		}
	}
	
	while(VI->EvtDelay)
	{
		if (VI->EvtDelay > 0x0000FFFF)
			delaywrite = 0xFFFF;
		else
			delaywrite = (uint16_t)VI->EvtDelay;
		
		if (delaywrite <= 0x0010)
		{
			fputc(0x6F + delaywrite, VI->hFile);
			VI->BytesWrt += 0x01;
		}
		else
		{
			fputc(0x61, VI->hFile);
			fwrite(&delaywrite, 0x02, 0x01, VI->hFile);
			VI->BytesWrt += 0x03;
		}
		VI->SmplsWrt += delaywrite;
		
		VI->EvtDelay -= delaywrite;
	}
	
	return;
}

void vgm_write(uint16_t chip_id, uint8_t port, uint16_t r, uint8_t v)
{
	VGM_CHIP* VC;
	VGM_INF* VI;
	VGM_PCMCACHE* VPC;
	int8_t cm;	// "Cheat Mode" to support 2 instances of 1 chip within 1 file
	uint16_t curchip;
	VGM_INIT_CMD WriteCmd;
	uint32_t mem_addr;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VC = &VgmChip[chip_id];
	VI = &VgmFile[VC->VgmID];
	if (VI->hFile == NULL)
		return;
	
	if (! VC->HadWrite)
	{
		VC->HadWrite = 0x01;
		if (VC->ChipType & 0x80)
		{
			for (curchip = 0x00; curchip < chip_id; curchip ++)
			{
				if (VgmChip[curchip].ChipType == (VC->ChipType & 0x7F))
					VgmChip[curchip].HadWrite = 0x01;
			}
		}
	}
	
	WriteCmd.CmdLen = 0x00;
	cm = (VC->ChipType & 0x80) ? 0x50 : 0x00;
	switch(VC->ChipType & 0x7F)	// Write the data
	{
	case VGMC_T6W28:
		cm = ~port & 0x01;
		port = 0x00;
		// fall through
	case VGMC_SN76496:
		switch(port)
		{
		case 0x00:
			cm = cm ? -0x20 : 0x00;
			WriteCmd.Data[0x00] = 0x50 + cm;
			WriteCmd.Data[0x01] = r;
			WriteCmd.CmdLen = 0x02;
			break;
		case 0x01:
			cm = cm ? -0x10 : 0x00;
			WriteCmd.Data[0x00] = 0x4F + cm;
			WriteCmd.Data[0x01] = r;
			WriteCmd.CmdLen = 0x02;
			break;
		}
		break;
	case VGMC_YM2413:
		WriteCmd.Data[0x00] = 0x51 + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM2612:
		WriteCmd.Data[0x00] = 0x52 + (port & 0x01) + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM2151:
		WriteCmd.Data[0x00] = 0x54 + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_SEGAPCM:
		r |= (VC->ChipType & 0x80) << 8;
		WriteCmd.Data[0x00] = 0xC0;				// Write Memory
		WriteCmd.Data[0x01] = (r >> 0) & 0xFF;	// offset low
		WriteCmd.Data[0x02] = (r >> 8) & 0xFF;	// offset high
		WriteCmd.Data[0x03] = v;				// data
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_RF5C68:
		switch(port)
		{
		case 0x00:
			WriteCmd.Data[0x00] = 0xB0;				// Write Register
			WriteCmd.Data[0x01] = r;				// Register
			WriteCmd.Data[0x02] = v;				// Value
			WriteCmd.CmdLen = 0x03;
			break;
		case 0x01:
			WriteCmd.Data[0x00] = 0xC1;				// Write Memory
			WriteCmd.Data[0x01] = (r >> 0) & 0xFF;	// offset low
			WriteCmd.Data[0x02] = (r >> 8) & 0xFF;	// offset high
			WriteCmd.Data[0x03] = v;				// Data
			WriteCmd.CmdLen = 0x04;
			break;
		}
		break;
	case VGMC_YM2203:
		WriteCmd.Data[0x00] = 0x55 + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM2608:
		WriteCmd.Data[0x00] = 0x56 + (port & 0x01) + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM2610:
		WriteCmd.Data[0x00] = 0x58 + (port & 0x01) + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM3812:
		WriteCmd.Data[0x00] = 0x5A + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM3526:
		WriteCmd.Data[0x00] = 0x5B + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_Y8950:
		WriteCmd.Data[0x00] = 0x5C + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YMF262:
		WriteCmd.Data[0x00] = 0x5E + (port & 0x01) + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YMF278B:
		WriteCmd.Data[0x00] = 0xD0;
		WriteCmd.Data[0x01] = port | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_YMF271:
		WriteCmd.Data[0x00] = 0xD1;
		WriteCmd.Data[0x01] = port | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_YMZ280B:
		WriteCmd.Data[0x00] = 0x5D + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_RF5C164:
		switch(port)
		{
		case 0x00:
			WriteCmd.Data[0x00] = 0xB1;				// Write Register
			WriteCmd.Data[0x01] = r;				// Register
			WriteCmd.Data[0x02] = v;				// Value
			WriteCmd.CmdLen = 0x03;
			break;
		case 0x01:
			WriteCmd.Data[0x00] = 0xC2;				// Write Memory
			WriteCmd.Data[0x01] = (r >> 0) & 0xFF;	// offset low
			WriteCmd.Data[0x02] = (r >> 8) & 0xFF;	// offset high
			WriteCmd.Data[0x03] = v;				// Data
			WriteCmd.CmdLen = 0x04;
			break;
		}
		break;
	case VGMC_PWM:
		WriteCmd.Data[0x00] = 0xB2;
		WriteCmd.Data[0x01] = (port << 4) | ((r & 0xF00) >> 8);
		WriteCmd.Data[0x02] = r & 0xFF;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_AY8910:
		WriteCmd.Data[0x00] = 0xA0;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_GBSOUND:
		WriteCmd.Data[0x00] = 0xB3;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_NESAPU:
		WriteCmd.Data[0x00] = 0xB4;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_MULTIPCM:
		switch(port)
		{
		case 0x00:	// Register Write
			WriteCmd.Data[0x00] = 0xB5;
			WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
			WriteCmd.Data[0x02] = v;
			WriteCmd.CmdLen = 0x03;
			break;
		case 0x01:	// Bank Write
			WriteCmd.Data[0x00] = 0xC3;
			WriteCmd.Data[0x01] = v | (VC->ChipType & 0x80);	// Both/Left/Right Offset
			WriteCmd.Data[0x02] = (r >> 0) & 0xFF;				// offset low
			WriteCmd.Data[0x03] = (r >> 8) & 0xFF;				// offset high
			WriteCmd.CmdLen = 0x04;
			break;
		}
		break;
	case VGMC_UPD7759:
		WriteCmd.Data[0x00] = 0xB6;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_OKIM6258:
		WriteCmd.Data[0x00] = 0xB7;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_OKIM6295:
		WriteCmd.Data[0x00] = 0xB8;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_K051649:
		WriteCmd.Data[0x00] = 0xD2;
		WriteCmd.Data[0x01] = port | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_K054539:
		WriteCmd.Data[0x00] = 0xD3;
		WriteCmd.Data[0x01] = (r >> 8) | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r & 0xFF;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_C6280:
		WriteCmd.Data[0x00] = 0xB9;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_C140:
		WriteCmd.Data[0x00] = 0xD4;
		WriteCmd.Data[0x01] = (r >> 8) | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r & 0xFF;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_K053260:
		WriteCmd.Data[0x00] = 0xBA;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_POKEY:
		WriteCmd.Data[0x00] = 0xBB;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_QSOUND:
		WriteCmd.Data[0x00] = 0xC4;
		WriteCmd.Data[0x01] = (r & 0xFF00) >> 8;	// Data MSB
		WriteCmd.Data[0x02] = (r & 0x00FF) >> 0;	// Data LSB
		WriteCmd.Data[0x03] = v;					// Command
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_SCSP:
		switch(port & 0x80)
		{
		case 0x00:	// Register Write
			WriteCmd.Data[0x00] = 0xC5;
			WriteCmd.Data[0x01] = ((r & 0xFF00) >> 8) | (VC->ChipType & 0x80);	// Data MSB
			WriteCmd.Data[0x02] = (r & 0x00FF) >> 0;							// Data LSB
			WriteCmd.Data[0x03] = v;											// Command
			WriteCmd.CmdLen = 0x04;
			break;
		case 0x80:	// Memory Write
			//vgm_write_delay(VC->VgmID);
			
			VPC = VC->PCMCache;
			mem_addr = ((port & 0x7F) << 16) | (r << 0);
			
			// optimize consecutive Memory Writes
			if (VPC->Start == 0xFFFFFFFF || mem_addr != VPC->Next)
			{
				// flush cache to file
				vgm_flush_pcm(VC);
				vgm_write_delay(VC->VgmID);
				//printf("Mem Cache Start: %06X\n", mem_addr);
				VPC->Start = mem_addr;
				VPC->Next = VPC->Start;
				VPC->Pos = 0x00;
			}
			VPC->CacheData[VPC->Pos] = v;
			VPC->Pos ++;
			VPC->Next ++;
			if (VPC->Pos >= VPC->CacheSize)
				VPC->Next = 0xFFFFFFFF;
			return;
		}
		break;
	case VGMC_WSWAN:
		switch(port)
		{
		case 0x00:
			WriteCmd.Data[0x00] = 0xBC;
			WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
			WriteCmd.Data[0x02] = v;
			WriteCmd.CmdLen = 0x03;
			break;
		case 0x01:
			WriteCmd.Data[0x00] = 0xC6;				// Write Memory
			WriteCmd.Data[0x01] = (r >> 0) & 0xFF;	// offset low
			WriteCmd.Data[0x02] = (r >> 8) & 0xFF;	// offset high
			WriteCmd.Data[0x03] = v;				// Data
			WriteCmd.CmdLen = 0x04;
			break;
		}
		break;
	case VGMC_VSU:
		r |= (VC->ChipType & 0x80) << 8;
		WriteCmd.Data[0x00] = 0xC7;
		WriteCmd.Data[0x01] = (r & 0xFF00) >> 8;	// Offset MSB
		WriteCmd.Data[0x02] = (r & 0x00FF) >> 0;	// Offset LSB
		WriteCmd.Data[0x03] = v;					// Data
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_SAA1099:
		WriteCmd.Data[0x00] = 0xBD;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_ES5503:
		switch(port & 0x80)
		{
		case 0x00:	// register write
			r |= (VC->ChipType & 0x80) << 8;
			WriteCmd.Data[0x00] = 0xD5;
			WriteCmd.Data[0x01] = (r & 0xFF00) >> 8;	// Chip Select
			WriteCmd.Data[0x02] = (r & 0x00FF) >> 0;	// Register
			WriteCmd.Data[0x03] = v;
			WriteCmd.CmdLen = 0x04;
			break;
		case 0x80:	// RAM write
			VPC = VC->PCMCache;
			mem_addr = ((port & 0x7F) << 16) | (r << 0);
			
			// optimize consecutive Memory Writes
			if (VPC->Start == 0xFFFFFFFF || mem_addr != VPC->Next)
			{
				// flush cache to file
				vgm_flush_pcm(VC);
				vgm_write_delay(VC->VgmID);
				VPC->Start = mem_addr;
				VPC->Next = VPC->Start;
				VPC->Pos = 0x00;
			}
			VPC->CacheData[VPC->Pos] = v;
			VPC->Pos ++;
			VPC->Next ++;
			if (VPC->Pos >= VPC->CacheSize)
				VPC->Next = 0xFFFFFFFF;
			return;
		}
		break;
	case VGMC_ES5506:
		switch(port & 0x80)
		{
		case 0x00:	// 8-bit register write
			WriteCmd.Data[0x00] = 0xBE;
			WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
			WriteCmd.Data[0x02] = v;
			WriteCmd.CmdLen = 0x03;
			break;
		case 0x80:		// 16-bit register write
			WriteCmd.Data[0x00] = 0xD6;
			WriteCmd.Data[0x01] = v | (VC->ChipType & 0x80);
			WriteCmd.Data[0x02] = (r & 0xFF00) >> 8;	// Data MSB
			WriteCmd.Data[0x03] = (r & 0x00FF) >> 0;	// Data LSB
			WriteCmd.CmdLen = 0x04;
			break;
		}
		break;
	case VGMC_X1_010:
		r |= (VC->ChipType & 0x80) << 8;
		WriteCmd.Data[0x00] = 0xC8;
		WriteCmd.Data[0x01] = (r & 0xFF00) >> 8;	// Offset MSB
		WriteCmd.Data[0x02] = (r & 0x00FF) >> 0;	// Offset LSB
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	/*case VGMC_C352:
		if (port == 0x01)
		{
			v = 0x06/2;	// flags register (channel 0)
			r = 0xFFFF;
		}
		WriteCmd.Data[0x00] = 0xC9;
		WriteCmd.Data[0x01] = v;					// Register
		WriteCmd.Data[0x02] = (r & 0xFF00) >> 8;	// Data MSB
		WriteCmd.Data[0x03] = (r & 0x00FF) >> 0;	// Data LSB
		WriteCmd.CmdLen = 0x04;
		break;*/
	case VGMC_C352:
		port |= (VC->ChipType & 0x80);
		WriteCmd.Data[0x00] = 0xE1;
		WriteCmd.Data[0x01] = port;					// Register MSB
		WriteCmd.Data[0x02] = v;					// Register LSB
		WriteCmd.Data[0x03] = (r & 0xFF00) >> 8;	// Data MSB
		WriteCmd.Data[0x04] = (r & 0x00FF) >> 0;	// Data LSB
		WriteCmd.CmdLen = 0x05;
		break;
	case VGMC_GA20:
		WriteCmd.Data[0x00] = 0xBF;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
//	case VGMC_OKIM6376:
//		WriteCmd.Data[0x00] = 0x31;
//		WriteCmd.Data[0x01] = v;
//		WriteCmd.CmdLen = 0x02;
//		break;
	}
	
	vgm_write_delay(VC->VgmID);
	
	if (! VI->WroteHeader)
	{
		/*if (VI->CmdCount >= VI->CmdAlloc)
		{
			VI->CmdAlloc += 0x100;
			VI->Commands = (VGM_INIT_CMD*)realloc(VI->Commands, sizeof(VGM_INIT_CMD) * VI->CmdAlloc);
		}*/
		
		// most commands sent at time 0 come from soundchip_reset(),
		// so I check if the command is "worth" being written
		cm = 0x00;
		switch(VC->ChipType & 0x7F)
		{
		case VGMC_YM2203:
		case VGMC_YM2608:	// (not on YM2610 and YM2612)
			if (r >= 0x2D && r <= 0x2F)
				cm = 0x01;	// Prescaler Select
			break;
		case VGMC_OKIM6258:
			if (r >= 0x08 && r <= 0x0F)
				cm = 0x01;	// OKIM6258 clock change
			break;
		case VGMC_OKIM6295:
			if (r >= 0x08 && r <= 0x0F)
				cm = 0x01;	// OKIM6295 clock change and configuration
			break;
		}
		
		if (cm && VI->CmdCount < 0x100)
		{
			VI->Commands[VI->CmdCount] = WriteCmd;
			VI->CmdCount ++;
		}
		return;
	}
	
	if (VI->hFile != NULL)
	{
		fwrite(WriteCmd.Data, 0x01, WriteCmd.CmdLen, VI->hFile);
		VI->BytesWrt += WriteCmd.CmdLen;
	}
	
	return;
}

void vgm_write_large_data(uint16_t chip_id, uint8_t type, uint32_t datasize, uint32_t value1, uint32_t value2, const void* data)
{
	// datasize - ROM/RAM size
	// value1 - Start Address
	// value2 - Bytes to Write (0 -> write from Start Address to end of ROM/RAM)
	
	VGM_INF* VI;
	VGM_ROM_DATA* VR;
	uint32_t finalsize;
	uint8_t blk_type;
	uint8_t dstart_msb;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VI = &VgmFile[VgmChip[chip_id].VgmID];
	if (VI->hFile == NULL)
		return;
	
	blk_type = 0x00;
	dstart_msb = 0x00;
	switch(VgmChip[chip_id].ChipType & 0x7F)	// Write the data
	{
	case VGMC_SN76496:
	case VGMC_T6W28:
		break;
	case VGMC_YM2413:
		break;
	case VGMC_YM2612:
		break;
	case VGMC_YM2151:
		break;
	case VGMC_SEGAPCM:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Image
			blk_type = 0x80;	// Type: SegaPCM ROM Image
			break;
		}
		break;
	case VGMC_RF5C68:
		break;
	case VGMC_YM2203:
		break;
	case VGMC_YM2608:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// PCM ROM Data
			blk_type = 0x81;	// Type: YM2608 DELTA-T ROM Data
			break;
		}
		break;
	case VGMC_YM2610:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// PCM ROM Data A
			blk_type = 0x82;	// Type: YM2610 ADPCM ROM Data
			break;
		case 0x02:	// PCM ROM Data B
			blk_type = 0x83;	// Type: YM2610 DELTA-T ROM Data
			break;
		}
		break;
	case VGMC_YM3812:
		break;
	case VGMC_YM3526:
		break;
	case VGMC_Y8950:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// DELTA-T Memory
			blk_type = 0x88;	// Type: Y8950 DELTA-T ROM Data
			break;
		}
		break;
	case VGMC_YMF262:
		break;
	case VGMC_YMF278B:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x84;	// Type: YMF278B ROM Data
			break;
		case 0x02:	// RAM Data
			blk_type = 0x87;	// Type: YMF278B RAM Data
			break;
		}
		break;
	case VGMC_YMF271:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x85;	// Type: YMF271 ROM Data
			break;
		}
		break;
	case VGMC_YMZ280B:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x86;	// Type: YMZ280B ROM Data
			break;
		}
		break;
	case VGMC_RF5C164:
		break;
	case VGMC_PWM:
		break;
	case VGMC_AY8910:
		break;
	case VGMC_GBSOUND:
		break;
	case VGMC_NESAPU:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// RAM Data
			if (vgm_nes_ram_check(VI, datasize, &value1, &value2, (uint8_t*)data))
				blk_type = 0xC2;
			break;
		}
		break;
	case VGMC_MULTIPCM:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x89;	// Type: MultiPCM ROM Data
			break;
		}
		break;
	case VGMC_UPD7759:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8A;	// Type: UPD7759 ROM Data
			break;
		}
		break;
	case VGMC_OKIM6258:
		break;
	case VGMC_OKIM6295:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8B;	// Type: OKIM6295 ROM Data
			break;
		}
		break;
	case VGMC_K051649:
		break;
	case VGMC_K054539:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8C;	// Type: K054539 ROM Data
			break;
		}
		break;
	case VGMC_C6280:
		break;
	case VGMC_C140:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8D;	// Type: C140 ROM Data
			break;
		}
		break;
	case VGMC_K053260:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8E;	// Type: K053260 ROM Data
			break;
		}
		break;
	case VGMC_POKEY:
		break;
	case VGMC_QSOUND:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x8F;	// Type: QSound ROM Data
			break;
		}
		break;
	case VGMC_SCSP:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// RAM Data
			vgm_flush_pcm(&VgmChip[chip_id]);
			blk_type = 0xE0;	// Type: YMF292/SCSP RAM Data
			break;
		case 0x02:	// ROM Data
			blk_type = 0x06;	// Type: unused/invalid
			break;
		}
		break;
	case VGMC_WSWAN:
		break;
	case VGMC_VSU:
		break;
	case VGMC_SAA1099:
		break;
	case VGMC_ES5503:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// RAM Data
			vgm_flush_pcm(&VgmChip[chip_id]);
			blk_type = 0xE1;	// Type: ES5503 RAM Data
			break;
		}
		break;
	case VGMC_ES5506:
		// "type" tells us the actual region
		blk_type = 0x90;	// Type: ES5506 ROM Data
		dstart_msb = type << 4;
		break;
	case VGMC_X1_010:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x91;	// Type: X1-010 ROM Data
			break;
		}
		break;
	case VGMC_C352:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x92;	// Type: C352 ROM Data
			break;
		}
		break;
	case VGMC_GA20:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// ROM Data
			blk_type = 0x93;	// Type: GA20 ROM Data
			break;
		}
		break;
//	case VGMC_OKIM6376:
//		switch(type)
//		{
//		case 0x00:
//			break;
//		case 0x01:	// ROM Data
//			//blk_type = 0x8C;	// Type: OKIM6376 ROM Data
//			break;
//		}
//		break;
	}
	
	if (! blk_type)
		return;
	
	if (data == NULL)
		logerror("ROM Data %02X: (0x%X bytes) is NULL!\n", blk_type, datasize);
	
	vgm_write_delay(VgmChip[chip_id].VgmID);
	
	if (! VI->WroteHeader)
	{
		switch(blk_type & 0xC0)
		{
		case 0x80:	// ROM Image
			if (VI->DataCount < 0x20)
			{
				VR = &VI->DataBlk[VI->DataCount];
				VI->DataCount ++;
				
				VR->Type = blk_type;
				VR->dstart_msb = dstart_msb;
				VR->DataSize = datasize | ((VgmChip[chip_id].ChipType & 0x80) << 24);
				VR->Data = data;
			}
			break;
		case 0xC0:	// RAM Writes
			break;
		}
		return;
	}
	
	fputc(0x67, VI->hFile);
	fputc(0x66, VI->hFile);
	fputc(blk_type, VI->hFile);
	
	switch(blk_type & 0xC0)
	{
	case 0x00:	// Normal Data Block
		if (! value2)
			value2 = datasize - value1;
		if (data == NULL)
		{
			value1 = 0x00;
			value2 = 0x00;
		}
		finalsize = value2;
		finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
		
		fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
		fwrite(data, 0x01, value2, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
		break;
	case 0x80:	// ROM Image
		// Value 1 & 2 are used to write parts of the image (and save space)
		if (! value2)
			value2 = datasize - value1;
		if (data == NULL)
		{
			value1 = 0x00;
			value2 = 0x00;
		}
		finalsize = 0x08 + value2;
		finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
		value1 |= (dstart_msb << 24);
		
		fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
		fwrite(&datasize, 0x04, 0x01, VI->hFile);	// ROM Size
		fwrite(&value1, 0x04, 0x01, VI->hFile);		// Data Base Address
		fwrite(data, 0x01, value2, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
		break;
	case 0xC0:	// RAM Writes
		if (! value2)
			value2 = datasize - value1;
		if (data == NULL)
		{
			value1 = 0x00;
			value2 = 0x00;
		}
		
		if (! (blk_type & 0x20))
		{
			finalsize = 0x02 + value2;
			finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
			
			fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
			fwrite(&value1, 0x02, 0x01, VI->hFile);		// Data Address
		}
		else
		{
			finalsize = 0x04 + value2;
			finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
			
			fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
			fwrite(&value1, 0x04, 0x01, VI->hFile);		// Data Address
		}
		fwrite(data, 0x01, value2, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
		break;
	}
	
	return;
}

static uint8_t vgm_nes_ram_check(VGM_INF* VI, uint32_t datasize, uint32_t* value1, uint32_t* value2, const uint8_t* data)
{
	uint16_t CurPos;
	uint16_t DataStart;
	uint16_t DataEnd;
	
	if (VI->NesMemEmpty)
	{
		VI->NesMemEmpty = 0x00;
		memcpy(VI->NesMem, data, 0x4000);
		
		*value1 = 0xC000;
		*value2 = 0x4000;
		return 0x02;
	}
	
	DataStart = *value1 & 0x3FFF;
	if (! *value2)
		DataEnd = 0x4000;
	else
		DataEnd = DataStart + *value2;
	if (DataEnd > 0x4000)
		DataEnd = 0x4000;
	
	for (CurPos = DataStart; CurPos < DataEnd; CurPos ++)
	{
		if (VI->NesMem[CurPos] != data[CurPos])
		{
			memcpy(VI->NesMem + DataStart, data + DataStart, DataEnd - DataStart);
			return 0x01;
		}
	}
	
	return 0x00;
}

uint16_t vgm_get_chip_idx(uint8_t chip_type, uint8_t Num)
{
	// This is a small helper-function, that allows drivers/machines to
	// make writes for their chips.
	// e.g. NeoGeo CD rewrites the YM2610's PCM-RAM after changes
	uint16_t curchip;
	
	for (curchip = 0x00; curchip < MAX_VGM_CHIPS; curchip ++)
	{
		if ((VgmChip[curchip].ChipType & 0x7F) == chip_type &&
			(VgmChip[curchip].ChipType >> 7) == Num)
			return curchip;
	}
	
	return 0xFFFF;
}

static void vgm_flush_pcm(VGM_CHIP* VC)
{
	VGM_INF* VI;
	VGM_PCMCACHE* VPC;
	uint8_t SingleWrt;
	uint8_t CmdType;
	uint32_t finalsize;
	
	VI = &VgmFile[VC->VgmID];
	if (! LOG_VGM_FILE || VI->hFile == NULL)
		return;
	
	VPC = VC->PCMCache;
	if (VPC == NULL || VPC->Start == 0xFFFFFFFF || ! VPC->Pos)
		return;
	//logerror("Flushing PCM Data: Chip %02X, Addr %06X, Size %06X\n",
	//			VC->ChipType, VPC->Start, VPC->Pos);
	
	if (VPC->Pos == 0x01 && (VC->ChipType & 0x7F) != VGMC_SCSP)
		SingleWrt = 0x01;
	else
		SingleWrt = 0x00;
	
	if (SingleWrt)
	{
		switch(VC->ChipType & 0x7F)
		{
		case VGMC_RF5C68:
			CmdType = 0xC1;
			break;
		case VGMC_RF5C164:
			CmdType = 0xC2;
			break;
		default:
			CmdType = 0xFF;
			break;
		}
	}
	else
	{
		switch(VC->ChipType & 0x7F)
		{
		case VGMC_RF5C68:
			CmdType = 0xC0;
			break;
		case VGMC_RF5C164:
			CmdType = 0xC1;
			break;
		case VGMC_SCSP:
			CmdType = 0xE0;
			break;
		case VGMC_ES5503:
			CmdType = 0xE1;
			break;
		default:
			CmdType = 0xFF;
			break;
		}
	}
	
	if (SingleWrt)
	{
		// it would be a waste of space to write a data block for 1 byte of data
		fputc(CmdType, VI->hFile);		// Write Memory
		fputc((VPC->Start >> 0) & 0xFF, VI->hFile);	// offset low
		fputc((VPC->Start >> 8) & 0xFF, VI->hFile);	// offset high
		fputc(VPC->CacheData[0x00], VI->hFile);		// Data
		VI->BytesWrt += 0x04;
	}
	else
	{
		// calling vgm_write_large_data doesn't work if vgm_flush_pcm is
		// called from vgm_write_delay
		fputc(0x67, VI->hFile);
		fputc(0x66, VI->hFile);
		fputc(CmdType, VI->hFile);
		if (! (CmdType & 0x20))
		{
			finalsize = 0x02 + VPC->Pos;
			finalsize |= (VC->ChipType & 0x80) << 24;
			
			fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
			fwrite(&VPC->Start, 0x02, 0x01, VI->hFile);		// Data Address
		}
		else
		{
			finalsize = 0x04 + VPC->Pos;
			finalsize |= (VC->ChipType & 0x80) << 24;
			
			fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
			fwrite(&VPC->Start, 0x04, 0x01, VI->hFile);		// Data Address
		}
		fwrite(VPC->CacheData, 0x01, VPC->Pos, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
	}
	
	VPC->Start = 0xFFFFFFFF;
	
	return;
}

void vgm_change_rom_data(uint32_t oldsize, const void* olddata, uint32_t newsize, const void* newdata)
{
	VGM_INF* VI;
	VGM_ROM_DATA* VR;
	uint16_t curvgm;
	uint8_t curdblk;
	
	logerror("Changing ROM Data (address %p, 0x%X bytes -> address %p, 0x%X bytes) ...\n",
				olddata, oldsize, newdata, newsize);
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		VI = &VgmFile[curvgm];
		if (VI->hFile == NULL)
			continue;
		
		for (curdblk = 0x00; curdblk < VI->DataCount; curdblk ++)
		{
			VR = &VI->DataBlk[curdblk];
			if ((VR->DataSize & 0x7FFFFFFF) == oldsize && VR->Data == olddata)
			{
				logerror("Match found in VGM %02hX, Block Type %02X\n", curvgm, VR->Type);
				VR->DataSize &= 0x80000000;	// keep "chip select" bit
				VR->DataSize |= newsize;
				VR->Data = newdata;
			}
		}
	}
	
	return;
}

void vgm_dump_sample_rom(uint16_t chip_id, uint8_t type, int region)
{
	//if (region == NULL)
	//	return;
	
	//!! logerror("VGM - Dumping ROM-Region %s: size 0x%X, ptr %p\n", region->name(), memory_region_length(region), memory_region(region));
	vgm_write_large_data(chip_id, type, memory_region_length(region), 0x00, 0x00, memory_region(region));
	
	return;
}

//!!
/*void vgm_dump_sample_rom(uint16_t chip_id, uint8_t type, address_space& space)
{
	uint32_t dataSize = space.addrmask() + 1;
	const void* dataPtr = space.get_read_ptr(0);
	
	logerror("VGM - Dumping ROM-Space %s: size 0x%X, space-ptr %p\n", space.name(), dataSize, dataPtr);
	vgm_write_large_data(chip_id, type, dataSize, 0x00, 0x00, dataPtr);
	
	return;
}*/
