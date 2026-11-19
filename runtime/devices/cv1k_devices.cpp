// See cv1k_devices.h.  The MAME member functions come from the *_mame.inc
// files; this file has the replaced start-up code and the save states.
#include "cv1k_devices.h"

#include <cstring>

// ---------------------------------------------------------------- NAND flash

#include "nandflash_mame.inc"

void nand_device::configure_k9f1g08u0m()
{
	// samsung_k9f1g08u0m_device::samsung_k9f1g08u0m_device
	m_id_len = 4;
	m_id[0] = 0xec;
	m_id[1] = 0xf1;
	m_id[2] = 0x00;
	m_id[3] = 0x15;
	m_page_data_size = 2048;
	m_page_total_size = 2048 + 64;
	m_log2_pages_per_block = 6;   // compute_log2(64)
	m_num_pages = 64 * 1024;
	m_col_address_cycles = 2;
	m_row_address_cycles = 2;
	m_sequential_row_read = 0;
}

void nand_device::device_start(const uint8_t *region, size_t region_bytes)
{
	m_data_uid_ptr = nullptr; // smartmed cruft
	m_feeprom_data = std::make_unique<uint8_t[]>(size_t(m_page_total_size) * m_num_pages);
	m_pagereg = std::make_unique<uint8_t[]>(m_page_total_size);
	m_dirty_blocks.assign(m_num_pages >> m_log2_pages_per_block, 0);

	// nvram_default with the "nand" region
	size_t bytes = region_bytes;
	if (bytes > size_t(m_page_total_size) * m_num_pages)
		bytes = size_t(m_page_total_size) * m_num_pages;
	std::memcpy(m_feeprom_data.get(), region, bytes);
	if (bytes < size_t(m_page_total_size) * m_num_pages)
		std::memset(m_feeprom_data.get() + bytes, 0xff, size_t(m_page_total_size) * m_num_pages - bytes);
}

void nand_device::save(state_writer &w) const
{
	const uint8_t modes[2] = { uint8_t(m_mode), uint8_t(m_pointer_mode) };
	w.raw(modes, 2);
	const uint32_t v[] = { m_page_addr, m_byte_addr, m_addr_load_ptr, m_program_byte_count };
	w.raw(v, sizeof(v));
	const uint8_t b[] = { m_status, m_accumulated_status, uint8_t(m_mode_3065) };
	w.raw(b, sizeof(b));
	w.raw(m_pagereg.get(), m_page_total_size);
	// the flash contents: only the erase blocks changed since power-on
	const uint32_t block_bytes = m_page_total_size << m_log2_pages_per_block;
	uint32_t n = 0;
	for (uint8_t d : m_dirty_blocks) n += d;
	w.put(n);
	for (uint32_t i = 0; i < m_dirty_blocks.size(); i++)
		if (m_dirty_blocks[i]) {
			w.put(i);
			w.raw(m_feeprom_data.get() + size_t(i) * block_bytes, block_bytes);
		}
}

void nand_device::load(state_reader &r)
{
	uint8_t modes[2];
	r.raw(modes, 2);
	m_mode = sm_mode_t(modes[0]);
	m_pointer_mode = pointer_sm_mode_t(modes[1]);
	uint32_t v[4];
	r.raw(v, sizeof(v));
	m_page_addr = v[0]; m_byte_addr = v[1]; m_addr_load_ptr = v[2]; m_program_byte_count = v[3];
	uint8_t b[3];
	r.raw(b, sizeof(b));
	m_status = b[0]; m_accumulated_status = b[1]; m_mode_3065 = b[2] != 0;
	r.raw(m_pagereg.get(), m_page_total_size);
	// the caller restores the power-on contents first (see machine::load_state)
	const uint32_t block_bytes = m_page_total_size << m_log2_pages_per_block;
	uint32_t n = 0;
	r.get(n);
	for (uint32_t k = 0; k < n && !r.bad; k++) {
		uint32_t i = 0;
		r.get(i);
		if (i >= m_dirty_blocks.size()) { r.bad = true; break; }
		r.raw(m_feeprom_data.get() + size_t(i) * block_bytes, block_bytes);
		m_dirty_blocks[i] = 1;
	}
}

// ---------------------------------------------------------------- RTC9701

#include "rtc9701_mame.inc"

void rtc9701_device::device_start(emu_timer *timer, uint32_t clock)
{
	/* let's call the timer callback every second */
	m_timer = timer;
	m_timer->adjust(attotime::from_hz(double(clock) / 32768.0), 0, attotime::from_hz(double(clock) / 32768.0));

	rtc_state = state_t::CMD_WAIT;
	cmd_stream_pos = 0;
	current_cmd = 0;
}

