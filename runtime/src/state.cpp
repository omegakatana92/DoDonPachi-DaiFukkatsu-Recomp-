// Save states of the whole board: the SH-3, the scheduler (timers in list
// order, pending input-line updates), the devices, main RAM and the
// blitter's video RAM.  MAME does not support save states for these sets;
// the format is this port's own.  A state names its version and ROM
// fingerprint and is refused by any other version, ROM revision or format.
//
// The video RAM (8192 x 4096 pixels, 128 MB) is stored as 32 x 32 pixel
// tiles, the all-zero ones as a bit, the rest deflated (miniz, level 1).
#include "cv1k/machine.h"
#include "../recomp/sh3_recomp.h"
#include "miniz.h"

#include <cstring>

// ---------------------------------------------------------------- blitter

static const int VRAM_W = 0x2000, VRAM_H = 0x1000, TILE = 32;
static const int TILES_X = VRAM_W / TILE, TILES_Y = VRAM_H / TILE;

static void save_vram(const ep1c12_device &b, state_writer &w)
{
	std::vector<uint8_t> present((TILES_X * TILES_Y + 7) / 8, 0);
	std::vector<uint8_t> raw;
	raw.reserve(size_t(64) << 20);
	for (int ty = 0; ty < TILES_Y; ty++)
		for (int tx = 0; tx < TILES_X; tx++) {
			bool any = false;
			for (int y = 0; y < TILE && !any; y++) {
				const uint32_t *p = &b.m_bitmaps->pix(ty * TILE + y, tx * TILE);
				for (int x = 0; x < TILE; x++)
					if (p[x]) { any = true; break; }
			}
			if (!any) continue;
			const int t = ty * TILES_X + tx;
			present[size_t(t) >> 3] |= uint8_t(1u << (t & 7));
			for (int y = 0; y < TILE; y++) {
				const uint8_t *p = reinterpret_cast<const uint8_t *>(&b.m_bitmaps->pix(ty * TILE + y, tx * TILE));
				raw.insert(raw.end(), p, p + TILE * 4);
			}
		}
	w.raw(present.data(), present.size());
	mz_ulong zs = mz_compressBound(mz_ulong(raw.size()));
	std::vector<uint8_t> z(zs);
	if (mz_compress2(z.data(), &zs, raw.data(), mz_ulong(raw.size()), 1) != MZ_OK)
		zs = 0;
	w.put(uint64_t(raw.size()));
	w.put(uint64_t(zs));
	w.raw(z.data(), size_t(zs));
}

static bool load_vram(ep1c12_device &b, state_reader &r)
{
	std::vector<uint8_t> present((TILES_X * TILES_Y + 7) / 8, 0);
	r.raw(present.data(), present.size());
	uint64_t rawn = 0, zs = 0;
	r.get(rawn); r.get(zs);
	if (r.bad || zs > r.n - r.at || rawn > (uint64_t(VRAM_W) * VRAM_H * 4)) { r.bad = true; return false; }
	std::vector<uint8_t> raw(static_cast<size_t>(rawn));
	mz_ulong outn = mz_ulong(rawn);
	if (rawn && mz_uncompress(raw.data(), &outn, r.p + r.at, mz_ulong(zs)) != MZ_OK) { r.bad = true; return false; }
	r.at += size_t(zs);
	size_t at = 0;
	for (int ty = 0; ty < TILES_Y; ty++)
		for (int tx = 0; tx < TILES_X; tx++) {
			const int t = ty * TILES_X + tx;
			const bool has = (present[size_t(t) >> 3] >> (t & 7)) & 1;
			for (int y = 0; y < TILE; y++) {
				uint32_t *p = &b.m_bitmaps->pix(ty * TILE + y, tx * TILE);
				if (has) {
					if (at + TILE * 4 > raw.size()) { r.bad = true; return false; }
					std::memcpy(p, &raw[at], TILE * 4);
					at += TILE * 4;
				} else
					std::memset(p, 0, TILE * 4);
			}
		}
	return at == raw.size();
}

