// MAME 0.264's SH-3 (big-endian) interpreter as one flat class.
//
// MAME: src/devices/cpu/sh/sh.h, sh4.h, sh4comn.h, sh4dmac.h (BSD-3-Clause,
// R. Belmont, David Haywood and others).  The constants, enums and the
// inline functions below are copied from those headers; the member functions
// are MAME's, imported unchanged by tools/import_mame_sh3.py into
// sh3_mame_defs.inc (declared in sh3_mame_decls.inc).  Left out: the DRC, the
// debugger and state-registration code, the SH-4-only class.
#ifndef DFK_SH3_H
#define DFK_SH3_H

#include "sh3_shim.h"
#include "sh3_regs.h"
#include "sh4regs.h"
#include "sh3comn.h"
#include "../emu/state_io.h"

// ---------------------------------------------------------------- sh.h
#define BUSY_LOOP_HACKS 0

#define CPU_TYPE_SH1    (0)
#define CPU_TYPE_SH2    (1)
#define CPU_TYPE_SH3    (2)
#define CPU_TYPE_SH4    (3)

#define REG_N  ((opcode >> 8) & 15)
#define REG_M  ((opcode >> 4) & 15)

/* Bits in SR */
#define SH_T   0x00000001
#define SH_S   0x00000002
#define SH_I   0x000000f0
#define SH_Q   0x00000100
#define SH_M   0x00000200

#define SH_FLAGS   (SH_M|SH_Q|SH_I|SH_S|SH_T)

/* SR shift values */
#define T_SHIFT 0
#define S_SHIFT 1
#define I_SHIFT 4
#define Q_SHIFT 8
#define M_SHIFT 9

// ---------------------------------------------------------------- sh4comn.h
#define EXPPRI(pl, po, p, n)    (((4 - (pl)) << 24) | ((15 - (po)) << 16) | ((p) << 8) | (255 - (n)))
#define NMIPRI()                EXPPRI(3, 0, 16, SH4_INTC_NMI)
#define INTPRI(p, n)            EXPPRI(4, 2, p, n)

#define FP_RS(r) m_sh2_state->m_fr[(r)] // binary representation of single precision floating point register r
#define FP_RFS(r) *( (float  *)(m_sh2_state->m_fr + (r)) ) // single precision floating point register r
#define FP_RFD(r) *( (double *)(m_sh2_state->m_fr + (r)) ) // double precision floating point register r
#define FP_XS(r) m_sh2_state->m_xf[(r)] // binary representation of extended single precision floating point register r
#define FP_XFS(r) *( (float  *)(m_sh2_state->m_xf + (r)) ) // single precision extended floating point register r
#define FP_XFD(r) *( (double *)(m_sh2_state->m_xf + (r)) ) // double precision extended floating point register r
#ifdef LSB_FIRST
#define FP_RS2(r) m_sh2_state->m_fr[(r) ^ m_sh2_state->m_fpu_pr]
#define FP_RFS2(r) *( (float  *)(m_sh2_state->m_fr + ((r) ^ m_sh2_state->m_fpu_pr)) )
#define FP_XS2(r) m_sh2_state->m_xf[(r) ^ m_sh2_state->m_fpu_pr]
#define FP_XFS2(r) *( (float  *)(m_sh2_state->m_xf + ((r) ^ m_sh2_state->m_fpu_pr)) )
#endif

enum
{
	ICF  = 0x00800000,
	OCFA = 0x00080000,
	OCFB = 0x00040000,
	OVF  = 0x00020000
};

#define FD  0x00008000
#define BL  0x10000000
#define sRB 0x20000000
#define MD  0x40000000

/* 29 bits */
#define SH34_AM  0x1fffffff

#define SH34_FLAGS   (MD | sRB | BL | FD | SH_M | SH_Q | SH_I | SH_S | SH_T)

/* Bits in FPSCR */
#define RM  0x00000003
#define DN  0x00040000
#define PR  0x00080000
#define SZ  0x00100000
#define FR  0x00200000

