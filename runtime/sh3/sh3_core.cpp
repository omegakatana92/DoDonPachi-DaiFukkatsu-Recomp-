// MAME 0.264's SH-3 interpreter (see sh3.h): the imported member functions,
// plus device_start (interpreter part of sh34_base_device::device_start) and
// the save-state fields (the save_item list of sh_common_execution and
// sh34_base_device::device_start).
#include "sh3.h"

#include "sh3_mame_defs.inc"

// sh3be_device::execute_run (MAME 0.264 sh4.cpp), with one addition: at an
// instruction boundary outside a delay slot, code the recompiler translated
// for the current address runs instead of the interpreter.  It executes whole
// instructions exactly as the loop below does (the delay-slot hand-over, the
// interrupt check, the cycle count) and returns at a boundary.
void sh3_device::execute_run()
{
	if (m_isdrc)
	{
		execute_run_drc();
		return;
	}

	if (m_sh2_state->m_cpu_off)
	{
		m_sh2_state->icount = 0;
		return;
	}

	do
	{
		if (m_native_lookup && !m_sh2_state->m_delay && !m_trace_hook)
		{
			int entry;
			native_fn fn = m_native_lookup(m_native_ctx, m_sh2_state->pc, &entry);
			if (fn)
			{
				m_native_calls++;
				const int before = m_sh2_state->icount;
				fn(this, m_sh2_state, entry);
				m_native_cycles += uint64_t(int64_t(before) - m_sh2_state->icount);
				continue;   // to the loop condition, as after an interpreted instruction
			}
		}
		m_interp_insns++;
		const int before = m_sh2_state->icount;
		if (m_interp_profile) m_interp_profile(this, m_sh2_state->pc);

		m_sh2_state->m_ppc = m_sh2_state->pc & SH34_AM;
		debugger_instruction_hook(m_sh2_state->pc & SH34_AM);

		const uint16_t opcode = m_pr16(m_sh2_state->pc & SH34_AM);

		if (m_sh2_state->m_delay)
		{
			m_sh2_state->pc = m_sh2_state->m_delay;
			m_sh2_state->m_delay = 0;
		}
		else
			m_sh2_state->pc += 2;

		execute_one(opcode);

		if (m_sh2_state->m_test_irq && !m_sh2_state->m_delay)
		{
			sh4_check_pending_irq("mame_sh4_execute");
		}

		m_sh2_state->icount--;
		m_interp_cycles += uint64_t(int64_t(before) - m_sh2_state->icount);
	} while (m_sh2_state->icount > 0);
}

sh3_device::sh3_device()
{
	std::memset(m_exception_priority, 0, sizeof(m_exception_priority));
	std::memset(m_exception_requesting, 0, sizeof(m_exception_requesting));
	std::memset(m_irq_line_state, 0, sizeof(m_irq_line_state));
	std::memset(m_m, 0, sizeof(m_m));
	std::memset(m_sh3internal_upper, 0, sizeof(m_sh3internal_upper));
	std::memset(m_sh3internal_lower, 0, sizeof(m_sh3internal_lower));
}

namespace {
enum { K_SH3_TMU = 100, K_SH3_DMA = 103, K_SH3_REFRESH = 107, K_SH3_RTC = 108 };
void t_tmu(void *ctx, int32_t p) { static_cast<sh3_device *>(ctx)->sh4_timer_callback(p); }
void t_dma(void *ctx, int32_t p) { static_cast<sh3_device *>(ctx)->sh4_dmac_callback(p); }
void t_refresh(void *ctx, int32_t p) { static_cast<sh3_device *>(ctx)->sh4_refresh_timer_callback(p); }
void t_rtc(void *ctx, int32_t p) { static_cast<sh3_device *>(ctx)->sh4_rtc_timer_callback(p); }
}

