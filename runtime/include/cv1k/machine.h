// Cave CV1000-D board model for DoDonPachi Dai-Fukkatsu (MAME sets ddpdfk,
// ddpdfk10, dfkbl).
//
// Reference: MAME 0.264 src/mame/cave/cv1k.cpp (cv1k_d machine config,
// cv1k_d_map, cv1k_port, the input ports, init_ddpdfk's idle-loop speedup)
// and the devices it uses: the SH-3 (runtime/sh3), the EP1C12 blitter, the
// NAND flash, the RTC9701 and the YMZ770 (runtime/devices).  The machine owns
// the scheduler and every device; run_frame() advances to the next VBLANK
// the way MAME's timeslice loop does.  One machine runs one version at a
// time; the version is chosen when the ROMs are loaded.
#ifndef CV1K_MACHINE_H
#define CV1K_MACHINE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../../emu/kemu.h"
#include "../../emu/state_io.h"
#include "../../sh3/sh3.h"
#include "../../devices/cv1k_devices.h"
#include "../../devices/ep1c12/ep1c12.h"

class sh3_recompiler;

namespace cv1k {

enum class version : int { ddpdfk = 0, ddpdfk10 = 1, dfkbl = 2 };
constexpr int VERSION_COUNT = 3;

struct rom_file {
	const char *name;      // MAME file name
	uint32_t size, crc;
	int region;            // 0 maincpu, 1 nand, 2 ymz770
	uint32_t offset;
	bool word_swap;        // ROM_LOAD16_WORD_SWAP
};

struct version_info {
	version v;
	const char *id;            // profile / data id
	const char *mame_set;      // MAME 0.264 set name
	const char *title;         // short title
	const char *full_title;    // MAME description
	const char *rom_hint;      // usual archive name
	const rom_file *files;     // the set's ROMs (4)
	uint32_t idle_ram, idle_pc;   // install_speedups(idleramoff, idlepc, is_typed)
};
const version_info &info(version v);
const version_info *find_version(const char *id_or_set);

constexpr uint32_t CPU_CLOCK = 102400000;            // 12.8_MHz_XTAL * 8
constexpr uint32_t YMZ_CLOCK = 16384000;             // 16.384_MHz_XTAL (stream at 16 kHz)
constexpr uint32_t YMZ_RATE = 16000;
constexpr int SCREEN_W = 512, SCREEN_H = 512;        // set_size(512, 512)
constexpr int VIS_W = 320, VIS_H = 240;              // set_visarea(0, 319, 0, 239)
constexpr uint32_t MAIN_RAM_SIZE = 0x1000000;        // cv1k_d: 16 MB

// inputs, active high (the board model inverts the active-low bits); the
// order of tools/cv1k_reference.lua
enum : uint32_t {
	IN_P1_UP = 1u << 0, IN_P1_DOWN = 1u << 1, IN_P1_LEFT = 1u << 2, IN_P1_RIGHT = 1u << 3,
	IN_P1_B1 = 1u << 4, IN_P1_B2 = 1u << 5, IN_P1_B3 = 1u << 6, IN_P1_B4 = 1u << 7,
	IN_P2_UP = 1u << 8, IN_P2_DOWN = 1u << 9, IN_P2_LEFT = 1u << 10, IN_P2_RIGHT = 1u << 11,
	IN_P2_B1 = 1u << 12, IN_P2_B2 = 1u << 13, IN_P2_B3 = 1u << 14, IN_P2_B4 = 1u << 15,
	IN_COIN1 = 1u << 16, IN_COIN2 = 1u << 17, IN_P1_START = 1u << 18, IN_P2_START = 1u << 19,
	IN_SERVICE1 = 1u << 20, IN_SERVICE2 = 1u << 21, IN_TEST = 1u << 22,
};
constexpr int PLAYER_SHIFT = 8;

struct rom_set {
	version v = version::ddpdfk;
	std::vector<uint64_t> prog;     // "maincpu" 0x400000, as MAME holds it (64-bit words, host order)
	std::vector<uint8_t> nand;      // "nand" 0x8400000
	std::vector<uint8_t> ymz;       // "ymz770" 0x800000 (bytes in the chip's order)
	uint32_t fingerprint = 0;       // CRC32 of the program ROM file
};

bool check_roms(version v, const char *path, std::string &err, const version_info **found_other = nullptr);
bool load_roms(version v, const char *path, rom_set &out, std::string &err);
const version_info *identify_roms(const char *path);

// stream_buffer time bookkeeping of one MAME sound_stream (sound.cpp)
struct stream_clock {
	uint32_t rate = 0;
	kemu::attoseconds_t sample_attos = 0;
	int32_t end_second = 0;
	uint32_t end_sample = 0;
	uint64_t total = 0;
	void set_rate(uint32_t r);
	kemu::attotime end_time() const { return kemu::attotime(end_second, kemu::attoseconds_t(end_sample) * sample_attos); }
	uint32_t advance_to(const kemu::attotime &t);
};

class machine {
public:
	machine();
	~machine();
	bool load(version v, const char *path, std::string &err);
	version current_version() const { return m_rom.v; }
	// device start + machine reset.  The RTC9701's EEPROM (settings, high
	// scores) is set from `eeprom` when it has the right size (0x200 bytes,
	// MAME's nvram/<set>/eeprom), otherwise to erased (0xffff words); the
	// NAND flash from the set's ROM plus `nand_diff` (see nand_save_diff).
	void power_on(const uint8_t *eeprom = nullptr, size_t eeprom_size = 0,
	              const uint8_t *nand_diff = nullptr, size_t nand_diff_size = 0);
	void run_frame();                // until the next VBLANK has been handled
	void set_inputs(uint32_t in) { m_inputs = in; }
	// MAME's digital joystick (ioport.cpp, without -joystick_contradictory)
	static uint32_t filter_opposing(uint32_t in)
	{
		for (int p = 0; p < 2; p++) {
			const uint32_t up = uint32_t(IN_P1_UP) << (PLAYER_SHIFT * p), down = uint32_t(IN_P1_DOWN) << (PLAYER_SHIFT * p);
			const uint32_t left = uint32_t(IN_P1_LEFT) << (PLAYER_SHIFT * p), right = uint32_t(IN_P1_RIGHT) << (PLAYER_SHIFT * p);
			if ((in & (up | down)) == (up | down)) in &= ~(up | down);
			if ((in & (left | right)) == (left | right)) in &= ~(left | right);
		}
		return in;
	}
	void set_dsw(uint8_t d) { m_dsw = d; }
	// clock time used by the next power_on() (default: the host's local time)
	void set_rtc_time(int year, int month, int day, int hour, int minute, int second)
	{ m_rtc_fixed = true; m_rtc_set[0] = year; m_rtc_set[1] = month; m_rtc_set[2] = day; m_rtc_set[3] = hour; m_rtc_set[4] = minute; m_rtc_set[5] = second; }
	bool m_rtc_fixed = false;
	int m_rtc_set[6] = {};

