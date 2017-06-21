/*
 * File:
 * process_server_macros.h
 *
 * Description:
 * 	this file provides the architecture specific macro and structures of the
 *  helper functionality implementation of the process server
 *
 * Created on:
 * 	Sep 19, 2014
 *
 * Author:
 * 	Sharath Kumar Bhat, SSRG, VirginiaTech
 *
 */

#ifndef PROCESS_SERVER_ARCH_MACROS_H_
#define PROCESS_SERVER_ARCH_MACROS_H_

#include <popcorn/bundle.h>

struct regset_x86_64 {
	/* Program counter/instruction pointer */
	uint64_t rip;

	/* General purpose registers */
	uint64_t rax, rdx, rcx, rbx,
			 rsi, rdi, rbp, rsp,
			 r8, r9, r10, r11,
			 r12, r13, r14, r15;

	/* Multimedia-extension (MMX) registers */
	uint64_t mmx[8];

	/* Streaming SIMD Extension (SSE) registers */
	unsigned __int128 xmm[16];

	/* x87 floating point registers */
	long double st[8];

	/* Segment registers */
	uint32_t cs, ss, ds, es, fs, gs;

	/* Flag register */
	uint64_t rflags;
};

struct regset_aarch64 {
	/* Stack pointer & program counter */
	uint64_t sp;
	uint64_t pc;

	/* General purpose registers */
	uint64_t x[31];

	/* FPU/SIMD registers */
	unsigned __int128 v[32];
};

struct regset_powerpc {
	uint64_t dummy;
};

struct regset_sparc {
	uint64_t dummy;
};

struct field_arch {
	/* Segmentations
	unsigned short thread_es;
	unsigned short thread_ds;
	unsigned long thread_fs;
	unsigned long thread_gs;
	*/
	unsigned long tls;

	union {
		unsigned long regsets;
		struct regset_x86_64 regs_x86;
		struct regset_aarch64 regs_aarch;
		struct regset_powerpc regs_powerpc;
		struct regset_sparc regs_sparc;
	};
};

static inline size_t regset_size(int arch) {
	const size_t sizes[] = {
		sizeof(struct regset_x86_64),
		sizeof(struct regset_aarch64),
		sizeof(struct regset_powerpc),
		sizeof(struct regset_sparc),
	};
	BUG_ON(arch < 0 || arch >= POPCORN_ARCH_UNKNOWN);
	return sizes[arch];
}

#endif