void ep1c12_device::save(state_writer &w, bool with_vram)
{
	wait_for_blit();
	w.put(m_gfx_addr); w.put(m_gfx_scroll_x); w.put(m_gfx_scroll_y); w.put(m_gfx_clip_x); w.put(m_gfx_clip_y);
	w.put(m_gfx_addr_shadowcopy); w.put(m_gfx_clip_x_shadowcopy); w.put(m_gfx_clip_y_shadowcopy);
	w.put(int32_t(m_clip.min_x)); w.put(int32_t(m_clip.max_x)); w.put(int32_t(m_clip.min_y)); w.put(int32_t(m_clip.max_y));
	w.put(int32_t(m_blitter_busy)); w.put(m_blit_delay_ns); w.put(m_blit_idle_op_bytes);
	w.put(int32_t(m_firmware_pos)); w.put(m_firmware_port); w.put(int32_t(m_firmware_version));
	w.raw(m_firmware.data(), m_firmware.size());
	if (with_vram)
		save_vram(*this, w);
}

void ep1c12_device::load(state_reader &r, bool with_vram)
{
	wait_for_blit();
	r.get(m_gfx_addr); r.get(m_gfx_scroll_x); r.get(m_gfx_scroll_y); r.get(m_gfx_clip_x); r.get(m_gfx_clip_y);
	r.get(m_gfx_addr_shadowcopy); r.get(m_gfx_clip_x_shadowcopy); r.get(m_gfx_clip_y_shadowcopy);
	int32_t c[4];
	for (auto &v : c) r.get(v);
	m_clip.set(c[0], c[1], c[2], c[3]);
	int32_t busy = 0, pos = 0, ver = 0;
	r.get(busy); r.get(m_blit_delay_ns); r.get(m_blit_idle_op_bytes);
	r.get(pos); r.get(m_firmware_port); r.get(ver);
	m_blitter_busy = busy; m_firmware_pos = pos; m_firmware_version = ver;
	r.raw(m_firmware.data(), m_firmware.size());
	if (with_vram) {
		load_vram(*this, r);
		m_vram_generation++;
	}
}

