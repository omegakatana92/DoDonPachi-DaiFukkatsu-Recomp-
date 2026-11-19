// The CV1000's NAND flash, RTC9701 and YMZ770, as stand-alone classes.
//
// The member functions are MAME 0.264's (imported by tools/import_mame.py
// into nandflash_mame.inc, rtc9701_mame.inc and ymz770_mame.inc, BSD-3-Clause:
// nandflash.cpp Raphael Nabet, rtc9701.cpp Angelo Salese and David Haywood,
// ymz770.cpp Olivier Galibert, R. Belmont and MetalliC).  The declarations
// below follow MAME's headers (nandflash.h, rtc9701.h, ymz770.h), with the
// MAME framework bases replaced by the members marked "port".
#ifndef DFK_CV1K_DEVICES_H
#define DFK_CV1K_DEVICES_H

#include "../emu/mame_shim.h"
#include "../emu/state_io.h"
#include "mpeg_audio.h"

#include <vector>

// ---------------------------------------------------------------- NAND flash

class nand_device
{
public:
	// port: the SAMSUNG_K9F1G08U0M configuration, the data from the "nand"
	// region (nvram_default) and the device_start allocations
	void configure_k9f1g08u0m();
	void device_start(const uint8_t *region, size_t region_bytes);
	void save(state_writer &w) const;
	void load(state_reader &r);
	// pages changed since power-on (the program/erase commands) are tracked
	// so that the battery-less flash can be kept as a small difference file
	std::vector<uint8_t> m_dirty_blocks;

	int is_present();
	int is_protected();
	int is_busy();

	uint8_t data_r();
	void command_w(uint8_t data);
	void address_w(uint8_t data);
	void data_w(uint8_t data);
	void device_reset();

	enum sm_mode_t : uint8_t
	{
		SM_M_INIT,       // initial state
		SM_M_READ,       // read page data
		SM_M_PROGRAM,    // program page data
		SM_M_ERASE,      // erase block data
		SM_M_READSTATUS, // read status
		SM_M_READID,     // read ID
		SM_M_30,
		SM_M_RANDOM_DATA_INPUT,
		SM_M_RANDOM_DATA_OUTPUT
	};

	enum pointer_sm_mode_t : uint8_t
	{
		SM_PM_A, // accessing first 256-byte half of 512-byte data field
		SM_PM_B, // accessing second 256-byte half of 512-byte data field
		SM_PM_C  // accessing spare field
	};

	uint32_t m_page_data_size = 0;       // 256 for a 2MB card, 512 otherwise
	uint32_t m_page_total_size = 0;      // 264 for a 2MB card, 528 otherwise
	uint32_t m_num_pages = 0;            // 8192 for a 4MB card, 16184 for 8MB, 32768 for 16MB,
										// 65536 for 32MB, 131072 for 64MB, 262144 for 128MB
										// 0 means no card loaded
	int32_t m_log2_pages_per_block = 0; // log2 of number of pages per erase block (usually 4 or 5)

	std::unique_ptr<uint8_t[]> m_feeprom_data; // FEEPROM data area
	std::unique_ptr<uint8_t[]> m_data_uid_ptr; // TODO: this probably should be contained to smartmed

	sm_mode_t m_mode;                 // current operation mode
	pointer_sm_mode_t m_pointer_mode; // pointer mode

	uint32_t m_page_addr = 0; // page address pointer
	uint32_t m_byte_addr = 0;          // byte address pointer
	uint32_t m_addr_load_ptr = 0;      // address load pointer

	uint8_t m_status = 0;             // current status
	uint8_t m_accumulated_status = 0; // accumulated status

	std::unique_ptr<uint8_t[]> m_pagereg; // page register used by program command
	uint8_t m_id[5] = {};                 // chip ID
	bool m_mode_3065 = false;

	uint32_t m_program_byte_count = 0;
	uint32_t m_id_len = 0;
	uint32_t m_col_address_cycles = 0;
	uint32_t m_row_address_cycles = 0;
	uint32_t m_sequential_row_read = 0;

	// port: devcb_write_line m_write_rnb (not connected on the CV1000)
	void m_write_rnb(int) {}
};

// ---------------------------------------------------------------- RTC9701

class rtc9701_device
{
public:
	// port: device_start (the 1 Hz timer is allocated by the board, in MAME's
	// device order) and the RTC interface's set_time
	void device_start(emu_timer *timer, uint32_t clock = 32768);
	void set_time(int year, int month, int day, int day_of_week, int hour, int minute, int second)
	{
		rtc_clock_updated(year, month, day, day_of_week, hour, minute, second);
	}
	void save(state_writer &w) const;
	void load(state_reader &r);
	void nvram_load(const uint8_t *data, size_t size);   // nvram_read: 0x200 bytes
	void nvram_save(std::vector<uint8_t> &out) const;    // nvram_write

	// I/O operations
	void write_bit(int state);
	int read_bit();
	void set_cs_line(int state);
	void set_clock_line(int state);
	TIMER_CALLBACK_MEMBER(timer_callback);