// ---------------------------------------------------------------- sh4dmac.h
#define CHCR_SSA    0xe0000000
#define CHCR_STC    0x10000000
#define CHCR_DSA    0x0e000000
#define CHCR_DTC    0x01000000
#define CHCR_DS     0x00080000
#define CHCR_RL     0x00040000
#define CHCR_AM     0x00020000
#define CHCR_AL     0x00010000
#define CHCR_DM     0x0000c000
#define CHCR_SM     0x00003000
#define CHCR_RS     0x00000f00
#define CHCR_TM     0x00000080
#define CHCR_TS     0x00000070
#define CHCR_IE     0x00000004
#define CHCR_TE     0x00000002
#define CHCR_DE     0x00000001

#define DMAOR_DDT   0x8000
#define DMAOR_PR    0x0300
#define DMAOR_COD   0x0010
#define DMAOR_AE    0x0004
#define DMAOR_NMIF  0x0002
#define DMAOR_DME   0x0001

// ---------------------------------------------------------------- sh4.h
#define SH4_INT_NONE    -1

enum
{
	SH4_IRL0=0, SH4_IRL1, SH4_IRL2, SH4_IRL3, SH4_IRLn
};

enum
{
	SH4_INTC_NMI=23,
	SH4_INTC_IRLn0,
	SH4_INTC_IRLn1,
	SH4_INTC_IRLn2,
	SH4_INTC_IRLn3,
	SH4_INTC_IRLn4,
	SH4_INTC_IRLn5,
	SH4_INTC_IRLn6,
	SH4_INTC_IRLn7,
	SH4_INTC_IRLn8,
	SH4_INTC_IRLn9,
	SH4_INTC_IRLnA,
	SH4_INTC_IRLnB,
	SH4_INTC_IRLnC,
	SH4_INTC_IRLnD,
	SH4_INTC_IRLnE,

	SH4_INTC_IRL0,
	SH4_INTC_IRL1,
	SH4_INTC_IRL2,
	SH4_INTC_IRL3,

	SH4_INTC_HUDI,
	SH4_INTC_GPOI,

	SH4_INTC_DMTE0,
	SH4_INTC_DMTE1,
	SH4_INTC_DMTE2,
	SH4_INTC_DMTE3,

	SH4_INTC_DMTE4,
	SH4_INTC_DMTE5,
	SH4_INTC_DMTE6,
	SH4_INTC_DMTE7,

	SH4_INTC_DMAE,

	SH4_INTC_TUNI3,
	SH4_INTC_TUNI4,
	SH4_INTC_TUNI0,
	SH4_INTC_TUNI1,
	SH4_INTC_TUNI2,
	SH4_INTC_TICPI2,
	SH4_INTC_ATI,
	SH4_INTC_PRI,
	SH4_INTC_CUI,
	SH4_INTC_SCI1ERI,
	SH4_INTC_SCI1RXI,
	SH4_INTC_SCI1TXI,
	SH4_INTC_SCI1TEI,
	SH4_INTC_SCIFERI,
	SH4_INTC_SCIFRXI,
	SH4_INTC_SCIFBRI,
	SH4_INTC_SCIFTXI,
	SH4_INTC_ITI,
	SH4_INTC_RCMI,
	SH4_INTC_ROVI
};

#define SH4_FPU_PZERO 0
#define SH4_FPU_NZERO 1
#define SH4_FPU_DENORM 2
#define SH4_FPU_NORM 3
#define SH4_FPU_PINF 4
#define SH4_FPU_NINF 5
#define SH4_FPU_qNaN 6
#define SH4_FPU_sNaN 7

enum
{
	SH4_IOPORT_16=8*0,
	SH4_IOPORT_4=8*1,
	SH4_IOPORT_DMA=8*2,
	// future use
	SH4_IOPORT_SCI=8*3,
	SH4_IOPORT_SCIF=8*4
};

struct sh4_device_dma
{
	uint32_t length;
	uint32_t size;
	void *buffer;
	int channel;
};

struct sh4_ddt_dma
{
	uint32_t source;
	uint32_t length;
	uint32_t size;
	uint32_t destination;
	void *buffer;
	int direction;
	int channel;
	int mode;
};

// ---------------------------------------------------------------- the device

class sh3_device
{
public:
	sh3_device();
	void device_start(uint32_t clock);   // sh34_base_device::device_start (interpreter part)
	void save(state_writer &w) const;
	void load(state_reader &r);

	void set_md(int bit, int md) { m_md[bit] = md; }
	void set_sh4_clock(int clock) { m_clock = clock; }