namespace cv1k {

kemu::timer::callback_fn sync_callback_for_kind(int kind, void **ctx);

static const uint32_t STATE_MAGIC = 0x4b315643u;   // "CV1K"
static const uint32_t STATE_VERSION = 1;

static uint32_t build_signature()
{
	// The format is written field by field in a fixed byte order, so it does
	// not depend on the compiler or the platform's structure layouts.  A
	// format change bumps STATE_VERSION.
	uint32_t h = 2166136261u;
	auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
	mix(0x44464b31u);   // "DFK1"
	mix(STATE_VERSION);
	mix(uint32_t(sizeof(internal_sh2_state)));
	return h;
}

static void save_exec(const kemu::exec_device &d, state_writer &w)
{
	w.put(d.suspend); w.put(d.nextsuspend); w.put(d.eatcycles); w.put(d.nexteatcycles);
	w.put(d.totalcycles); w.put(d.localtime); w.put(d.cycles_running); w.put(d.cycles_stolen);
	w.put(d.trigger_id);
	for (const auto &in : d.inputs) {
		w.put(in.stored_vector); w.put(in.curvector); w.put(in.curstate); w.put(in.qindex); w.put(in.queue);
	}
}

static void load_exec(kemu::exec_device &d, state_reader &r)
{
	r.get(d.suspend); r.get(d.nextsuspend); r.get(d.eatcycles); r.get(d.nexteatcycles);
	r.get(d.totalcycles); r.get(d.localtime); r.get(d.cycles_running); r.get(d.cycles_stolen);
	r.get(d.trigger_id);
	for (auto &in : d.inputs) {
		r.get(in.stored_vector); r.get(in.curvector); r.get(in.curstate); r.get(in.qindex); r.get(in.queue);
		if (in.qindex < 0 || in.qindex > 32) { r.bad = true; in.qindex = 0; }
	}
}

// ---------------------------------------------------------------- machine

void machine::save_state(std::vector<uint8_t> &out)
{
	state_writer w;
	save_core(w, false);
	out.swap(w.buf);
}

// `light`: without main RAM and the video RAM pixels (the rewind history
// keeps those as differences)
void machine::save_core(state_writer &w, bool light)
{
	w.put(STATE_MAGIC);
	w.put(STATE_VERSION);
	w.put(build_signature());
	w.put(int32_t(m_rom.v));
	w.put(m_rom.fingerprint);

	// scheduler
	w.put(m_sched.m_basetime);
	w.put(m_sched.m_suspend_changes_pending);
	w.put(m_sched.m_quantum_minimum);
	uint32_t nq = uint32_t(m_sched.m_quanta.size());
	w.put(nq);
	for (const auto &q : m_sched.m_quanta) { w.put(q.requested); w.put(q.actual); w.put(q.expire); }
	save_exec(m_cpu_exec, w);
	w.put(int32_t(kemu::spin_timetrig_get()));
	uint32_t ntimers = uint32_t(m_timers.size());
	w.put(ntimers);
	for (const kemu::timer *t : m_timers) {
		w.put(t->enabled); w.put(t->param); w.put(t->period); w.put(t->start); w.put(t->expire);
	}
	std::vector<std::pair<int32_t, const kemu::timer *>> active;
	for (const kemu::timer *t = m_sched.m_timer_list; t; t = t->next) {
		int32_t id = -1;
		for (size_t i = 0; i < m_timers.size(); i++)
			if (m_timers[i] == t) id = int32_t(i);
		active.push_back({id, t});
	}
	uint32_t nactive = uint32_t(active.size());
	w.put(nactive);
	for (const auto &a : active) {
		w.put(a.first);
		if (a.first < 0) {
			// temporary: an input-line queue flush (kind 1)
			const kemu::timer *t = a.second;
			w.put(t->kind); w.put(t->param); w.put(t->start); w.put(t->expire); w.put(t->enabled);
		}
	}

	// CPU and devices
	m_cpu.save(w);
	m_rtc.save(w);
	m_nand.save(w);
	m_ymz.save(w);
	m_blitter.save(w, !light);

	// memory and board latches
	if (!light) {
		// deflated (level 1): most of the 16 MB is zero or graphics data
		mz_ulong zs = mz_compressBound(MAIN_RAM_SIZE);
		std::vector<uint8_t> z(zs);
		if (mz_compress2(z.data(), &zs, reinterpret_cast<const uint8_t *>(m_ram.get()), MAIN_RAM_SIZE, 1) != MZ_OK)
			zs = 0;
		w.put(uint32_t(zs));
		w.raw(z.data(), size_t(zs));
	}
	{
		// the cache RAM area: the 64 KB blocks that are not zero
		std::vector<uint32_t> blocks;
		for (uint32_t b = 0; b < 0x1000000 / 0x10000; b++) {
			const uint64_t *p = &m_cache_ram[size_t(b) * 0x10000 / 8];
			for (size_t i = 0; i < 0x10000 / 8; i++) if (p[i]) { blocks.push_back(b); break; }
		}
		w.put(uint32_t(blocks.size()));
		for (uint32_t b : blocks) { w.put(b); w.raw(&m_cache_ram[size_t(b) * 0x10000 / 8], 0x10000); }
	}
	w.put(m_inputs); w.put(m_dsw); w.put(m_eepromout); w.put(m_eepromout_old); w.put(m_nv_writes);

	// stream position (generated audio is output, not state)
	w.put(m_ymz_clock.rate); w.put(m_ymz_clock.sample_attos); w.put(m_ymz_clock.end_second);
	w.put(m_ymz_clock.end_sample); w.put(m_ymz_clock.total);
	w.put(m_vblank_start_time); w.put(m_vblank_end_time);
	w.put(m_frame);
}

bool machine::validate_state(const uint8_t *data, size_t size) const
{
	state_reader r(data, size);
	uint32_t magic = 0, ver = 0, sig = 0, fp = 0;
	int32_t v = -1;
	r.get(magic); r.get(ver); r.get(sig); r.get(v); r.get(fp);
	return !r.bad && magic == STATE_MAGIC && ver == STATE_VERSION && sig == build_signature() &&
	       v == int32_t(m_rom.v) && fp == m_rom.fingerprint;
}

bool machine::load_state(const uint8_t *data, size_t size)
{
	if (!validate_state(data, size))
		return false;
	// keep a copy of the current state so a damaged file cannot leave a half-loaded machine
	std::vector<uint8_t> backup;
	if (!m_in_restore)
		save_state(backup);
	state_reader r(data, size);
	if (load_core(r, false)) {
		m_rewind_valid = false;   // the rewind history restarts from here
		return true;
	}
	if (!m_in_restore) {
		m_in_restore = true;
		load_state(backup.data(), backup.size());
		m_in_restore = false;
	}
	return false;
}

bool machine::load_core(state_reader &r, bool light)
{
	uint32_t magic, ver, sig, fp;
	int32_t v;
	r.get(magic); r.get(ver); r.get(sig); r.get(v); r.get(fp);
	auto fail = [&]() { return false; };

	r.get(m_sched.m_basetime);
	r.get(m_sched.m_suspend_changes_pending);
	r.get(m_sched.m_quantum_minimum);
	uint32_t nq = 0;
	r.get(nq);
	if (r.bad || nq == 0 || nq > 64) return fail();
	m_sched.m_quanta.assign(nq, kemu::scheduler::quantum_slot());
	for (auto &q : m_sched.m_quanta) { r.get(q.requested); r.get(q.actual); r.get(q.expire); }
	load_exec(m_cpu_exec, r);
	int32_t timetrig = 0;
	r.get(timetrig);
	kemu::spin_timetrig_set(timetrig & 255);
	uint32_t ntimers = 0;
	r.get(ntimers);
	if (r.bad || ntimers != m_timers.size()) return fail();
	for (kemu::timer *t : m_timers) {
		r.get(t->enabled); r.get(t->param); r.get(t->period); r.get(t->start); r.get(t->expire);
		t->next = t->prev = nullptr;
	}
	// drop any temporary timers of the running machine
	for (kemu::timer *t = m_sched.m_timer_list; t; ) {
		kemu::timer *n = t->next;
		if (t->temporary) delete t;
		t = n;
	}
	m_sched.m_timer_list = nullptr;
	m_sched.m_inactive_timers = nullptr;
	uint32_t nactive = 0;
	r.get(nactive);
	if (r.bad || nactive > 4096) return fail();
	std::vector<kemu::timer *> order;
	std::vector<bool> is_active(m_timers.size(), false);
	for (uint32_t i = 0; i < nactive && !r.bad; i++) {
		int32_t id = 0;
		r.get(id);
		if (id >= 0) {
			if (size_t(id) >= m_timers.size()) return fail();
			order.push_back(m_timers[size_t(id)]);
			is_active[size_t(id)] = true;
		} else {
			kemu::timer *t = new kemu::timer();
			r.get(t->kind); r.get(t->param); r.get(t->start); r.get(t->expire); r.get(t->enabled);
			t->sched = &m_sched;
			t->temporary = true;
			t->period = kemu::attotime::never();
			if (t->kind == 1) {
				t->ctx = &m_cpu_exec;
				t->cb = [](void *ctx, int32_t param) { static_cast<kemu::exec_device *>(ctx)->empty_event_queue(param); };
			} else {
				t->cb = sync_callback_for_kind(t->kind, &t->ctx);
				if (!t->cb) { delete t; return fail(); }
			}
			order.push_back(t);
		}
	}
	for (size_t i = 0; i < m_timers.size(); i++)
		if (!is_active[i]) {
			kemu::timer *t = m_timers[i];
			t->in_active_list = false;
			if (m_sched.m_inactive_timers) m_sched.m_inactive_timers->prev = t;
			t->next = m_sched.m_inactive_timers;
			t->prev = nullptr;
			m_sched.m_inactive_timers = t;
		}
	kemu::timer *prev = nullptr;
	for (kemu::timer *t : order) {
		t->in_active_list = true;
		t->prev = prev;
		t->next = nullptr;
		if (prev) prev->next = t; else m_sched.m_timer_list = t;
		prev = t;
	}

	m_cpu.load(r);
	m_rtc.load(r);
	{
		// the flash: back to its power-on contents, then the state's changed blocks
		const uint32_t block_bytes = m_nand.m_page_total_size << m_nand.m_log2_pages_per_block;
		for (uint32_t i = 0; i < m_nand.m_dirty_blocks.size(); i++)
			if (m_nand.m_dirty_blocks[i]) {
				const size_t at = size_t(i) * block_bytes;
				if (at + block_bytes <= m_rom.nand.size())
					std::memcpy(m_nand.m_feeprom_data.get() + at, m_rom.nand.data() + at, block_bytes);
				m_nand.m_dirty_blocks[i] = 0;
			}
		m_nand.load(r);
	}
	m_ymz.load(r);
	m_blitter.load(r, !light);

	if (!light) {
		uint32_t zs = 0;
		r.get(zs);
		mz_ulong n = MAIN_RAM_SIZE;
		if (r.bad || zs > r.n - r.at || mz_uncompress(reinterpret_cast<uint8_t *>(m_ram.get()), &n, r.p + r.at, zs) != MZ_OK || n != MAIN_RAM_SIZE)
			return fail();
		r.at += zs;
	}
	{
		std::memset(m_cache_ram.get(), 0, 0x1000000);
		uint32_t n = 0;
		r.get(n);
		for (uint32_t k = 0; k < n && !r.bad; k++) {
			uint32_t b = 0;
			r.get(b);
			if (b >= 0x100) { r.bad = true; break; }
			r.raw(&m_cache_ram[size_t(b) * 0x10000 / 8], 0x10000);
		}
	}
	r.get(m_inputs); r.get(m_dsw); r.get(m_eepromout); r.get(m_eepromout_old); r.get(m_nv_writes);
	r.get(m_ymz_clock.rate); r.get(m_ymz_clock.sample_attos); r.get(m_ymz_clock.end_second);
	r.get(m_ymz_clock.end_sample); r.get(m_ymz_clock.total);
	if (m_ymz_clock.rate == 0 || m_ymz_clock.sample_attos == 0) r.bad = true;
	r.get(m_vblank_start_time); r.get(m_vblank_end_time);
	r.get(m_frame);

	m_sched.m_execute_list = nullptr;
	m_sched.rebuild_execute_list();
	m_sched.m_executing_device = nullptr;
	m_sched.m_callback_timer = nullptr;
	if (r.bad || (!light && r.at != r.n))
		return fail();   // truncated or oversized file: the caller restores the previous machine

	// translated code whose instructions are not in the loaded RAM any more
	if (m_recomp && !light)
		m_recomp->verify_code();
	return true;
}

// ---------------------------------------------------------------- rewind
//
// The rewind history (the frontend keeps it, newest last) holds captures
// made every few frames.  Main RAM and the video RAM are not in them: the
// machine keeps a copy of both as they were at the newest capture (the
// shadow), and each capture holds, for the 4 KB pages of RAM and the 32 x 32
// tiles of video RAM changed since the capture before it, their contents
// then.  Stepping back restores the newest capture from the shadow and, when
// the frontend drops that capture, turns the shadow into the one before.

static const size_t RW_PAGE = 4096;

void machine::rewind_reset()
{
	m_blitter.wait_for_blit();
	m_rw_ram.assign(m_ram.get(), m_ram.get() + MAIN_RAM_SIZE / 8);
	m_rw_vram.assign(&m_blitter.m_bitmaps->pix(0, 0), &m_blitter.m_bitmaps->pix(0, 0) + size_t(VRAM_W) * VRAM_H);
	std::fill(m_blitter.m_dirty_tiles.begin(), m_blitter.m_dirty_tiles.end(), 0);
	m_rewind_valid = true;
}

static void tile_copy(uint32_t *dst, const uint32_t *src, int t)
{
	const size_t x = size_t(t % TILES_X) * TILE, y = size_t(t / TILES_X) * TILE;
	for (int r = 0; r < TILE; r++)
		std::memcpy(dst + (y + size_t(r)) * VRAM_W + x, src + (y + size_t(r)) * VRAM_W + x, TILE * 4);
}

void machine::rewind_capture(std::vector<uint8_t> &out)
{
	if (!m_rewind_valid)
		rewind_reset();
	m_blitter.wait_for_blit();
	state_writer w;
	save_core(w, true);
	// RAM pages changed since the last capture: their contents then
	uint8_t *ram = reinterpret_cast<uint8_t *>(m_ram.get());
	uint8_t *sram = reinterpret_cast<uint8_t *>(m_rw_ram.data());
	std::vector<uint32_t> pages;
	for (uint32_t p = 0; p < MAIN_RAM_SIZE / RW_PAGE; p++)
		if (std::memcmp(ram + size_t(p) * RW_PAGE, sram + size_t(p) * RW_PAGE, RW_PAGE)) pages.push_back(p);
	w.put(uint32_t(pages.size()));
	for (uint32_t p : pages) {
		w.put(p);
		w.raw(sram + size_t(p) * RW_PAGE, RW_PAGE);
		std::memcpy(sram + size_t(p) * RW_PAGE, ram + size_t(p) * RW_PAGE, RW_PAGE);
	}
	// video RAM tiles the blitter wrote since the last capture
	uint32_t *vram = &m_blitter.m_bitmaps->pix(0, 0);
	std::vector<uint32_t> tiles;
	for (int t = 0; t < TILES_X * TILES_Y; t++)
		if (m_blitter.m_dirty_tiles[size_t(t)]) tiles.push_back(uint32_t(t));
	w.put(uint32_t(tiles.size()));
	for (uint32_t t : tiles) {
		w.put(t);
		const size_t x = size_t(t % TILES_X) * TILE, y = size_t(t / TILES_X) * TILE;
		for (int r = 0; r < TILE; r++)
			w.raw(&m_rw_vram[(y + size_t(r)) * VRAM_W + x], TILE * 4);
		tile_copy(m_rw_vram.data(), vram, int(t));
		m_blitter.m_dirty_tiles[t] = 0;
	}
	// deflated
	mz_ulong zs = mz_compressBound(mz_ulong(w.buf.size()));
	out.resize(size_t(zs) + 8);
	if (mz_compress2(out.data() + 8, &zs, w.buf.data(), mz_ulong(w.buf.size()), 1) != MZ_OK) { out.clear(); return; }
	const uint32_t n = uint32_t(w.buf.size()), z = uint32_t(zs);
	std::memcpy(out.data(), &n, 4);
	std::memcpy(out.data() + 4, &z, 4);
	out.resize(size_t(zs) + 8);
}

bool machine::rewind_restore(const uint8_t *data, size_t size, bool pop)
{
	if (!m_rewind_valid || size < 8)
		return false;
	uint32_t n, z;
	std::memcpy(&n, data, 4);
	std::memcpy(&z, data + 4, 4);
	if (size_t(z) + 8 > size) return false;
	std::vector<uint8_t> raw(n);
	mz_ulong rn = n;
	if (mz_uncompress(raw.data(), &rn, data + 8, z) != MZ_OK || rn != n) return false;
	if (!validate_state(raw.data(), raw.size())) return false;
	m_blitter.wait_for_blit();
	state_reader r(raw.data(), raw.size());
	if (!load_core(r, true)) return false;
	// main RAM and the video RAM as they were at this capture: the shadow
	std::memcpy(m_ram.get(), m_rw_ram.data(), MAIN_RAM_SIZE);
	uint32_t *vram = &m_blitter.m_bitmaps->pix(0, 0);
	for (int t = 0; t < TILES_X * TILES_Y; t++)
		if (m_blitter.m_dirty_tiles[size_t(t)]) { tile_copy(vram, m_rw_vram.data(), t); m_blitter.m_dirty_tiles[size_t(t)] = 0; }
	m_blitter.m_vram_generation++;
	// the capture's differences: the shadow becomes the capture before it
	uint32_t np = 0;
	r.get(np);
	uint8_t *sram = reinterpret_cast<uint8_t *>(m_rw_ram.data());
	for (uint32_t k = 0; k < np && !r.bad; k++) {
		uint32_t p = 0;
		r.get(p);
		if (p >= MAIN_RAM_SIZE / RW_PAGE) { r.bad = true; break; }
		if (pop) r.raw(sram + size_t(p) * RW_PAGE, RW_PAGE);
		else r.at += RW_PAGE;
	}
	uint32_t nt = 0;
	r.get(nt);
	for (uint32_t k = 0; k < nt && !r.bad; k++) {
		uint32_t t = 0;
		r.get(t);
		if (t >= uint32_t(TILES_X * TILES_Y) || r.at + TILE * TILE * 4 > r.n) { r.bad = true; break; }
		if (pop) {
			const size_t x = size_t(t % TILES_X) * TILE, y = size_t(t / TILES_X) * TILE;
			for (int row = 0; row < TILE; row++)
				r.raw(&m_rw_vram[(y + size_t(row)) * VRAM_W + x], TILE * 4);
			m_blitter.m_dirty_tiles[t] = 1;
		} else
			r.at += TILE * TILE * 4;
	}
	if (m_recomp)
		m_recomp->verify_code();
	return !r.bad;
}

} // namespace cv1k
