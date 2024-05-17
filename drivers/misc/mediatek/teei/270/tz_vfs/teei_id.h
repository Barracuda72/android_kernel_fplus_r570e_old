/*
 * Copyright (c) 2015-2017 MICROTRUST Incorporated
 * Copyright (C) 2019 XiaoMi, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __VFS_TEEI_ID_H_
#define __VFS_TEEI_ID_H_

#include <asm/cacheflush.h>

#define SMC_ENOMEM          7
#define SMC_EOPNOTSUPP      6
#define SMC_EINVAL_ADDR     5
#define SMC_EINVAL_ARG      4
#define SMC_ERROR           3
#define SMC_INTERRUPTED     2
#define SMC_PENDING         1
#define SMC_SUCCESS         0
/*extern void __flush_dcache_area(void *addr, size_t len);*/
/**
 * @brief Encoding data type
 */
enum teei_enc_data_type {
	TEEI_ENC_INVALID_TYPE = 0,
	TEEI_ENC_UINT32,
	TEEI_ENC_ARRAY,
	TEEI_MEM_REF,
	TEEI_SECURE_MEM_REF
};
/**
 * @brief Command ID's for global service
 */
enum _global_cmd_id {
	TEEI_GLOBAL_CMD_ID_INVALID = 0x0,
	TEEI_GLOBAL_CMD_ID_BOOT_ACK,
	/* add by lodovico */
	TEEI_GLOBAL_CMD_ID_INIT_CONTEXT,
	/* add end */
	TEEI_GLOBAL_CMD_ID_OPEN_SESSION,
	TEEI_GLOBAL_CMD_ID_CLOSE_SESSION,
	TEEI_GLOBAL_CMD_ID_RESUME_ASYNC_TASK,
	TEEI_GLOBAL_CMD_ID_UNKNOWN         = 0x7FFFFFFE,
	TEEI_GLOBAL_CMD_ID_MAX             = 0x7FFFFFFF
};

/* add by lodovico */
/* void printff(); */

#if 1
int service_smc_call(u32 teei_cmd_type, u32 dev_file_id, u32 svc_id,
			u32 cmd_id, u32 context, u32 enc_id,
			const void *cmd_buf,
			size_t cmd_len,
			void *resp_buf,
			size_t resp_len,
			const void *meta_data,
			int *ret_resp_len,
			void *wq,
			void *arg_lock, int *error_code);
#endif

enum teei_cmd_type {
	TEEI_CMD_TYPE_INVALID = 0x0,
	TEEI_CMD_TYPE_SOCKET_INIT,
	TEEI_CMD_TYPE_INITIALIZE_CONTEXT,
	TEEI_CMD_TYPE_FINALIZE_CONTEXT,
	TEEI_CMD_TYPE_OPEN_SESSION,
	TEEI_CMD_TYPE_CLOSE_SESSION,
	TEEI_CMD_TYPE_INVOKE_COMMAND,
	TEEI_CMD_TYPE_UNKNOWN         = 0x7FFFFFFE,
	TEEI_CMD_TYPE_MAX             = 0x7FFFFFFF
};

#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))

#define Cache_line_size 32
/****************************************************************
 * @brief:
 *     Flush_Dcache_By_Area
 * @param:
 *     start - mva start
 *     end   - mva end
 * @return:
 * ***************************************************************/
static inline void Flush_Dcache_By_Area(unsigned long start, unsigned long end)
{

}
/******************************************************************
 * @brief:
 *     Invalidate_Dcache_By_Area
 * @param:
 *     start - mva start
 *     end   - mva end
 * @return:
 * *****************************************************************/
static inline void Invalidate_Dcache_By_Area(unsigned long start,
							unsigned long end)
{

}

/* add end */
#endif /* __OPEN_OTZ_ID_H_ */