	// the board's buses and scheduler interface
	sh3_space m_program_space, m_io_space;
	kemu::exec_device *m_exec = nullptr;
	int &icount() { return m_sh2_state->icount; }
	uint32_t pc() const { return m_sh2_state->pc; }   // state "PC"
	internal_sh2_state *state() { return m_sh2_state; }

	// recompiled code: called at each instruction boundary with the
	// instruction's address; returns the native code entered there or nullptr
	typedef void (*native_fn)(void *dev, internal_sh2_state *, int entry);
	native_fn (*m_native_lookup)(void *ctx, uint32_t pc, int *entry) = nullptr;
	void *m_native_ctx = nullptr;
	uint64_t m_native_insns = 0, m_interp_insns = 0, m_native_calls = 0;
	uint64_t m_native_cycles = 0, m_interp_cycles = 0;   // statistics: cycles spent in each
	// (a cut-short timeslice also takes cycles off icount; they count where it happened)

	// device_execute_interface glue
	void execute_set_input_glue(int line, int state) { execute_set_input(line, state); }
	void spin_until_time(const attotime &d) { m_exec->spin_until_time(d); }

	void execute_run();   // sh3be_device::execute_run, in sh3_core.cpp
#include "sh3_mame_decls.inc"

	// ---- sh4.h inline members
	void pulse_frt_input() { set_frt_input(ASSERT_LINE); set_frt_input(CLEAR_LINE); }

	inline void sh4_check_pending_irq(const char *message) // look for highest priority active exception and handle it
	{
		m_willjump = 0; // for the DRC
		int irq = 0;
		int z = -1;
		for (int a = 0; a <= SH4_INTC_ROVI; a++)
		{
			if (m_exception_requesting[a])
			{
				if ((int)m_exception_priority[a] > z)
				{
					z = m_exception_priority[a];
					irq = a;
				}
			}
		}
		if (z >= 0)
		{
			sh4_exception(message, irq);
		}
	}

	uint32_t sh4_handle_sar0_addr_r(uint32_t mem_mask) { return m_SH4_SAR0; }
	uint32_t sh4_handle_sar1_addr_r(uint32_t mem_mask) { return m_SH4_SAR1; }
	uint32_t sh4_handle_sar2_addr_r(uint32_t mem_mask) { return m_SH4_SAR2; }
	uint32_t sh4_handle_sar3_addr_r(uint32_t mem_mask) { return m_SH4_SAR3; }
	uint32_t sh4_handle_dar0_addr_r(uint32_t mem_mask) { return m_SH4_DAR0; }
	uint32_t sh4_handle_dar1_addr_r(uint32_t mem_mask) { return m_SH4_DAR1; }
	uint32_t sh4_handle_dar2_addr_r(uint32_t mem_mask) { return m_SH4_DAR2; }
	uint32_t sh4_handle_dar3_addr_r(uint32_t mem_mask) { return m_SH4_DAR3; }
	uint32_t sh4_handle_dmatcr0_addr_r(uint32_t mem_mask) { return m_SH4_DMATCR0; }
	uint32_t sh4_handle_dmatcr1_addr_r(uint32_t mem_mask) { return m_SH4_DMATCR1; }
	uint32_t sh4_handle_dmatcr2_addr_r(uint32_t mem_mask) { return m_SH4_DMATCR2; }
	uint32_t sh4_handle_dmatcr3_addr_r(uint32_t mem_mask) { return m_SH4_DMATCR3; }
	uint32_t sh4_handle_chcr0_addr_r(uint32_t mem_mask) { return m_SH4_CHCR0; }
	uint32_t sh4_handle_chcr1_addr_r(uint32_t mem_mask) { return m_SH4_CHCR1; }
	uint32_t sh4_handle_chcr2_addr_r(uint32_t mem_mask) { return m_SH4_CHCR2; }
	uint32_t sh4_handle_chcr3_addr_r(uint32_t mem_mask) { return m_SH4_CHCR3; }
	uint32_t sh4_handle_dmaor_addr_r(uint32_t mem_mask) { return m_SH4_DMAOR; }

