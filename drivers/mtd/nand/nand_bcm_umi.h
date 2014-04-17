/*****************************************************************************
* Copyright 2003 - 2009 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
#ifndef NAND_BCM_UMI_H
#define NAND_BCM_UMI_H

#include <mach/reg_umi.h>
#include <mach/reg_nand.h>
#include <cfg_global.h>

#if (CFG_GLOBAL_CHIP_FAMILY == CFG_GLOBAL_CHIP_FAMILY_BCMRING)
#define NAND_ECC_BCH (CFG_GLOBAL_CHIP_REV > 0xA0)
#else
#define NAND_ECC_BCH 0
#endif

#define CFG_GLOBAL_NAND_ECC_BCH_NUM_BYTES	13

#if NAND_ECC_BCH
#ifdef BOOT0_BUILD
#define NAND_ECC_NUM_BYTES 13
#else
#define NAND_ECC_NUM_BYTES CFG_GLOBAL_NAND_ECC_BCH_NUM_BYTES
#endif
#else
#define NAND_ECC_NUM_BYTES 3
#endif

#define NAND_DATA_ACCESS_SIZE 512

int nand_bcm_umi_bch_correct_page(uint8_t *datap, uint8_t *readEccData,
				  int numEccBytes);

static inline int nand_bcm_umi_dev_ready(void)
{
	return REG_UMI_NAND_RCSR & REG_UMI_NAND_RCSR_RDY;
}

static inline void nand_bcm_umi_wait_till_ready(void)
{
	while (nand_bcm_umi_dev_ready() == 0)
		;
}

static inline void nand_bcm_umi_hamming_enable_hwecc(void)
{
	
	REG_UMI_NAND_ECC_CSR &= ~(REG_UMI_NAND_ECC_CSR_ECC_ENABLE |
		REG_UMI_NAND_ECC_CSR_256BYTE);
	
	REG_UMI_NAND_ECC_CSR |= REG_UMI_NAND_ECC_CSR_ECC_ENABLE;
}

#if NAND_ECC_BCH
#define ECC_BITS_PER_CORRECTABLE_BIT 13

static inline void nand_bcm_umi_bch_enable_read_hwecc(void)
{
	
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID;
	
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN;
}

static inline void nand_bcm_umi_bch_enable_write_hwecc(void)
{
	
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID;
	
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_WR_EN;
}

static inline void nand_bcm_umi_bch_config_ecc(uint8_t numEccBytes)
{
	uint32_t nValue;
	uint32_t tValue;
	uint32_t kValue;
	uint32_t numBits = numEccBytes * 8;

	
	REG_UMI_BCH_CTRL_STATUS =
	    REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID |
	    REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID;

	
	tValue = (uint32_t) (numBits / ECC_BITS_PER_CORRECTABLE_BIT);

	
	nValue = (NAND_DATA_ACCESS_SIZE + numEccBytes) * 8;

	
	kValue = nValue - (tValue * ECC_BITS_PER_CORRECTABLE_BIT);

	
	REG_UMI_BCH_N = nValue;
	REG_UMI_BCH_T = tValue;
	REG_UMI_BCH_K = kValue;
}

static inline void nand_bcm_umi_bch_pause_read_ecc_calc(void)
{
	REG_UMI_BCH_CTRL_STATUS =
	    REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN |
	    REG_UMI_BCH_CTRL_STATUS_PAUSE_ECC_DEC;
}

static inline void nand_bcm_umi_bch_resume_read_ecc_calc(void)
{
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN;
}

static inline uint32_t nand_bcm_umi_bch_poll_read_ecc_calc(void)
{
	uint32_t regVal;

	do {
		
		regVal = REG_UMI_BCH_CTRL_STATUS;
	} while ((regVal & REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID) == 0);

	return regVal;
}

static inline void nand_bcm_umi_bch_poll_write_ecc_calc(void)
{
	
	while ((REG_UMI_BCH_CTRL_STATUS & REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID)
	       == 0)
		;
}

#if defined(__KERNEL__) && !defined(STANDALONE)
static inline void nand_bcm_umi_bch_read_oobEcc(uint32_t pageSize,
	uint8_t *eccCalc, int numEccBytes, uint8_t *oobp)
#else
static inline void nand_bcm_umi_bch_read_oobEcc(uint32_t pageSize,
	uint8_t *eccCalc, int numEccBytes)
#endif
{
	int eccPos = 0;
	int numToRead = 16;	

	
	if (pageSize != NAND_DATA_ACCESS_SIZE) {
		
#if defined(__KERNEL__) && !defined(STANDALONE)
		*oobp++ = REG_NAND_DATA8;
#else
		REG_NAND_DATA8;
#endif
		numToRead--;
	}

	while (numToRead > numEccBytes) {
		
#if defined(__KERNEL__) && !defined(STANDALONE)
		*oobp++ = REG_NAND_DATA8;
#else
		REG_NAND_DATA8;
#endif
		numToRead--;
	}

	if (pageSize == NAND_DATA_ACCESS_SIZE) {
		
		nand_bcm_umi_bch_resume_read_ecc_calc();

		while (numToRead > 11) {
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp = REG_NAND_DATA8;
			eccCalc[eccPos++] = *oobp;
			oobp++;
#else
			eccCalc[eccPos++] = REG_NAND_DATA8;
#endif
			numToRead--;
		}

		nand_bcm_umi_bch_pause_read_ecc_calc();

		if (numToRead == 11) {
			
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp++ = REG_NAND_DATA8;
#else
			REG_NAND_DATA8;
#endif
			numToRead--;
		}

	}
	
	nand_bcm_umi_bch_resume_read_ecc_calc();
	while (numToRead) {
#if defined(__KERNEL__) && !defined(STANDALONE)
		*oobp = REG_NAND_DATA8;
		eccCalc[eccPos++] = *oobp;
		oobp++;
#else
		eccCalc[eccPos++] = REG_NAND_DATA8;
#endif
		numToRead--;
	}
}

static inline void NAND_BCM_UMI_ECC_WRITE(int numEccBytes, int eccBytePos,
					  uint8_t *oobp, uint8_t eccVal)
{
	if (eccBytePos <= numEccBytes)
		*oobp = eccVal;
}

static inline void nand_bcm_umi_bch_write_oobEcc(uint32_t pageSize,
						 uint8_t *oobp, int numEccBytes)
{
	uint32_t eccVal = 0xffffffff;

	
	nand_bcm_umi_bch_poll_write_ecc_calc();


	if (pageSize == NAND_DATA_ACCESS_SIZE) {
		
		if (numEccBytes >= 13)
			eccVal = REG_UMI_BCH_WR_ECC_3;

		
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 15, &oobp[0],
			(eccVal >> 16) & 0xff);
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 14, &oobp[1],
			(eccVal >> 8) & 0xff);

		
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 13, &oobp[2],
			eccVal & 0xff);	

		if (numEccBytes >= 9)
			eccVal = REG_UMI_BCH_WR_ECC_2;

		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 12, &oobp[3],
			(eccVal >> 24) & 0xff);	
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 11, &oobp[4],
			(eccVal >> 16) & 0xff);	

		
	} else {
		

		
		if (numEccBytes >= 13)
			eccVal = REG_UMI_BCH_WR_ECC_3;

		
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 15, &oobp[1],
			(eccVal >> 16) & 0xff);
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 14, &oobp[2],
			(eccVal >> 8) & 0xff);

		
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 13, &oobp[3],
			eccVal & 0xff);	

		if (numEccBytes >= 9)
			eccVal = REG_UMI_BCH_WR_ECC_2;

		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 12, &oobp[4],
			(eccVal >> 24) & 0xff);	
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 11, &oobp[5],
			(eccVal >> 16) & 0xff);	
	}

	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 10, &oobp[6],
		(eccVal >> 8) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 9, &oobp[7],
		eccVal & 0xff);	

	if (numEccBytes >= 5)
		eccVal = REG_UMI_BCH_WR_ECC_1;

	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 8, &oobp[8],
		(eccVal >> 24) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 7, &oobp[9],
		(eccVal >> 16) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 6, &oobp[10],
		(eccVal >> 8) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 5, &oobp[11],
		eccVal & 0xff);	

	if (numEccBytes >= 1)
		eccVal = REG_UMI_BCH_WR_ECC_0;

	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 4, &oobp[12],
		(eccVal >> 24) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 3, &oobp[13],
		(eccVal >> 16) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 2, &oobp[14],
		(eccVal >> 8) & 0xff);	
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 1, &oobp[15],
		eccVal & 0xff);	
}
#endif

#endif 
