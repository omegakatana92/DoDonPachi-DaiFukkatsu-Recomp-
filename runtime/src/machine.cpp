// CV1000-D board model; see include/cv1k/machine.h.
#include "cv1k/machine.h"
#include "../recomp/sh3_recomp.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <new>
#include <cstdio>

using kemu::attoseconds_t;

namespace cv1k {

static machine *g_m = nullptr;

// timer kinds (save states identify callbacks by kind; 1 is the scheduler's
// input-line queue flush, 100-108 the SH-3's, 200 the blitter's)
enum {
	K_SOUND_UPDATE = 10, K_SPIN_END, K_RTC, K_VBLANK_BEGIN, K_VBLANK_END, K_SCANLINE0,
};

// ------------------------------------------------------------------ trampolines

static u8 r8_cb(void *, offs_t a) { return g_m->read8(a); }
static u16 r16_cb(void *, offs_t a) { return g_m->read16(a); }
static u32 r32_cb(void *, offs_t a) { return g_m->read32(a); }
static u64 r64_cb(void *, offs_t a) { return g_m->read64(a); }
static void w8_cb(void *, offs_t a, u8 d) { g_m->write8(a, d); }
static void w16_cb(void *, offs_t a, u16 d) { g_m->write16(a, d); }
static void w32_cb(void *, offs_t a, u32 d) { g_m->write32(a, d); }
static void w64_cb(void *, offs_t a, u64 d) { g_m->write64(a, d); }
static u8 io_r8(void *, offs_t a) { return u8(g_m->io_read(a & ~7u) >> (8 * (7 - (a & 7)))); }
static u16 io_r16(void *, offs_t a) { return u16(g_m->io_read(a & ~7u) >> (16 * (3 - ((a >> 1) & 3)))); }
static u32 io_r32(void *, offs_t a) { return u32(g_m->io_read(a & ~7u) >> (32 * (1 - ((a >> 2) & 1)))); }
static u64 io_r64(void *, offs_t a) { return g_m->io_read(a & ~7u); }
static void io_w8(void *, offs_t a, u8 d) { g_m->io_write(a & ~7u, u64(d) << (8 * (7 - (a & 7)))); }
static void io_w16(void *, offs_t a, u16 d) { g_m->io_write(a & ~7u, u64(d) << (16 * (3 - ((a >> 1) & 3)))); }
static void io_w32(void *, offs_t a, u32 d) { g_m->io_write(a & ~7u, u64(d) << (32 * (1 - ((a >> 2) & 1)))); }
static void io_w64(void *, offs_t a, u64 d) { g_m->io_write(a & ~7u, d); }

static void cpu_run(void *) { g_m->m_cpu.execute_run(); }
static void cpu_set_input(void *, int line, int state) { g_m->m_cpu.execute_set_input(line, state); }
static void cpu_reset_device(void *) { g_m->m_cpu.device_reset(); }

static void t_sound_update(void *, int32_t) { g_m->sound_manager_update(); }
static void t_spin_end(void *, int32_t p) { g_m->m_cpu_exec.trigger(p); }   // timed_trigger_callback
static void t_rtc(void *, int32_t p) { g_m->m_rtc.timer_callback(p); }
static void t_vblank_begin(void *, int32_t) { g_m->vblank_begin(); }
static void t_vblank_end(void *, int32_t) { g_m->vblank_end(); }
static void t_scanline0(void *, int32_t) { g_m->scanline0(); }
static void ymz_stream_update(void *) { g_m->ymz_update(); }
static u32 blitter_dsw(void *) { return g_m->m_dsw; }

// temporary timers recreated from a save state: none on this board besides
// the scheduler's own input-line queue flush
kemu::timer::callback_fn sync_callback_for_kind(int, void **ctx)
{
	*ctx = g_m;
	return nullptr;
}

// ------------------------------------------------------------------ streams

void stream_clock::set_rate(uint32_t r)
{
	// stream_buffer::set_sample_rate: keep the end time, re-index it at the new rate
	if (r == rate)
		return;
	const kemu::attotime prevend = end_time();
	rate = r;
	sample_attos = r == 0 ? kemu::ATTOSECONDS_PER_SECOND : (kemu::ATTOSECONDS_PER_SECOND + r - 1) / r;
	end_sample = uint32_t(prevend.atto / sample_attos);   // time_to_buffer_index(prevend, false, true)
}

uint32_t stream_clock::advance_to(const kemu::attotime &t)
{
	// stream_buffer::time_to_buffer_index(time, round_up = true, allow_expansion = true)
	uint32_t sample = uint32_t((t.atto + (sample_attos - 1)) / sample_attos);
	if (t.sec > end_second || (t.sec == end_second && sample > end_sample)) {
		uint64_t before = uint64_t(end_second) * rate + end_sample;
		end_sample = sample;
		end_second = t.sec;
		if (sample >= rate) {
			end_sample -= rate;
			end_second++;
		}
		uint64_t after = uint64_t(end_second) * rate + end_sample;
		total += after - before;
		return uint32_t(after - before);
	}
	return 0;
}

// ------------------------------------------------------------------ machine

machine::machine()
{
	g_m = this;
}

machine::~machine()
{
	if (g_m == this)
		g_m = nullptr;
}

bool machine::load(version v, const char *path, std::string &err)
{
	m_recomp.reset();
	m_recomp_ready = false;
	return load_roms(v, path, m_rom, err);
}

void machine::start_devices(const uint8_t *eeprom, size_t eeprom_size)
{
	g_m = this;
	m_sched = kemu::scheduler();
	m_timers.clear();
	auto alloc = [&](kemu::timer::callback_fn cb, int kind) {
		kemu::timer *t = m_sched.timer_alloc(cb, this, kind);
		m_timers.push_back(t);
		return t;
	};

	// sound_manager: periodic update timer, created with the machine
	m_sound_update_timer = alloc(t_sound_update, K_SOUND_UPDATE);
	m_sound_update_timer->adjust(kemu::attotime::from_hz_u(50), 0, kemu::attotime::from_hz_u(50));

	// device order (cv1k_d): maincpu, eeprom, nand, screen, mono, ymz770, blitter

	// maincpu: SH3BE at 12.8 MHz * 8, md pins, set_sh4_clock
	m_cpu_exec = kemu::exec_device();
	m_cpu_exec.configure(m_sched, "maincpu", CPU_CLOCK);
	m_cpu_exec.icountptr = &m_cpu.icount();
	m_cpu_exec.run = cpu_run;
	m_cpu_exec.set_input = cpu_set_input;
	m_cpu_exec.reset_device = cpu_reset_device;
	// device_execute_interface::interface_pre_start: the spin-end timer (the
	// input-pulse timers it also allocates are never used on this board)
	m_cpu_exec.spin_end_timer = alloc(t_spin_end, K_SPIN_END);
	m_cpu.~sh3_device();
	new (&m_cpu) sh3_device();
	m_cpu.m_exec = &m_cpu_exec;
	sh3_space &p = m_cpu.m_program_space;
	p.r8 = r8_cb; p.r16 = r16_cb; p.r32 = r32_cb; p.r64 = r64_cb;
	p.w8 = w8_cb; p.w16 = w16_cb; p.w32 = w32_cb; p.w64 = w64_cb;
	sh3_space &io = m_cpu.m_io_space;
	io.r8 = io_r8; io.r16 = io_r16; io.r32 = io_r32; io.r64 = io_r64;
	io.w8 = io_w8; io.w16 = io_w16; io.w32 = io_w32; io.w64 = io_w64;
	static const int md[9] = { 0, 0, 0, 0, 0, 1, 0, 1, 0 };
	for (int i = 0; i < 9; i++)
		m_cpu.set_md(i, md[i]);
	m_cpu.set_sh4_clock(int(CPU_CLOCK));
	m_cpu.device_start(CPU_CLOCK);
	for (kemu::timer *t : m_cpu.m_timer) m_timers.push_back(t);
	for (kemu::timer *t : m_cpu.m_dma_timer) m_timers.push_back(t);
	m_timers.push_back(m_cpu.m_refresh_timer);
	m_timers.push_back(m_cpu.m_rtc_timer);

	// eeprom: RTC9701 (1 Hz timer); nvram_read / nvram_default
	m_rtc = rtc9701_device();
	m_rtc.device_start(alloc(t_rtc, K_RTC));
	m_rtc.nvram_load(eeprom, eeprom_size);

	// nand: SAMSUNG_K9F1G08U0M with the "nand" region
	m_nand.~nand_device();
	new (&m_nand) nand_device();
	m_nand.configure_k9f1g08u0m();
	m_nand.device_start(m_rom.nand.data(), m_rom.nand.size());

	// screen: set_refresh(HZ_TO_ATTOSECONDS(60.024)), set_size(512, 512),
	// visarea 320 x 240, set_vblank_time(ATTOSECONDS_IN_USEC(0))
	m_frame_period = attoseconds_t(double(kemu::ATTOSECONDS_PER_SECOND) / 60.024);
	m_scantime = m_frame_period / SCREEN_H;
	m_pixeltime = m_frame_period / (SCREEN_H * SCREEN_W);
	m_vblank_period = 0;
	m_vblank_begin_timer = alloc(t_vblank_begin, K_VBLANK_BEGIN);
	m_vblank_end_timer = alloc(t_vblank_end, K_VBLANK_END);
	m_scanline0_timer = alloc(t_scanline0, K_SCANLINE0);
	m_vblank_start_time = kemu::attotime();
	m_vblank_begin_timer->adjust(time_until_pos(VIS_H));
	m_scanline0_timer->adjust(time_until_pos(0));
	m_vblank_start_time = kemu::attotime();
	m_vblank_end_time = kemu::attotime(0, m_vblank_period);

	// ymz770: stream at 16 kHz, right output to the mono speaker
	m_ymz.~ymz770_device();
	new (&m_ymz) ymz770_device();
	m_ymz.m_stream_storage.update_cb = ymz_stream_update;
	m_ymz.m_stream_storage.ctx = this;
	m_ymz.device_start(m_rom.ymz.data(), m_rom.ymz.size());
	m_ymz_clock = stream_clock();
	m_ymz_clock.set_rate(YMZ_RATE);

	// blitter: EP1C12, port_r = DSW, 16 MB main RAM
	m_blitter.~ep1c12_device();
	new (&m_blitter) ep1c12_device();
	m_blitter.m_port_r = blitter_dsw;
	m_blitter.m_port_ctx = this;
	m_blitter.set_mainramsize(MAIN_RAM_SIZE);
	m_blitter.device_start(m_sched);
	m_timers.push_back(m_blitter.m_blitter_delay_timer);

	m_sched.rebuild_execute_list();
	m_sched.set_quantum(kemu::attotime::from_hz_u(60).atto);
}

void machine::reset_machine()
{
	// device_t::reset over the device tree in creation order, then machine_reset
	m_cpu_exec.pre_reset();
	m_cpu.device_reset();
	m_cpu_exec.post_reset();
	m_rtc.device_reset();
	m_nand.device_reset();
	m_ymz.device_reset();
	m_blitter.device_reset();
	// cv1k_state::machine_reset
	m_blitter.set_rambase(reinterpret_cast<uint16_t *>(m_ram.get()));
	m_blitter.device_reset();
}

void machine::power_on(const uint8_t *eeprom, size_t eeprom_size, const uint8_t *nand_diff, size_t nand_diff_size)
{
	if (!m_ram)
		m_ram.reset(new uint64_t[MAIN_RAM_SIZE / 8]);
	if (!m_cache_ram)
		m_cache_ram.reset(new uint64_t[0x1000000 / 8]);
	std::memset(m_ram.get(), 0, MAIN_RAM_SIZE);
	std::memset(m_cache_ram.get(), 0, 0x1000000);
	m_frame = 0;
	m_nv_writes = 0;
	m_eepromout = 0;
	m_eepromout_old[0] = 0; m_eepromout_old[1] = 0; m_eepromout_old[2] = 1;
	m_idle_ram = info(m_rom.v).idle_ram;
	m_idle_pc = info(m_rom.v).idle_pc;
	m_screen.assign(VIS_W * VIS_H, 0);
	kemu::spin_timetrig_set(0);

	start_devices(eeprom, eeprom_size);

	// the flash blocks rewritten in earlier sessions
	if (nand_diff && nand_diff_size) {
		state_reader r(nand_diff, nand_diff_size);
		const uint32_t block_bytes = m_nand.m_page_total_size << m_nand.m_log2_pages_per_block;
		uint32_t n = 0;
		r.get(n);
		for (uint32_t k = 0; k < n && !r.bad; k++) {
			uint32_t i = 0;
			r.get(i);
			if (i >= m_nand.m_dirty_blocks.size()) break;
			r.raw(m_nand.m_feeprom_data.get() + size_t(i) * block_bytes, block_bytes);
			m_nand.m_dirty_blocks[i] = 1;
		}
	}

	// device_rtc_interface: the current local time
	std::tm lt {};
	if (m_rtc_fixed) {
		lt.tm_year = m_rtc_set[0] - 1900; lt.tm_mon = m_rtc_set[1] - 1; lt.tm_mday = m_rtc_set[2];
		lt.tm_hour = m_rtc_set[3]; lt.tm_min = m_rtc_set[4]; lt.tm_sec = m_rtc_set[5];
		std::tm tmp = lt;
		std::mktime(&tmp);            // only for the weekday
		lt.tm_wday = tmp.tm_wday;
	} else {
		std::time_t now = std::time(nullptr);
#ifdef _WIN32
		localtime_s(&lt, &now);
#else
		localtime_r(&now, &lt);
#endif
	}
	m_rtc.set_time(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_wday + 1, lt.tm_hour, lt.tm_min, lt.tm_sec);

	reset_machine();

	// load-time recompilation of the program ROM.  The translation is kept
	// across later power cycles of the same ROM.
	m_cpu.m_native_lookup = nullptr;
	if (m_use_recompiler && !m_recomp_ready) {
		if (!m_recomp)
			m_recomp.reset(new sh3_recompiler());
		m_recomp_error.clear();
		sh3_recomp_memory mem;
		mem.rom = m_rom.prog.data();
		mem.rom_bytes = 0x400000;
		mem.ram = m_ram.get();
		mem.ram_base = 0x0c000000;
		mem.ram_bytes = MAIN_RAM_SIZE;
		mem.bus_only = 0x0c000000 + m_idle_ram;
		m_recomp->attach(mem);
		// the boot program in ROM from the reset address; the game, which it
		// copies to RAM, is translated as it is reached
		m_recomp_ready = m_recomp->translate({ 0xa0000000u }, m_recomp_error);
	}
	else if (m_use_recompiler && m_recomp_ready) {
		// a new power cycle: RAM starts empty, so drop the code translated from it
		sh3_recomp_memory mem;
		mem.rom = m_rom.prog.data(); mem.rom_bytes = 0x400000;
		mem.ram = m_ram.get(); mem.ram_base = 0x0c000000; mem.ram_bytes = MAIN_RAM_SIZE;
		mem.bus_only = 0x0c000000 + m_idle_ram;
		m_recomp->attach(mem);
		m_recomp_ready = m_recomp->translate({ 0xa0000000u }, m_recomp_error);
	}
	m_codemap = nullptr;
	if (m_use_recompiler && m_recomp_ready) {
		m_codemap = m_recomp->code_map();
		m_cpu.m_native_ctx = m_recomp.get();
		m_cpu.m_native_lookup = [](void *ctx, uint32_t pc, int *entry) -> sh3_device::native_fn {
			return static_cast<sh3_recompiler *>(ctx)->lookup(pc, entry);
		};
	}
}

void machine::recomp_end_of_frame()
{
	if (m_cpu.m_native_lookup && m_recomp && m_recomp->has_misses()) {
		std::string err;
		if (!m_recomp->translate_misses(err)) {
			m_recomp_error = err;
			m_recomp_ready = false;
			m_use_recompiler = false;
			m_cpu.m_native_lookup = nullptr;   // keep running on the interpreter
		}
	}
}

void machine::nand_save_diff(std::vector<uint8_t> &out) const
{
	state_writer w;
	const uint32_t block_bytes = m_nand.m_page_total_size << m_nand.m_log2_pages_per_block;
	uint32_t n = 0;
	for (uint8_t d : m_nand.m_dirty_blocks) n += d;
	w.put(n);
	for (uint32_t i = 0; i < m_nand.m_dirty_blocks.size(); i++)
		if (m_nand.m_dirty_blocks[i]) {
			w.put(i);
			w.raw(m_nand.m_feeprom_data.get() + size_t(i) * block_bytes, block_bytes);
		}
	out.swap(w.buf);
}

void machine::main_ram_bytes(std::vector<uint8_t> &out) const
{
	out.resize(MAIN_RAM_SIZE);
	for (size_t i = 0; i < MAIN_RAM_SIZE / 8; i++) {
		uint64_t w = m_ram[i];
		for (int b = 7; b >= 0; b--) { out[i * 8 + size_t(b)] = uint8_t(w); w >>= 8; }
	}
}

void machine::run_frame()
{
	g_m = this;
	m_frame_done = false;
	m_audio.clear();
	while (!m_frame_done)
		m_sched.timeslice();
	recomp_end_of_frame();
}

// ------------------------------------------------------------------ screen

kemu::attotime machine::time_until_pos(int vpos, int hpos) const
{
	vpos += SCREEN_H - VIS_H;
	vpos %= SCREEN_H;
	attoseconds_t targetdelta = attoseconds_t(vpos) * m_scantime + attoseconds_t(hpos) * m_pixeltime;
	attoseconds_t curdelta = (m_sched.time() - m_vblank_start_time).as_attoseconds();
	if (targetdelta <= curdelta + m_pixeltime / 2)
		targetdelta += m_frame_period;
	while (targetdelta <= curdelta)
		targetdelta += m_frame_period;
	return kemu::attotime(0, targetdelta - curdelta);
}

void machine::screen_update()
{
	// cv1k_state::screen_update -> ep1c12_device::draw_screen
	static bitmap_rgb32 bmp;
	if (bmp.width() != VIS_W)
		bmp.allocate(VIS_W, VIS_H);
	m_blitter.draw_screen(bmp, rectangle(0, VIS_W - 1, 0, VIS_H - 1));
	for (int y = 0; y < VIS_H; y++)
		for (int x = 0; x < VIS_W; x++)
			m_screen[size_t(y) * VIS_W + x] = bmp.pix(y, x) & 0x00ffffff;
}

void machine::vblank_begin()
{
	m_vblank_start_time = m_sched.time();
	m_vblank_end_time = m_vblank_start_time + kemu::attotime(0, m_vblank_period);

	// video_manager::frame_update (screen update, Lua frame notifiers)
	m_frame++;
	if (m_render)
		screen_update();
	if (on_vblank_hook)
		on_vblank_hook(*this, hook_ctx);

	// device_execute_interface::on_vblank: irq2_line_hold
	if (!m_cpu_exec.suspended(kemu::SUSPEND_REASON_HALT | kemu::SUSPEND_REASON_RESET | kemu::SUSPEND_REASON_DISABLE | kemu::SUSPEND_REASON_CLOCK))
		m_cpu_exec.set_input_line(2, kemu::HOLD_LINE);

	m_vblank_begin_timer->adjust(time_until_pos(VIS_H));
	// no VBLANK period: vblank_end at once
	vblank_end();
	m_frame_done = true;
}

void machine::vblank_end()
{
	// nothing listens to the end of VBLANK on this board
}

void machine::scanline0()
{
	// reset_partial_updates
	m_scanline0_timer->adjust(time_until_pos(0));
}

// ------------------------------------------------------------------ sound

void machine::ymz_update()
{
	kemu::attotime start = m_ymz_clock.end_time();
	kemu::attotime end = m_sched.time();
	if (start >= end)
		return;
	uint32_t n = m_ymz_clock.advance_to(end);
	if (!n)
		return;
	std::vector<float> l(n), r(n);
	m_ymz.generate(l.data(), r.data(), int(n));
	// add_route(1, "mono", 1.0): the right output only
	m_audio.insert(m_audio.end(), r.begin(), r.end());
}

void machine::sound_manager_update()
{
	ymz_update();
}

// ------------------------------------------------------------------ inputs (INPUT_PORTS_START(cv1k))

uint32_t machine::port_c() const
{
	uint32_t v = 0xff;
	if (m_inputs & IN_SERVICE1) v &= ~0x01u;
	if (m_inputs & IN_SERVICE2) v &= ~0x02u;
	if (m_inputs & IN_COIN1) v &= ~0x04u;
	if (m_inputs & IN_COIN2) v &= ~0x08u;
	if (m_inputs & IN_P1_START) v &= ~0x10u;
	if (m_inputs & IN_P2_START) v &= ~0x20u;
	return v;
}

uint32_t machine::port_d() const { return 0xffu & ~(m_inputs & 0xff); }
uint32_t machine::port_l() const { return 0xffu & ~((m_inputs >> 8) & 0xff); }
uint32_t machine::port_f() const { return (m_inputs & IN_TEST) ? 0xfdu : 0xffu; }

// ------------------------------------------------------------------ devices (cv1k_d_map, cv1k_port)

uint64_t machine::speedup_r()
{
	const offs_t pc = m_cpu.pc() & SH34_AM;
	if (pc == m_idle_pc || pc == m_idle_pc + 2)
		m_cpu.spin_until_time(attotime::from_usec(10));
	return m_ram[m_idle_ram / 8];
}

uint8_t machine::flash_io_r(uint32_t offset)
{
	switch (offset)
	{
		default:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			return 0xff;

		case 0x00:
			return m_nand.data_r();
	}
}

void machine::flash_io_w(uint32_t offset, uint8_t data)
{
	switch (offset)
	{
		default:
		case 0x03:
			break;

		case 0x00:
			m_nand.data_w(data);
			break;

		case 0x01:
		{
			// note the erase block a program or erase command changes (for the
			// NVRAM difference file); the command itself is MAME's
			const bool program = (data == 0x10 || data == 0x15) &&
				(m_nand.m_mode == nand_device::SM_M_PROGRAM || m_nand.m_mode == nand_device::SM_M_RANDOM_DATA_INPUT);
			const bool erase = data == 0xd0 && m_nand.m_mode == nand_device::SM_M_ERASE;
			const uint32_t page = m_nand.m_page_addr;
			m_nand.command_w(data);
			if ((program || erase) && page < m_nand.m_num_pages) {
				m_nand.m_dirty_blocks[page >> m_nand.m_log2_pages_per_block] = 1;
				m_nv_writes++;
			}
			break;
		}

		case 0x2:
			m_nand.address_w(data);
			break;
	}
}

uint8_t machine::serial_rtc_eeprom_r(uint32_t offset)
{
	switch (offset)
	{
		case 0x01:
			return uint8_t(0xfe | m_rtc.read_bit());

		default:
			return 0;
	}
}

void machine::serial_rtc_eeprom_w(uint32_t offset, uint8_t data)
{
	switch (offset)
	{
		case 0x01:
		{
			// m_eepromout->write(data, 0xff): ioport_port::write with the three
			// IPT_OUTPUT fields, each calling its line only when it changes
			// (dynamic_field::write); bit 2 is active low
			m_eepromout = (m_eepromout & ~0xffu) | data;
			const uint32_t v = m_eepromout ^ 0x04;   // ^ defvalue of each field
			const uint8_t nv[3] = { uint8_t(v & 1), uint8_t((v >> 1) & 1), uint8_t((v >> 2) & 1) };
			if (nv[0] != m_eepromout_old[0]) { m_eepromout_old[0] = nv[0]; m_rtc.write_bit(nv[0]); }
			if (nv[1] != m_eepromout_old[1]) { m_eepromout_old[1] = nv[1]; m_rtc.set_clock_line(nv[1]); }
			if (nv[2] != m_eepromout_old[2]) { m_eepromout_old[2] = nv[2]; m_rtc.set_cs_line(nv[2]); }
			m_nv_writes++;
			break;
		}
		case 0x03:
			break;

		default:
			break;
	}
}

uint64_t machine::io_read(uint32_t port)
{
	switch (port) {
	case SH3_PORT_C: return port_c();
	case SH3_PORT_D: return port_d();
	case SH3_PORT_E: return (!m_nand.is_busy() ? 0x20 : 0x00) | 0xdf;   // flash_port_e_r
	case SH3_PORT_F: return port_f();
	case SH3_PORT_L: return port_l();
	case SH3_PORT_J: return m_blitter.fpga_r();
	}
	return 0;
}

void machine::io_write(uint32_t port, uint64_t d)
{
	if (port == SH3_PORT_J)
		m_blitter.fpga_w(0, d);
}

// A 32-bit handler on the 64-bit big-endian bus sees offset (address - start) / 4
// and the lanes of its half of the 64-bit word.
template <typename R>
static uint64_t read32_lanes(uint32_t lane, uint32_t start, uint64_t mask, R rd)
{
	uint64_t v = 0;
	if (mask >> 32) v |= uint64_t(rd((lane - start) >> 2, uint32_t(mask >> 32))) << 32;
	if (uint32_t(mask)) v |= rd((lane + 4 - start) >> 2, uint32_t(mask));
	return v;
}
template <typename W>
static void write32_lanes(uint32_t lane, uint32_t start, uint64_t d, uint64_t mask, W wr)
{
	if (mask >> 32) wr((lane - start) >> 2, uint32_t(d >> 32), uint32_t(mask >> 32));
	if (uint32_t(mask)) wr((lane + 4 - start) >> 2, uint32_t(d), uint32_t(mask));
}

uint64_t machine::dev_read(uint32_t a, uint64_t mask)
{
	const uint32_t lane = a & ~7u;
	if (lane >= 0x04000000 && lane <= 0x07ffffff)
		return read32_lanes(lane, 0x04000000, mask, [&](offs_t o, u32 m) { return m_cpu.sh3_internal_r(o, m); });
	if (lane >= 0xffffd000)
		return read32_lanes(lane, 0xffffd000, mask, [&](offs_t o, u32 m) { return m_cpu.sh3_internal_high_r(o, m); });
	if (lane >= 0x18000000 && lane <= 0x18000057)
		return read32_lanes(lane, 0x18000000, mask, [&](offs_t o, u32 m) { return m_blitter.blitter_r(o, m); });
	if (lane == 0x10000000 || lane == 0x10c00000) {
		uint64_t v = 0;
		for (int b = 0; b < 8; b++) {
			const int sh = 8 * (7 - b);
			if ((mask >> sh) & 0xff)
				v |= uint64_t(lane == 0x10000000 ? flash_io_r(uint32_t(b)) : serial_rtc_eeprom_r(uint32_t(b))) << sh;
		}
		return v;
	}
	return 0;   // unmapped (and the write-only YMZ770)
}

void machine::dev_write(uint32_t a, uint64_t d, uint64_t mask)
{
	const uint32_t lane = a & ~7u;
	if (lane >= 0x04000000 && lane <= 0x07ffffff) {
		write32_lanes(lane, 0x04000000, d, mask, [&](offs_t o, u32 v, u32 m) { m_cpu.sh3_internal_w(o, v, m); });
		return;
	}
	if (lane >= 0xffffd000) {
		write32_lanes(lane, 0xffffd000, d, mask, [&](offs_t o, u32 v, u32 m) { m_cpu.sh3_internal_high_w(o, v, m); });
		return;
	}
	if (lane >= 0x18000000 && lane <= 0x18000057) {
		address_space space;
		write32_lanes(lane, 0x18000000, d, mask, [&](offs_t o, u32 v, u32 m) { m_blitter.blitter_w(space, o, v, m); });
		return;
	}
	if (lane == 0x10000000 || lane == 0x10400000 || lane == 0x10c00000) {
		for (int b = 0; b < 8; b++) {
			const int sh = 8 * (7 - b);
			if ((mask >> sh) & 0xff) {
				const uint8_t v = uint8_t(d >> sh);
				if (lane == 0x10000000) flash_io_w(uint32_t(b), v);
				else if (lane == 0x10400000) m_ymz.write(offs_t(b), v);
				else serial_rtc_eeprom_w(uint32_t(b), v);
			}
		}
	}
}

// main RAM and the cache RAM: 64-bit words in host order, as MAME keeps them
static inline uint8_t *ram_bytes(uint64_t *r) { return reinterpret_cast<uint8_t *>(r); }

uint8_t machine::read8(uint32_t a)
{
	if (a < 0x400000)
		return uint8_t(m_rom.prog[a >> 3] >> (8 * (7 - (a & 7))));
	if (a - 0x0c000000u < MAIN_RAM_SIZE) {
		if ((a & ~7u) == 0x0c000000u + m_idle_ram)
			return uint8_t(speedup_r() >> (8 * (7 - (a & 7))));
		return uint8_t(m_ram[(a & 0xffffff) >> 3] >> (8 * (7 - (a & 7))));
	}
	if (a - 0xf0000000u < 0x1000000u)
		return uint8_t(m_cache_ram[(a & 0xffffff) >> 3] >> (8 * (7 - (a & 7))));
	return uint8_t(dev_read(a, 0xffull << (8 * (7 - (a & 7)))) >> (8 * (7 - (a & 7))));
}

uint16_t machine::read16(uint32_t a)
{
	a &= ~1u;
	const int sh = 16 * (3 - ((a >> 1) & 3));
	if (a < 0x400000)
		return uint16_t(m_rom.prog[a >> 3] >> sh);
	if (a - 0x0c000000u < MAIN_RAM_SIZE) {
		if ((a & ~7u) == 0x0c000000u + m_idle_ram)
			return uint16_t(speedup_r() >> sh);
		return uint16_t(m_ram[(a & 0xffffff) >> 3] >> sh);
	}
	if (a - 0xf0000000u < 0x1000000u)
		return uint16_t(m_cache_ram[(a & 0xffffff) >> 3] >> sh);
	return uint16_t(dev_read(a, 0xffffull << sh) >> sh);
}

uint32_t machine::read32(uint32_t a)
{
	a &= ~3u;
	const int sh = (a & 4) ? 0 : 32;
	if (a < 0x400000)
		return uint32_t(m_rom.prog[a >> 3] >> sh);
	if (a - 0x0c000000u < MAIN_RAM_SIZE) {
		if ((a & ~7u) == 0x0c000000u + m_idle_ram)
			return uint32_t(speedup_r() >> sh);
		return uint32_t(m_ram[(a & 0xffffff) >> 3] >> sh);
	}
	if (a - 0xf0000000u < 0x1000000u)
		return uint32_t(m_cache_ram[(a & 0xffffff) >> 3] >> sh);
	return uint32_t(dev_read(a, 0xffffffffull << sh) >> sh);
}

uint64_t machine::read64(uint32_t a)
{
	a &= ~7u;
	if (a < 0x400000)
		return m_rom.prog[a >> 3];
	if (a - 0x0c000000u < MAIN_RAM_SIZE) {
		if (a == 0x0c000000u + m_idle_ram)
			return speedup_r();
		return m_ram[(a & 0xffffff) >> 3];
	}
	if (a - 0xf0000000u < 0x1000000u)
		return m_cache_ram[(a & 0xffffff) >> 3];
	return dev_read(a, ~0ull);
}

template <typename T>
static inline void merge(uint64_t &w, T d, int sh)
{
	const uint64_t m = uint64_t(T(~T(0))) << sh;
	w = (w & ~m) | (uint64_t(d) << sh);
}

// translated code in RAM overwritten (by the interpreter or a device)
inline void machine::code_check(uint32_t a, uint32_t bytes)
{
	if (m_codemap) {
		const uint32_t o = (a - 0x0c000000u) >> 1;
		if (m_codemap[o] | (bytes > 2 ? m_codemap[o + 1] : 0) | (bytes > 4 ? m_codemap[o + 2] | m_codemap[o + 3] : 0))
			m_recomp->code_written(a, bytes);
	}
}

void machine::write8(uint32_t a, uint8_t d)
{
	const int sh = 8 * (7 - (a & 7));
	if (a - 0x0c000000u < MAIN_RAM_SIZE) { merge(m_ram[(a & 0xffffff) >> 3], d, sh); code_check(a, 1); return; }
	if (a < 0x400000) return;   // .nopw()
	if (a - 0xf0000000u < 0x1000000u) { merge(m_cache_ram[(a & 0xffffff) >> 3], d, sh); return; }
	dev_write(a, uint64_t(d) << sh, 0xffull << sh);
}

void machine::write16(uint32_t a, uint16_t d)
{
	a &= ~1u;
	const int sh = 16 * (3 - ((a >> 1) & 3));
	if (a - 0x0c000000u < MAIN_RAM_SIZE) { merge(m_ram[(a & 0xffffff) >> 3], d, sh); code_check(a, 2); return; }
	if (a < 0x400000) return;
	if (a - 0xf0000000u < 0x1000000u) { merge(m_cache_ram[(a & 0xffffff) >> 3], d, sh); return; }
	dev_write(a, uint64_t(d) << sh, 0xffffull << sh);
}

void machine::write32(uint32_t a, uint32_t d)
{
	a &= ~3u;
	const int sh = (a & 4) ? 0 : 32;
	if (a - 0x0c000000u < MAIN_RAM_SIZE) { merge(m_ram[(a & 0xffffff) >> 3], d, sh); code_check(a, 4); return; }
	if (a < 0x400000) return;
	if (a - 0xf0000000u < 0x1000000u) { merge(m_cache_ram[(a & 0xffffff) >> 3], d, sh); return; }
	dev_write(a, uint64_t(d) << sh, 0xffffffffull << sh);
}

void machine::write64(uint32_t a, uint64_t d)
{
	a &= ~7u;
	if (a - 0x0c000000u < MAIN_RAM_SIZE) { m_ram[(a & 0xffffff) >> 3] = d; code_check(a, 8); return; }
	if (a < 0x400000) return;
	if (a - 0xf0000000u < 0x1000000u) { m_cache_ram[(a & 0xffffff) >> 3] = d; return; }
	dev_write(a, d, ~0ull);
}

} // namespace cv1k