void sh3_device::device_start(uint32_t clock)
{
	(void)clock;
	kemu::scheduler &s = *m_exec->sched;

	// sh_common_execution::device_start
	m_sh2_state->pc = 0;
	m_sh2_state->pr = 0;
	m_sh2_state->sr = 0;
	m_sh2_state->gbr = 0;
	m_sh2_state->vbr = 0;
	m_sh2_state->mach = 0;
	m_sh2_state->macl = 0;
	memset(m_sh2_state->r, 0, sizeof(m_sh2_state->r));
	m_sh2_state->ea = 0;
	m_sh2_state->m_delay = 0;
	m_sh2_state->pending_irq = 0;
	m_sh2_state->pending_nmi = 0;
	m_sh2_state->irqline = 0;
	m_sh2_state->evec = 0;
	m_sh2_state->irqsr = 0;
	m_sh2_state->target = 0;
	m_sh2_state->internal_irq_level = 0;
	m_sh2_state->icount = 0;
	m_sh2_state->sleep_mode = 0;
	m_sh2_state->arg0 = 0;

	// sh34_base_device::device_start
	for (int i = 0; i < 3; i++)
	{
		m_timer[i] = s.timer_alloc(t_tmu, this, K_SH3_TMU + i);
		m_timer[i]->adjust(attotime::never, i);
	}

	for (int i = 0; i < 4; i++)
	{
		m_dma_timer[i] = s.timer_alloc(t_dma, this, K_SH3_DMA + i);
		m_dma_timer[i]->adjust(attotime::never, i);
	}

	m_refresh_timer = s.timer_alloc(t_refresh, this, K_SH3_REFRESH);
	m_refresh_timer->adjust(attotime::never);
	m_refresh_timer_base = 0;

	m_rtc_timer = s.timer_alloc(t_rtc, this, K_SH3_RTC);
	m_rtc_timer->adjust(attotime::never);

	sh4_parse_configuration();

	sh4_default_exception_priorities();
	m_irln = 15;
	m_sh2_state->m_test_irq = 0;
}

// ---------------------------------------------------------------- save states

template <typename W, typename T> static void field(W &w, T &v) { w.raw(&v, sizeof(v)); }

void sh3_device::save(state_writer &w) const
{
	w.raw(m_sh2_state, sizeof(*m_sh2_state));
	w.raw(m_exception_priority, sizeof(m_exception_priority));
	w.raw(m_exception_requesting, sizeof(m_exception_requesting));
	w.raw(m_irq_line_state, sizeof(m_irq_line_state));
	w.raw(m_m, sizeof(m_m));
	const uint32_t regs[] = {
		m_SH4_TSTR, m_SH4_TCNT0, m_SH4_TCNT1, m_SH4_TCNT2, m_SH4_TCR0, m_SH4_TCR1, m_SH4_TCR2,
		m_SH4_TCOR0, m_SH4_TCOR1, m_SH4_TCOR2, m_SH4_TOCR, m_SH4_TCPR2, m_SH4_IPRA, m_SH4_IPRC,
		m_SH4_SAR0, m_SH4_SAR1, m_SH4_SAR2, m_SH4_SAR3, m_SH4_DAR0, m_SH4_DAR1, m_SH4_DAR2, m_SH4_DAR3,
		m_SH4_CHCR0, m_SH4_CHCR1, m_SH4_CHCR2, m_SH4_CHCR3, m_SH4_DMATCR0, m_SH4_DMATCR1, m_SH4_DMATCR2,
		m_SH4_DMATCR3, m_SH4_DMAOR, m_refresh_timer_base };
	w.raw(regs, sizeof(regs));
	const int32_t ints[] = { m_nmi_line_state, m_irln, m_internal_irq_level, m_internal_irq_vector,
		m_is_slave, m_cpu_clock, m_bus_clock, m_pm_clock, m_ioport16_pullup, m_ioport16_direction,
		m_ioport4_pullup, m_ioport4_direction, m_willjump, m_sh4_mmu_enabled };
	w.raw(ints, sizeof(ints));
	w.raw(m_dma_timer_active, sizeof(m_dma_timer_active));
	w.raw(m_dma_source, sizeof(m_dma_source));
	w.raw(m_dma_destination, sizeof(m_dma_destination));
	w.raw(m_dma_count, sizeof(m_dma_count));
	w.raw(m_dma_wordsize, sizeof(m_dma_wordsize));
	w.raw(m_dma_source_increment, sizeof(m_dma_source_increment));
	w.raw(m_dma_destination_increment, sizeof(m_dma_destination_increment));
	w.raw(m_dma_mode, sizeof(m_dma_mode));
	w.raw(m_sh3internal_upper, sizeof(m_sh3internal_upper));
	w.raw(m_sh3internal_lower, sizeof(m_sh3internal_lower));
}