	// non-volatile memory: the RTC9701 EEPROM, and the NAND flash blocks
	// rewritten since power-on (as "index + block" records)
	void eeprom_save(std::vector<uint8_t> &out) const { m_rtc.nvram_save(out); }
	void nand_save_diff(std::vector<uint8_t> &out) const;
	uint32_t nv_writes() const { return m_nv_writes; }

	// state
	void save_state(std::vector<uint8_t> &out);
	bool load_state(const uint8_t *data, size_t size);
	bool validate_state(const uint8_t *data, size_t size) const;
	bool m_in_restore = false;
	// rewind history support (see state.cpp): capture every few frames,
	// restore newest first; `pop` when the frontend drops that capture
	void rewind_reset();
	void rewind_capture(std::vector<uint8_t> &out);
	bool rewind_restore(const uint8_t *data, size_t size, bool pop);
	std::vector<uint64_t> m_rw_ram;
	std::vector<uint32_t> m_rw_vram;
	bool m_rewind_valid = false;
	void save_core(state_writer &w, bool light);
	bool load_core(state_reader &r, bool light);

	// audio produced since the last call: mono at audio_rate(), MAME mixer scaling
	std::vector<float> m_audio;
	uint32_t audio_rate() const { return YMZ_RATE; }

	// picture of the last VBLANK (320 x 240, 0x00RRGGBB)
	std::vector<uint32_t> m_screen;
	bool m_render = true;                 // draw the picture at each VBLANK