	enum class state_t : u8
	{
		CMD_WAIT = 0,
		RTC_READ,
		RTC_WRITE,
		EEPROM_READ,
		EEPROM_WRITE,
		AFTER_WRITE_ENABLE

	};

	struct regs_t
	{
		uint8_t sec, min, hour, day, wday, month, year;
	};

	void device_reset();
	void nvram_default();
	inline uint8_t rtc_read(uint8_t offset);
	inline void rtc_write(uint8_t offset,uint8_t data);
	void rtc_clock_updated(int year, int month, int day, int day_of_week, int hour, int minute, int second);

	int                     m_latch = 0;
	int                     m_reset_line = CLEAR_LINE;
	int                     m_clock_line = CLEAR_LINE;

	state_t rtc_state {};
	int cmd_stream_pos = 0;
	int current_cmd = 0;

	int rtc9701_address_pos = 0;
	int rtc9701_current_address = 0;

	uint16_t rtc9701_current_data = 0;
	int rtc9701_data_pos = 0;

	uint16_t rtc9701_data[0x100];

	regs_t m_rtc {};

	emu_timer *m_timer = nullptr;
};

// ---------------------------------------------------------------- YMZ770

// port: the sound stream the YMZ770 writes into (sound_stream_update's
// views); the board brings it up to date (sound_stream::update)
struct write_stream_view {
	float *buf = nullptr;
	int n = 0;
	int samples() const { return n; }
	void put_int(int i, int32_t v, int32_t range) { buf[i] = float(v) * (1.0f / float(range)); }
};
struct read_stream_view {};
struct sound_stream {
	void (*update_cb)(void *) = nullptr;
	void *ctx = nullptr;
	void update() { if (update_cb) update_cb(ctx); }
};

class ymz770_device
{
public:
	ymz770_device() = default;
	~ymz770_device();
	// port: device_start with the "ymz770" region
	void device_start(const uint8_t *rom, size_t bytes);
	void save(state_writer &w) const;
	void load(state_reader &r);
	// generate n stereo samples at 16 kHz (sound_stream_update)
	void generate(float *l, float *r, int n);

	void write(offs_t offset, uint8_t data);

	void device_reset();
	void sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs);
	void internal_reg_write(uint8_t reg, uint8_t data);
	uint32_t get_phrase_offs(int phrase) { return m_rom[(4 * phrase) + 1] << 16 | m_rom[(4 * phrase) + 2] << 8 | m_rom[(4 * phrase) + 3]; }
	uint32_t get_seq_offs(int sqn) { return m_rom[(4 * sqn) + 1 + 0x400] << 16 | m_rom[(4 * sqn) + 2 + 0x400] << 8 | m_rom[(4 * sqn) + 3 + 0x400]; }
	void sequencer();
	uint8_t get_rom_byte(uint32_t offset) { return m_rom[offset % m_rom.bytes()]; } // need optimise or its good as is ?

	sound_stream m_stream_storage;
	sound_stream *m_stream = &m_stream_storage;
	uint32_t m_sclock = 16000;

	// data
	uint8_t m_cur_reg = 0;
	uint8_t m_mute = 0;         // mute chip
	uint8_t m_doen = 0;         // digital output enable
	uint8_t m_vlma = 0;         // overall volume L0/R0
	uint8_t m_vlma1 = 0;        // overall volume L1/R1
	uint8_t m_bsl = 0;          // boost level
	uint8_t m_cpl = 0;          // clip limiter

	// port: required_region_ptr<uint8_t> m_rom
	struct region_ptr {
		const uint8_t *p = nullptr;
		size_t n = 0;
		const uint8_t &operator[](size_t i) const { return p[i]; }
		size_t bytes() const { return n; }
		const uint8_t &operator*() const { return *p; }
	} m_rom;

	struct ymz_channel
	{
		uint16_t phrase;
		uint8_t pan;
		uint8_t pan_delay;
		uint8_t pan1;
		uint8_t pan1_delay;
		int32_t volume;
		uint8_t volume_target;
		uint8_t volume_delay;
		uint8_t volume2;
		uint8_t loop;

		bool is_playing, last_block, is_paused;

		mpeg_audio *decoder;

		int16_t output_data[0x1000];
		int output_remaining;
		int output_ptr;
		int atbl;
		int pptr;
	};

	struct ymz_sequence
	{
		uint32_t delay;
		uint16_t sequence;
		uint16_t timer;
		uint16_t stopchan;
		uint8_t loop;
		uint32_t offset;
		uint8_t bank;
		bool is_playing;
		bool is_paused;
	};

	struct ymz_sqc
	{
		uint8_t sqc;
		uint8_t loop;
		uint32_t offset;
		bool is_playing;
		bool is_waiting;
	};

	ymz_channel m_channels[16] {};
	ymz_sequence m_sequences[8] {};
	ymz_sqc m_sqcs[8] {};
};

#endif