void rtc9701_device::nvram_load(const uint8_t *data, size_t size)
{
	if (data && size == 0x200)
		std::memcpy(rtc9701_data, data, 0x200);
	else
		nvram_default();
}

void rtc9701_device::nvram_save(std::vector<uint8_t> &out) const
{
	out.resize(0x200);
	std::memcpy(out.data(), rtc9701_data, 0x200);
}

void rtc9701_device::save(state_writer &w) const
{
	const int32_t v[] = { m_latch, m_reset_line, m_clock_line, int32_t(rtc_state), cmd_stream_pos, current_cmd,
		rtc9701_address_pos, rtc9701_current_address, rtc9701_current_data, rtc9701_data_pos };
	w.raw(v, sizeof(v));
	w.raw(rtc9701_data, sizeof(rtc9701_data));
	w.raw(&m_rtc, sizeof(m_rtc));
}

void rtc9701_device::load(state_reader &r)
{
	int32_t v[10];
	r.raw(v, sizeof(v));
	m_latch = v[0]; m_reset_line = v[1]; m_clock_line = v[2]; rtc_state = state_t(v[3]);
	cmd_stream_pos = v[4]; current_cmd = v[5]; rtc9701_address_pos = v[6];
	rtc9701_current_address = v[7]; rtc9701_current_data = uint16_t(v[8]); rtc9701_data_pos = v[9];
	r.raw(rtc9701_data, sizeof(rtc9701_data));
	r.raw(&m_rtc, sizeof(m_rtc));
}

// ---------------------------------------------------------------- YMZ770

#include "ymz770_mame.inc"

ymz770_device::~ymz770_device()
{
	for (auto &channel : m_channels)
		delete channel.decoder;
}

void ymz770_device::device_start(const uint8_t *rom, size_t bytes)
{
	m_rom.p = rom;
	m_rom.n = bytes;
	for (auto & channel : m_channels)
	{
		channel.is_playing = false;
		delete channel.decoder;
		channel.decoder = new mpeg_audio(&m_rom[0], mpeg_audio::AMM, false, 0);
	}
	for (auto & sequence : m_sequences)
		sequence.is_playing = false;
	for (auto & sqc : m_sqcs)
		sqc.is_playing = false;
}

void ymz770_device::generate(float *l, float *r, int n)
{
	std::vector<read_stream_view> in;
	std::vector<write_stream_view> out(2);
	out[0].buf = l; out[0].n = n;
	out[1].buf = r; out[1].n = n;
	sound_stream_update(*m_stream, in, out);
}

// The decoder keeps its synthesis history between blocks.  Its object is
// stored as bytes, less the leading data pointer and the trailing bit-reader
// function pointer, which stay those of the running decoder.
static constexpr size_t MPEG_SKIP_HEAD = sizeof(void *);
static constexpr size_t MPEG_SKIP_TAIL = sizeof(void *);

void ymz770_device::save(state_writer &w) const
{
	const uint8_t regs[] = { m_cur_reg, m_mute, m_doen, m_vlma, m_vlma1, m_bsl, m_cpl };
	w.raw(regs, sizeof(regs));
	for (const auto &c : m_channels) {
		w.raw(&c, offsetof(ymz_channel, decoder));
		w.raw(&c.output_data, sizeof(ymz_channel) - offsetof(ymz_channel, output_data));
		const uint8_t *d = reinterpret_cast<const uint8_t *>(c.decoder);
		w.raw(d + MPEG_SKIP_HEAD, sizeof(mpeg_audio) - MPEG_SKIP_HEAD - MPEG_SKIP_TAIL);
	}
	w.raw(m_sequences, sizeof(m_sequences));
	w.raw(m_sqcs, sizeof(m_sqcs));
}

void ymz770_device::load(state_reader &r)
{
	uint8_t regs[7];
	r.raw(regs, sizeof(regs));
	m_cur_reg = regs[0]; m_mute = regs[1]; m_doen = regs[2]; m_vlma = regs[3];
	m_vlma1 = regs[4]; m_bsl = regs[5]; m_cpl = regs[6];
	for (auto &c : m_channels) {
		r.raw(&c, offsetof(ymz_channel, decoder));
		r.raw(&c.output_data, sizeof(ymz_channel) - offsetof(ymz_channel, output_data));
		uint8_t *d = reinterpret_cast<uint8_t *>(c.decoder);
		r.raw(d + MPEG_SKIP_HEAD, sizeof(mpeg_audio) - MPEG_SKIP_HEAD - MPEG_SKIP_TAIL);
	}
	r.raw(m_sequences, sizeof(m_sequences));
	r.raw(m_sqcs, sizeof(m_sqcs));
}