	// debug / comparison
	const uint64_t *main_ram() const { return m_ram.get(); }
	void main_ram_bytes(std::vector<uint8_t> &out) const;   // logical (big-endian) byte order
	uint64_t frame_number() const { return m_frame; }
	void (*on_vblank_hook)(machine &m, void *ctx) = nullptr;
	void *hook_ctx = nullptr;
	kemu::scheduler &sched() { return m_sched; }

	// SH-3 program: recompiled to native code at load time (default), or the
	// interpreter alone (set before power_on)
	bool m_use_recompiler = true;
	std::unique_ptr<sh3_recompiler> m_recomp;
	bool m_recomp_ready = false;
	std::string m_recomp_error;
	void recomp_end_of_frame();
	const uint8_t *m_codemap = nullptr;    // the recompiler's map of RAM holding translated code
	void code_check(uint32_t a, uint32_t bytes);

	rom_set m_rom;

	// --- internals (public for the bus trampolines) ---
	kemu::scheduler m_sched;
	sh3_device m_cpu;
	kemu::exec_device m_cpu_exec;
	rtc9701_device m_rtc;
	nand_device m_nand;
	ymz770_device m_ymz;
	ep1c12_device m_blitter;
	std::unique_ptr<uint64_t[]> m_ram;       // 0x0c000000-0x0cffffff "mainram"
	std::unique_ptr<uint64_t[]> m_cache_ram; // 0xf0000000-0xf0ffffff
	uint32_t m_inputs = 0;
	uint8_t m_dsw = 0;
	uint32_t m_eepromout = 0;                // the EEPROMOUT output port
	uint8_t m_eepromout_old[3] = {0, 0, 1};  // dynamic_field::m_oldval of its three fields
	uint32_t m_nv_writes = 0;
	uint32_t m_idle_ram = 0, m_idle_pc = 0;

	// sound stream
	stream_clock m_ymz_clock;
	void ymz_update();
	kemu::timer *m_sound_update_timer = nullptr;
	void sound_manager_update();

	// screen
	kemu::attoseconds_t m_frame_period = 0, m_scantime = 0, m_pixeltime = 0, m_vblank_period = 0;
	kemu::attotime m_vblank_start_time, m_vblank_end_time;
	kemu::timer *m_vblank_begin_timer = nullptr, *m_vblank_end_timer = nullptr, *m_scanline0_timer = nullptr;
	uint64_t m_frame = 0;
	bool m_frame_done = false;
	kemu::attotime time_until_pos(int vpos, int hpos = 0) const;
	void vblank_begin();
	void vblank_end();
	void scanline0();
	void screen_update();

	// buses (address after the SH-3's masking)
	uint8_t read8(uint32_t a);
	uint16_t read16(uint32_t a);
	uint32_t read32(uint32_t a);
	uint64_t read64(uint32_t a);
	void write8(uint32_t a, uint8_t d);
	void write16(uint32_t a, uint16_t d);
	void write32(uint32_t a, uint32_t d);
	void write64(uint32_t a, uint64_t d);
	uint64_t dev_read(uint32_t a, uint64_t mask);           // everything but RAM / ROM, one 64-bit lane
	void dev_write(uint32_t a, uint64_t d, uint64_t mask);
	uint64_t io_read(uint32_t port);
	void io_write(uint32_t port, uint64_t d);
	uint64_t speedup_r();
	uint8_t flash_io_r(uint32_t offset);
	void flash_io_w(uint32_t offset, uint8_t data);
	uint8_t serial_rtc_eeprom_r(uint32_t offset);
	void serial_rtc_eeprom_w(uint32_t offset, uint8_t data);
	uint32_t port_c() const;
	uint32_t port_d() const;
	uint32_t port_f() const;
	uint32_t port_l() const;

	std::vector<kemu::timer *> m_timers;   // every non-temporary timer, creation order
private:
	void start_devices(const uint8_t *eeprom, size_t eeprom_size);
	void reset_machine();
};

} // namespace cv1k

#endif