	// ---- framework stand-ins used by the MAME bodies
	sh3_machine_stub m_machine_stub;
	sh3_machine_stub &machine() { return m_machine_stub; }
	const char *tag() const { return ":maincpu"; }
	void (*m_trace_hook)(sh3_device *, offs_t) = nullptr;
	void (*m_interp_profile)(sh3_device *, uint32_t pc) = nullptr;   // development   // instruction trace (tests)
	void debugger_instruction_hook(offs_t pc) { if (m_trace_hook) m_trace_hook(this, pc); }
	int standard_irq_callback(int line, offs_t) { return m_exec->standard_irq_callback(line); }
	void set_input_line(int line, int state) { m_exec->set_input_line(line, state); }
	attotime cycles_to_attotime(uint64_t cycles) const { return attotime(m_exec->cycles_to_attotime(cycles)); }
	void execute_run_drc() {}
	struct pr16_fn {
		sh3_device *d;
		u16 operator()(offs_t a) const { return d->m_program->read_word(a); }
	} m_pr16 { this };

	// ---- sh.h members
	internal_sh2_state m_state_storage {};
	internal_sh2_state *m_sh2_state = &m_state_storage;
	sh3_space *m_program = &m_program_space;
	int m_cpu_type = CPU_TYPE_SH3;
	uint32_t m_am = SH34_AM;
	bool m_isdrc = false;
	uint8_t m_cache_dirty = 0;

	// ---- sh4.h members
	int m_md[9] = {};
	int m_clock = 0;
	int m_mmuhack = 1;
	uint32_t  m_exception_priority[128];
	int     m_exception_requesting[128];
	int8_t    m_irq_line_state[17];
	sh3_space *m_internal = &m_program_space;
	sh3_space *m_io = &m_io_space;

	// sh4 internal
	uint32_t m_m[16384];

	// timer regs handled manually for reuse
	uint32_t m_SH4_TSTR;
	uint32_t m_SH4_TCNT0;
	uint32_t m_SH4_TCNT1;
	uint32_t m_SH4_TCNT2;
	uint32_t m_SH4_TCR0;
	uint32_t m_SH4_TCR1;
	uint32_t m_SH4_TCR2;
	uint32_t m_SH4_TCOR0;
	uint32_t m_SH4_TCOR1;
	uint32_t m_SH4_TCOR2;
	uint32_t m_SH4_TOCR;
	uint32_t m_SH4_TCPR2;

	// INTC regs
	uint32_t m_SH4_IPRA;
	uint32_t m_SH4_IPRC;

	// DMAC regs
	uint32_t m_SH4_SAR0;
	uint32_t m_SH4_SAR1;
	uint32_t m_SH4_SAR2;
	uint32_t m_SH4_SAR3;
	uint32_t m_SH4_DAR0;
	uint32_t m_SH4_DAR1;
	uint32_t m_SH4_DAR2;
	uint32_t m_SH4_DAR3;
	uint32_t m_SH4_CHCR0;
	uint32_t m_SH4_CHCR1;
	uint32_t m_SH4_CHCR2;
	uint32_t m_SH4_CHCR3;
	uint32_t m_SH4_DMATCR0;
	uint32_t m_SH4_DMATCR1;
	uint32_t m_SH4_DMATCR2;
	uint32_t m_SH4_DMATCR3;
	uint32_t m_SH4_DMAOR;

	int8_t    m_nmi_line_state;

	int     m_irln;
	int     m_internal_irq_level;
	int     m_internal_irq_vector;

	emu_timer *m_dma_timer[4] = {};
	emu_timer *m_refresh_timer = nullptr;
	emu_timer *m_rtc_timer = nullptr;
	emu_timer *m_timer[3] = {};
	uint32_t  m_refresh_timer_base;
	int     m_dma_timer_active[4];
	uint32_t  m_dma_source[4];
	uint32_t  m_dma_destination[4];
	uint32_t  m_dma_count[4];
	int     m_dma_wordsize[4];
	int     m_dma_source_increment[4];
	int     m_dma_destination_increment[4];
	int     m_dma_mode[4];

	int     m_is_slave;
	int     m_cpu_clock;
	int     m_bus_clock;
	int     m_pm_clock;
	int     m_ioport16_pullup;
	int     m_ioport16_direction;
	int     m_ioport4_pullup;
	int     m_ioport4_direction;

	int     m_willjump;

	bool m_sh4_mmu_enabled;

	// sh3 internal
	uint32_t  m_sh3internal_upper[0x3000/4];
	uint32_t  m_sh3internal_lower[0x1000];

	uint64_t m_debugger_temp;
};

#endif
