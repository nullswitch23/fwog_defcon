/*-----------------------------------------------------------------------/
/  Low level disk interface modlue include file   (C)ChaN, 2019          /
/-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

/* ---- VENDOR PATCH (wiliOGBsp). See fs/ff/README.md. ----
 *
 * WAS: a private block re-declaring UINT/BYTE/WORD/DWORD/QWORD/WCHAR/FSIZE_t/
 * LBA_t here, naming uint16_t/uint32_t/uint64_t without including <stdint.h>.
 * NOW: the types come from ff.h, which is where upstream FatFs R0.15 gets
 * them and which is the only place they can come from correctly.
 *
 * Two separate bugs made the original block unusable outside the reference's
 * exact build:
 *
 *   1. Nothing here defined uint16_t. The reference only compiled because
 *      every one of its translation units happened to reach <stdint.h>
 *      through some other include first.
 *
 *   2. Even with <stdint.h>, the types DISAGREED with ff.h's. ff.h picks its
 *      integer types by preprocessor branch, and on a Windows host (the CTest
 *      tree builds under MinGW) it takes its `#if defined(_WIN32)` branch and
 *      gets DWORD from windows.h as `unsigned long` -- while this block said
 *      `uint32_t`, i.e. `unsigned int`. Both are 32 bits and they are still
 *      different TYPES, so ff.c failed to compile against its own header.
 *
 * The `#ifndef BYTE` guard the block sat behind never fired either: BYTE is a
 * typedef, not a macro. It was always processed, and was legal only because C
 * permits repeating a typedef with the SAME underlying type -- which is
 * exactly the condition that broke.
 *
 * On the ARM target neither bug fires, so this patch changes nothing about
 * what the board runs; it is what lets the host test tree compile FatFs at
 * all, which is what gives this package its coverage. */
#include "ff.h"

/* Status of Disk Functions */
typedef BYTE	DSTATUS;

/* Results of Disk Functions */
typedef enum {
	RES_OK = 0,		/* 0: Successful */
	RES_ERROR,		/* 1: R/W Error */
	RES_WRPRT,		/* 2: Write Protected */
	RES_NOTRDY,		/* 3: Not Ready */
	RES_PARERR		/* 4: Invalid Parameter */
} DRESULT;


/*---------------------------------------*/
/* Prototypes for disk control functions */


DSTATUS disk_initialize (BYTE pdrv);
DSTATUS disk_status (BYTE pdrv);
DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff);


/* Disk Status Bits (DSTATUS) */

#define STA_NOINIT		0x01	/* Drive not initialized */
#define STA_NODISK		0x02	/* No medium in the drive */
#define STA_PROTECT		0x04	/* Write protected */


/* Command code for disk_ioctrl fucntion */

/* Generic command (Used by FatFs) */
#define CTRL_SYNC			0	/* Complete pending write process (needed at FF_FS_READONLY == 0) */
#define GET_SECTOR_COUNT	1	/* Get media size (needed at FF_USE_MKFS == 1) */
#define GET_SECTOR_SIZE		2	/* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
#define GET_BLOCK_SIZE		3	/* Get erase block size (needed at FF_USE_MKFS == 1) */
#define CTRL_TRIM			4	/* Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1) */

/* Generic command (Not used by FatFs) */
#define CTRL_POWER			5	/* Get/Set power status */
#define CTRL_LOCK			6	/* Lock/Unlock media removal */
#define CTRL_EJECT			7	/* Eject media */
#define CTRL_FORMAT			8	/* Create physical format on the media */

/* MMC/SDC specific ioctl command */
#define MMC_GET_TYPE		10	/* Get card type */
#define MMC_GET_CSD			11	/* Get CSD */
#define MMC_GET_CID			12	/* Get CID */
#define MMC_GET_OCR			13	/* Get OCR */
#define MMC_GET_SDSTAT		14	/* Get SD status */
#define ISDIO_READ			55	/* Read data form SD iSDIO register */
#define ISDIO_WRITE			56	/* Write data to SD iSDIO register */
#define ISDIO_MRITE			57	/* Masked write data to SD iSDIO register */

/* ATA/CF specific ioctl command */
#define ATA_GET_REV			20	/* Get F/W revision */
#define ATA_GET_MODEL		21	/* Get model name */
#define ATA_GET_SN			22	/* Get serial number */

#ifdef __cplusplus
}
#endif

#endif
