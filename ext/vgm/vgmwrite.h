#ifndef __VGMWRITE_H__
#define __VGMWRITE_H__

#include "driver.h"

void vgm_start(struct RunningMachine *machine);
void vgm_stop(void);
UINT16 vgm_open(UINT8 chip_type, int clock);
void vgm_header_set(UINT16 chip_id, UINT8 attr, UINT32 data);
void vgm_write(UINT16 chip_id, UINT8 port, UINT16 r, UINT8 v);
void vgm_write_large_data(UINT16 chip_id, UINT8 type, UINT32 datasize, UINT32 value1, UINT32 value2, const void* data);

// VGM Chip Constants
// v1.00
#define VGMC_SN76496	0x00
#define VGMC_YM2413	0x01
#define VGMC_YM2612	0x02
#define VGMC_YM2151	0x03
// v1.51
#define VGMC_SEGAPCM	0x04
#define VGMC_RF5C68	0x05
#define VGMC_YM2203	0x06
#define VGMC_YM2608	0x07
#define VGMC_YM2610	0x08
#define VGMC_YM3812	0x09
#define VGMC_YM3526	0x0A
#define VGMC_Y8950	0x0B
#define VGMC_YMF262	0x0C
#define VGMC_YMF278B	0x0D
#define VGMC_YMF271	0x0E
#define VGMC_YMZ280B	0x0F
#define VGMC_T6W28	0x10	// note: emulated via 2xSN76496
#define VGMC_RF5C164	0x11
#define VGMC_PWM	0x12
#define VGMC_AY8910	0x13
// v1.61
#define VGMC_GBSOUND	0x14
#define VGMC_NESAPU	0x15
#define VGMC_MULTIPCM	0x16
#define VGMC_UPD7759	0x17
#define VGMC_OKIM6258	0x18
#define VGMC_OKIM6295	0x19
#define VGMC_OKIM6376	0x1A

#endif /* __VGMWRITE_H__ */
