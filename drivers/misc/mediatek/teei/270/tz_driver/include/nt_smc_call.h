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

#ifndef SMC_CALL_H_
#define SMC_CALL_H_

#include <teei_secure_api.h>

/*This field id is fixed by arm*/
#define ID_FIELD_F_FAST_SMC_CALL		1
#define ID_FIELD_F_STANDARD_SMC_CALL		0
#define ID_FIELD_W_64				1
#define ID_FIELD_W_32				0

#define ID_FIELD_T_ARM_SERVICE			0
#define ID_FIELD_T_CPU_SERVICE			1
#define ID_FIELD_T_SIP_SERVICE			2
#define ID_FIELD_T_OEM_SERVICE			3
#define ID_FIELD_T_STANDARD_SERVICE		4
/*TA Call 48-49*/
#define ID_FIELD_T_TA_SERVICE0			48
#define ID_FIELD_T_TA_SERVICE1			49
/*TOS Call 50-63*/
#define ID_FIELD_T_TRUSTED_OS_SERVICE0		50
#define ID_FIELD_T_TRUSTED_OS_SERVICE1		51
#define ID_FIELD_T_TRUSTED_OS_SERVICE2		52
#define ID_FIELD_T_TRUSTED_OS_SERVICE3		53

#define MAKE_SMC_CALL_ID(F, W, T, FN) (((F)<<31)|((W)<<30)|((T)<<24)|(FN))

#define SMC_CALL_RTC_OK                 0x0
#define SMC_CALL_RTC_UNKNOWN_FUN        0xFFFFFFFF
#define SMC_CALL_RTC_MONITOR_NOT_READY  0xFFFFFFFE

#define SMC_CALL_INTERRUPTED_IRQ	0x54
#define SMC_CALL_DONE			0x45

#define N_GET_T_FP_DEVICE_ID     \
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_64, ID_FIELD_T_TRUSTED_OS_SERVICE2, 11)
/*Child type command type for soter to recognize*/
#define N_ACK_T_INVOKE_DRV_CMD	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_64, ID_FIELD_T_TRUSTED_OS_SERVICE3, 5)
#define N_INVOKE_T_LOAD_TEE_CMD	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_64, ID_FIELD_T_TRUSTED_OS_SERVICE3, 6)
#define N_INVOKE_T_NQ_CMD		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_64, ID_FIELD_T_TRUSTED_OS_SERVICE3, 2)

/*For non-secure side Fast Call*/
#define N_SWITCH_TO_T_OS_STAGE2	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 0)
#define N_GET_PARAM_IN		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 1)
#define N_INIT_T_FC_BUF	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 2)
#define N_INVOKE_T_FAST_CALL	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 3)
#define NT_DUMP_STATE		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 4)
#define N_ACK_T_FOREGROUND	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 5)
#define N_ACK_T_BACKSTAGE	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 6)
#define N_INIT_T_BOOT_STAGE1	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 7)
#define N_SWITCH_CORE \
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 8)
#define N_GET_NON_IRQ_NUM      \
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 9)
#define N_GET_SE_OS_STATE    \
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 10)
#define N_GET_T_FP_DEVICE_ID_32     \
		MAKE_SMC_CALL_ID(ID_FIELD_F_FAST_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE2, 11)

/*For non-secure side Standard Call*/
#define NT_SCHED_T		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 0)
#define N_INVOKE_T_SYS_CTL	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 1)
#define N_INVOKE_T_NQ		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 2)
#define N_INVOKE_T_DRV		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 3)
#define N_RAISE_T_EVENT		\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 4)
#define N_ACK_T_INVOKE_DRV	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 5)
#define N_INVOKE_T_LOAD_TEE	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 6)
#define N_ACK_T_LOAD_IMG	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 7)
#define NT_SCHED_T_FIQ	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 8)
#define NT_CANCEL_T_TUI	\
		MAKE_SMC_CALL_ID(ID_FIELD_F_STANDARD_SMC_CALL, \
		ID_FIELD_W_32, ID_FIELD_T_TRUSTED_OS_SERVICE3, 9)

#ifdef CONFIG_ARM64
static inline void get_t_device_id(uint64_t *rtc0)
{
	uint64_t temp[4];

	__asm__ volatile(
			/* ".arch_extension sec\n" */
			"mov x0, %[fun_id]\n\t"
			"mov x1, #0\n\t"
			"mov x2, #0\n\t"
			"mov x3, #0\n\t"
			"smc 0\n\t"
			"nop\n\t"
			"str x1, [%[temp], #0]\n\t"
			: :
			[fun_id] "r" (N_GET_T_FP_DEVICE_ID), [temp] "r" (temp)
			: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",
			"x9", "x10", "x11", "x12",
			"x13", "x14", "x15", "x16", "x17", "memory");

	*rtc0 = temp[0];

}
#else
static inline void get_t_device_id(uint64_t *p0)
{
	uint32_t temp, temp2;

	temp = (uint32_t)(*p0);
	temp2 = teei_secure_call(N_GET_T_FP_DEVICE_ID_32, temp, 0, 0);
	*p0 = temp2;
}
#endif
#endif /* SMC_CALL_H_ */