void sh3_device::load(state_reader &r)
{
	r.raw(m_sh2_state, sizeof(*m_sh2_state));
	r.raw(m_exception_priority, sizeof(m_exception_priority));
	r.raw(m_exception_requesting, sizeof(m_exception_requesting));
	r.raw(m_irq_line_state, sizeof(m_irq_line_state));
	r.raw(m_m, sizeof(m_m));
	uint32_t regs[32];
	r.raw(regs, sizeof(regs));
	uint32_t *dst[] = {
		&m_SH4_TSTR, &m_SH4_TCNT0, &m_SH4_TCNT1, &m_SH4_TCNT2, &m_SH4_TCR0, &m_SH4_TCR1, &m_SH4_TCR2,
		&m_SH4_TCOR0, &m_SH4_TCOR1, &m_SH4_TCOR2, &m_SH4_TOCR, &m_SH4_TCPR2, &m_SH4_IPRA, &m_SH4_IPRC,
		&m_SH4_SAR0, &m_SH4_SAR1, &m_SH4_SAR2, &m_SH4_SAR3, &m_SH4_DAR0, &m_SH4_DAR1, &m_SH4_DAR2, &m_SH4_DAR3,
		&m_SH4_CHCR0, &m_SH4_CHCR1, &m_SH4_CHCR2, &m_SH4_CHCR3, &m_SH4_DMATCR0, &m_SH4_DMATCR1, &m_SH4_DMATCR2,
		&m_SH4_DMATCR3, &m_SH4_DMAOR, &m_refresh_timer_base };
	for (int i = 0; i < 32; i++)
		*dst[i] = regs[i];
	int32_t ints[14];
	r.raw(ints, sizeof(ints));
	m_nmi_line_state = int8_t(ints[0]); m_irln = ints[1]; m_internal_irq_level = ints[2];
	m_internal_irq_vector = ints[3]; m_is_slave = ints[4]; m_cpu_clock = ints[5]; m_bus_clock = ints[6];
	m_pm_clock = ints[7]; m_ioport16_pullup = ints[8]; m_ioport16_direction = ints[9];
	m_ioport4_pullup = ints[10]; m_ioport4_direction = ints[11]; m_willjump = ints[12];
	m_sh4_mmu_enabled = ints[13] != 0;
	r.raw(m_dma_timer_active, sizeof(m_dma_timer_active));
	r.raw(m_dma_source, sizeof(m_dma_source));
	r.raw(m_dma_destination, sizeof(m_dma_destination));
	r.raw(m_dma_count, sizeof(m_dma_count));
	r.raw(m_dma_wordsize, sizeof(m_dma_wordsize));
	r.raw(m_dma_source_increment, sizeof(m_dma_source_increment));
	r.raw(m_dma_destination_increment, sizeof(m_dma_destination_increment));
	r.raw(m_dma_mode, sizeof(m_dma_mode));
	r.raw(m_sh3internal_upper, sizeof(m_sh3internal_upper));
	r.raw(m_sh3internal_lower, sizeof(m_sh3internal_lower));
}
