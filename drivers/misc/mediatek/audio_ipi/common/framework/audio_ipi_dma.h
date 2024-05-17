/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */


#ifndef AUDUI_IPI_DMA_H
#define AUDUI_IPI_DMA_H

#include <linux/types.h>




/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */


/*
 * =============================================================================
 *                     struct def
 * =============================================================================
 */

enum { /* audio_ipi_dma_path_t */
	AUDIO_IPI_DMA_AP_TO_SCP = 0,
	AUDIO_IPI_DMA_SCP_TO_AP = 1,
	NUM_AUDIO_IPI_DMA_PATH
};


struct aud_ptr_t {
	union {
		uint8_t *addr;
		unsigned long addr_val; /* the value of address */

		uint32_t dummy[2];      /* work between 32/64 bit environment */
	};
};



/*
 * =============================================================================
 *                     ref struct
 * =============================================================================
 */

struct ipi_msg_t;


/*
 * =============================================================================
 *                     init
 * =============================================================================
 */

/* kernel */
int init_audio_ipi_dma(const uint8_t task);

int deinit_audio_ipi_dma(const uint8_t task);

/* dsp */
int audio_ipi_dma_init_dsp(const uint8_t task);



/*
 * =============================================================================
 *                     alloc/free
 * =============================================================================
 */

int audio_ipi_dma_alloc(const uint8_t task,
			struct aud_ptr_t *phy_addr,
			const uint32_t size);

int audio_ipi_dma_free(const uint8_t task,
			struct aud_ptr_t *phy_addr,
			const uint32_t size);


void *get_audio_ipi_dma_vir_addr(uint32_t opendsp_id,
				unsigned long phy_addr_val);


/*
 * =============================================================================
 *                     data retion
 * =============================================================================
 */

int audio_ipi_dma_alloc_region(const uint8_t task,
			       const uint32_t ap_to_dsp_size,
			       const uint32_t dsp_to_ap_size);

int audio_ipi_dma_free_region(const uint8_t task);
int audio_ipi_dma_free_region_all_task(void);



int audio_ipi_dma_write_region(const uint8_t task,
			       const void *data_buf,
			       uint32_t data_size,
			       uint32_t *write_idx);

int audio_ipi_dma_read_region(const uint8_t task,
			      void *data_buf,
			      uint32_t data_size,
			      uint32_t read_idx);

int audio_ipi_dma_drop_region(const uint8_t task,
			      uint32_t data_size,
			      uint32_t read_idx);






/*
 * =============================================================================
 *                     data retion
 * =============================================================================
 */

int audio_ipi_dma_msg_to_hal(struct ipi_msg_t *p_ipi_msg);

size_t audio_ipi_dma_msg_read(void __user *buf, size_t count);



#endif /* end of AUDUI_IPI_DMA_H */

