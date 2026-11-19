/* The SH-3 register state: MAME 0.264's sh_common_execution::internal_sh2_state
   (src/devices/cpu/sh/sh.h, BSD-3-Clause), field for field, as a plain C
   struct so that the interpreter (C++) and the recompiled code (C, compiled
   by TinyCC) share it. */
#ifndef DFK_SH3_REGS_H
#define DFK_SH3_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct internal_sh2_state
{
	uint32_t  pc;
	uint32_t  pr;
	uint32_t  sr;
	uint32_t  mach;
	uint32_t  macl;
	uint32_t  r[16];
	uint32_t  ea;

	uint32_t  pending_irq;
	uint32_t  pending_nmi;
	int32_t   irqline;
	uint32_t  evec;               // exception vector for DRC
	uint32_t  irqsr;              // IRQ-time old SR for DRC
	uint32_t  target;             // target for jmp/jsr/etc so the delay slot can't kill it
	int     internal_irq_level;
	int     icount;
	uint8_t   sleep_mode;
	uint32_t  arg0;              /* print_debug argument 1 */
	uint32_t  arg1;
	uint32_t  gbr;
	uint32_t  vbr;

	uint32_t  m_delay;

	uint32_t  m_ppc;
	uint32_t  m_spc;
	uint32_t  m_ssr;
	uint32_t  m_rbnk[2][8];
	uint32_t  m_sgr;
	uint32_t  m_fr[16];
	uint32_t  m_xf[16];
	uint32_t  m_cpu_off;
	uint32_t  m_pending_irq;
	uint32_t  m_test_irq;
	uint32_t  m_fpscr;
	uint32_t  m_fpul;
	uint32_t  m_dbr;

	int     m_frt_input;
	int     m_fpu_sz;
	int     m_fpu_pr;
};

#ifdef __cplusplus
}
#endif

#endif
