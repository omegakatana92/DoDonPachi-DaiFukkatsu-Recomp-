// SDL2 frontend adapter: DoDonPachi Dai-Fukkatsu Ver 1.5, Ver 1.0 and Black
// Label in one application through the shared frontend's suite API.  Every
// version is a complete fe_game on the same CV1000-D board model, whose SH-3
// program is recompiled to native code as it runs.
#include "cv1k/machine.h"
#include "../../runtime/recomp/sh3_recomp.h"

extern "C" {
#include "fe.h"
}
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifndef DFK_VERSION
#define DFK_VERSION "dev"
#endif

namespace {

std::unique_ptr<cv1k::machine> board;
bool loaded = false;
cv1k::version loaded_version = cv1k::version::ddpdfk;
bool use_interpreter = false;      // --interpreter: MAME's SH-3 interpreter instead of the recompiled code
bool recomp_error_logged = false;
std::vector<uint8_t> nv_saved;     // the non-volatile memory as last written to disk
void fifo_reset();

#define K(x) SDL_SCANCODE_##x
#define P(x) SDL_CONTROLLER_BUTTON_##x

// MAME's "cv1k" input ports: two players with an 8-way stick and four
// buttons, two coin slots, starts, Service 1 (service coin), Service 2 (the
// test button on the JAMMA edge) and the S3 test push button on the board.
// Ids are unique per version, so each keeps its own control profile.
#define PLAYER_INPUTS(pre)                                                                                  \
	{ pre "p1_up", "P1 Up", 1, cv1k::IN_P1_UP, K(UP), -1, P(DPAD_UP), 0 },                                 \
	{ pre "p1_down", "P1 Down", 1, cv1k::IN_P1_DOWN, K(DOWN), -1, P(DPAD_DOWN), 0 },                       \
	{ pre "p1_left", "P1 Left", 1, cv1k::IN_P1_LEFT, K(LEFT), -1, P(DPAD_LEFT), 0 },                       \
	{ pre "p1_right", "P1 Right", 1, cv1k::IN_P1_RIGHT, K(RIGHT), -1, P(DPAD_RIGHT), 0 },                  \
	{ pre "p1_b1", "P1 Button A", 1, cv1k::IN_P1_B1, K(Z), -1, P(A), 0 },                           \
	{ pre "p1_b2", "P1 Button B", 1, cv1k::IN_P1_B2, K(X), -1, P(B), 0 },                           \
	{ pre "p1_b3", "P1 Button C", 1, cv1k::IN_P1_B3, K(C), -1, P(X), 0 },                                  \
	{ pre "p1_b4", "P1 Button D", 1, cv1k::IN_P1_B4, K(V), -1, P(Y), 0 },                                  \
	{ pre "p1_start", "P1 Start", 1, cv1k::IN_P1_START, K(RETURN), K(1), P(START), 0 },                    \
	{ pre "p2_up", "P2 Up", 2, cv1k::IN_P2_UP, K(I), -1, P(DPAD_UP), 0 },                                  \
	{ pre "p2_down", "P2 Down", 2, cv1k::IN_P2_DOWN, K(K), -1, P(DPAD_DOWN), 0 },                          \
	{ pre "p2_left", "P2 Left", 2, cv1k::IN_P2_LEFT, K(J), -1, P(DPAD_LEFT), 0 },                          \
	{ pre "p2_right", "P2 Right", 2, cv1k::IN_P2_RIGHT, K(L), -1, P(DPAD_RIGHT), 0 },                      \
	{ pre "p2_b1", "P2 Button A", 2, cv1k::IN_P2_B1, K(F), -1, P(A), 0 },                           \
	{ pre "p2_b2", "P2 Button B", 2, cv1k::IN_P2_B2, K(G), -1, P(B), 0 },                           \
	{ pre "p2_b3", "P2 Button C", 2, cv1k::IN_P2_B3, K(H), -1, P(X), 0 },                                  \
	{ pre "p2_b4", "P2 Button D", 2, cv1k::IN_P2_B4, K(T), -1, P(Y), 0 },                                  \
	{ pre "p2_start", "P2 Start", 2, cv1k::IN_P2_START, K(2), -1, P(START), 0 },                           \
	/* coins are pulses, like a real coin switch */                                                         \
	{ pre "coin1", "Coin 1", 1, cv1k::IN_COIN1, K(5), -1, P(BACK), 5 },                                    \
	{ pre "coin2", "Coin 2", 2, cv1k::IN_COIN2, K(6), -1, P(BACK), 5 },                                    \
	{ pre "service1", "Service Coin", 0, cv1k::IN_SERVICE1, K(9), -1, -1, 5 },                             \
	{ pre "service2", "Test Button (JAMMA)", 0, cv1k::IN_SERVICE2, K(F1), -1, -1, 0 },                     \
	{ pre "test", "Test Switch (S3)", 0, cv1k::IN_TEST, K(F2), -1, -1, 0 },
const fe_input_def INPUTS_15[] = { PLAYER_INPUTS("dfk15_") };
const fe_input_def INPUTS_10[] = { PLAYER_INPUTS("dfk10_") };
const fe_input_def INPUTS_BL[] = { PLAYER_INPUTS("dfkbl_") };
const int INPUT_COUNT = int(sizeof(INPUTS_15) / sizeof(INPUTS_15[0]));

int gadget_version(char *t, size_t n) { if (!loaded) return 0; std::snprintf(t, n, "%s", cv1k::info(loaded_version).title); return 1; }
// share of the SH-3's cycles spent in recompiled native code
int gadget_native(char *t, size_t n)
{
	if (!loaded) return 0;
	if (!board->m_cpu.m_native_lookup) { std::snprintf(t, n, "SH-3: interpreter"); return 1; }
	const uint64_t a = board->m_cpu.m_native_cycles, b = board->m_cpu.m_interp_cycles;
	std::snprintf(t, n, "SH-3 native %.2f%% (%u insns)", a + b ? 100.0 * double(a) / double(a + b) : 0.0,
		board->m_recomp ? board->m_recomp->instructions() : 0u);
	return 1;
}
const fe_gadget_def GADGETS[] = {
	{ "version", "Current Game", gadget_version },
	{ "native", "Recompiled Code", gadget_native },
};
const int GADGET_COUNT = int(sizeof(GADGETS) / sizeof(GADGETS[0]));

// --- per-version entry points (the frontend's callbacks carry no context)

int validate_for(cv1k::version v, const char *path, char *err, size_t n)
{
	std::string e;
	const cv1k::version_info *other = nullptr;
	if (cv1k::check_roms(v, path, e, &other)) return 1;
	std::snprintf(err, n, "%s", e.c_str());
	return 0;
}

int load_for(cv1k::version v, const char *path, char *err, size_t n)
{
	board.reset();
	loaded = false;
	board.reset(new cv1k::machine());
	board->m_use_recompiler = !use_interpreter;
	recomp_error_logged = false;
	std::string e;
	if (!board->load(v, path, e)) {
		std::snprintf(err, n, "%s", e.c_str());
		board.reset();
		return 0;
	}
	board->power_on();
	if (!board->m_recomp_error.empty()) {
		fe_log("SH-3 recompiler off (%s); using the interpreter", board->m_recomp_error.c_str());
		recomp_error_logged = true;
	}
	else if (board->m_use_recompiler)
		fe_log("SH-3 recompiler ready: code is translated as the game reaches it");
	else
		fe_log("SH-3 on the interpreter (--interpreter)");
	nv_saved.clear();
	fifo_reset();
	loaded_version = v;
	loaded = true;
	return 1;
}

int v_15(const char *p, char *e, size_t n) { return validate_for(cv1k::version::ddpdfk, p, e, n); }
int v_10(const char *p, char *e, size_t n) { return validate_for(cv1k::version::ddpdfk10, p, e, n); }
int v_bl(const char *p, char *e, size_t n) { return validate_for(cv1k::version::dfkbl, p, e, n); }
int l_15(const char *p, char *e, size_t n) { return load_for(cv1k::version::ddpdfk, p, e, n); }
int l_10(const char *p, char *e, size_t n) { return load_for(cv1k::version::ddpdfk10, p, e, n); }
int l_bl(const char *p, char *e, size_t n) { return load_for(cv1k::version::dfkbl, p, e, n); }

void g_unload() { board.reset(); loaded = false; }

// The non-volatile memory: "DFKN", the RTC9701's EEPROM (512 bytes, MAME's
// nvram/<set>/eeprom) and the NAND flash blocks the game rewrote (none in
// normal play).
void nv_blob(std::vector<uint8_t> &out)
{
	std::vector<uint8_t> e, d;
	board->eeprom_save(e);
	board->nand_save_diff(d);
	out.assign({ 'D', 'F', 'K', 'N' });
	out.insert(out.end(), e.begin(), e.end());
	out.insert(out.end(), d.begin(), d.end());
}

void g_reset()
{
	if (!loaded) return;
	// a power cycle: the EEPROM and the flash keep their contents
	std::vector<uint8_t> e, d;
	board->eeprom_save(e);
	board->nand_save_diff(d);
	board->power_on(e.data(), e.size(), d.data(), d.size());
	fifo_reset();
}

void g_set_dips(const uint16_t *) {}

void g_set_inputs(uint32_t m) { if (loaded) board->set_inputs(cv1k::machine::filter_opposing(m)); }

// The YMZ770's stream is brought up to date when the game writes to the chip
// and at the sound manager's 50 Hz updates (as in MAME), so a frame brings
// an uneven number of samples.  The adapter waits until 75 ms have
// accumulated, hands most of it to the device as a cushion, keeps about
// 30 ms in reserve and then passes on one frame's worth per frame
// (16,000 / 60.024 = 266.6 samples), nudged to keep the reserve steady.  This changes only when
// samples reach the speaker, not what the board does.
std::vector<float> fifo;
bool fifo_primed = false;
double frac = 0.0;
constexpr size_t PRIME_SAMPLES = 1200, RESERVE_SAMPLES = 500;

void fifo_reset() { fifo.clear(); fifo_primed = false; frac = 0.0; }

int g_run(int16_t *audio, int *frames, char *, size_t)
{
	board->run_frame();
	fifo.insert(fifo.end(), board->m_audio.begin(), board->m_audio.end());
	long want = 0;
	if (!fifo_primed) {
		if (fifo.size() >= PRIME_SAMPLES) {
			fifo_primed = true;
			want = long(fifo.size() - RESERVE_SAMPLES);
		}
	} else {
		frac += double(cv1k::YMZ_RATE) / 60.024;
		want = long(frac);
		frac -= double(want);
		want += (long(fifo.size()) - long(RESERVE_SAMPLES)) / 32;   // drift correction
		if (want < 0) want = 0;
	}
	int count = int(std::min<size_t>(std::min<size_t>(size_t(want), fifo.size()), 4096));
	for (int i = 0; i < count; i++) {
		float s = fifo[size_t(i)] * 32768.0f;
		int16_t v = int16_t(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : int(s));
		audio[2 * i] = audio[2 * i + 1] = v;    // mono speaker on both channels
	}
	fifo.erase(fifo.begin(), fifo.begin() + count);
	if (frames) *frames = count;
	if (!board->m_recomp_error.empty() && !recomp_error_logged) {
		fe_log("SH-3 recompiler stopped (%s); continuing on the interpreter", board->m_recomp_error.c_str());
		recomp_error_logged = true;
	}
	return 0;
}

void g_render32(uint32_t *px)
{
	std::memcpy(px, board->m_screen.data(), board->m_screen.size() * 4);
}

uint8_t *copy_out(const std::vector<uint8_t> &st, size_t *size)
{
	uint8_t *p = static_cast<uint8_t *>(std::malloc(st.size() ? st.size() : 1));
	if (!p) return nullptr;
	std::memcpy(p, st.data(), st.size());
	*size = st.size();
	return p;
}

uint8_t *g_save(size_t *size)
{
	std::vector<uint8_t> st;
	board->save_state(st);
	return copy_out(st, size);
}

int g_validate_state(const uint8_t *d, size_t s, char *e, size_t n)
{
	if (loaded && board->validate_state(d, s)) return 1;
	std::snprintf(e, n, "This state belongs to another version, ROM revision or build of the runtime.");
	return 0;
}

int g_load_state(const uint8_t *d, size_t s, char *e, size_t n)
{
	if (board->load_state(d, s)) { fifo_reset(); return 1; }
	std::snprintf(e, n, "The state file is damaged or does not match this version.");
	return 0;
}

uint8_t *g_rewind_capture(size_t *size)
{
	std::vector<uint8_t> st;
	board->rewind_capture(st);
	if (st.empty()) return nullptr;
	return copy_out(st, size);
}

int g_rewind_restore(const uint8_t *d, size_t s, int drop)
{
	if (!board->rewind_restore(d, s, drop != 0)) return 0;
	fifo_reset();
	return 1;
}

void g_rewind_clear() { if (loaded) board->m_rewind_valid = false; }

uint32_t g_fingerprint() { return loaded ? board->m_rom.fingerprint : 0; }

size_t g_nvram_save(uint8_t *buf, size_t max)
{
	if (!loaded) return 0;
	std::vector<uint8_t> nv;
	nv_blob(nv);
	if (nv.size() > max) {
		// more rewritten flash than the frontend's buffer holds: keep the EEPROM
		fe_log("NAND flash changes too large for the NVRAM file (%zu bytes); only the EEPROM is kept", nv.size());
		nv.resize(4 + 0x200 + 4);
		std::memset(&nv[4 + 0x200], 0, 4);
	}
	std::memcpy(buf, nv.data(), nv.size());
	nv_saved = nv;
	return nv.size();
}

int g_nvram_load(const uint8_t *d, size_t s)
{
	if (!loaded || s < 4 + 0x200 || std::memcmp(d, "DFKN", 4)) return 0;
	board->power_on(d + 4, 0x200, d + 4 + 0x200, s - 4 - 0x200);
	nv_saved.assign(d, d + s);
	fifo_reset();
	return 1;
}

// written when the EEPROM or the flash differs from what was last saved
int g_nvram_dirty()
{
	if (!loaded) return 0;
	std::vector<uint8_t> nv;
	nv_blob(nv);
	return nv != nv_saved;
}

const char *const ABOUT_SUITE =
	"DoDonPachi Dai-Fukkatsu Ver 1.5, Ver 1.0 and Black Label  (CAVE / AMI, CV1000-D: SH-3, EP1C12, YMZ770)\n"
	"Version " DFK_VERSION ". The game's SH-3 program is recompiled to native code as it runs (MAME 0.264's\n"
	"SH-3 handlers, compiled in memory by TinyCC); the EP1C12 blitter, YMZ770, NAND flash and RTC9701\n"
	"follow MAME 0.264. Requires your own MAME ROM sets; no game data is distributed.\n"
	"MAME code: BSD-3-Clause. TinyCC: LGPL-2.1. Inter font: SIL OFL 1.1. SDL2: zlib. miniz: MIT.\n"
	"This project's own code is GPL-2.0. See the LICENSES folder.";

fe_game make_game(cv1k::version v)
{
	const cv1k::version_info &vi = cv1k::info(v);
	fe_game g = {};
	g.id = vi.id;
	g.title = vi.title;
	g.subtitle = "Cave CV1000-D - SH-3 program recompiled to native code";
	g.about = vi.full_title;
	g.rom_hint = vi.rom_hint;
	g.width = cv1k::VIS_W;
	g.height = cv1k::VIS_H;
	g.display_aspect = 4.0 / 3.0;
	g.rotation = 3;                                    // MAME ROT270: vertical monitor
	g.refresh_hz = 60.024;                             // set_refresh(HZ_TO_ATTOSECONDS(60.024))
	g.audio_rate = double(cv1k::YMZ_RATE);             // the YMZ770 stream, 16 kHz
	g.max_audio_frames = 4096;
	g.dip_words = 0;
	g.players = 2;
	g.inputs = v == cv1k::version::ddpdfk ? INPUTS_15 : v == cv1k::version::ddpdfk10 ? INPUTS_10 : INPUTS_BL;
	g.input_count = INPUT_COUNT;
	g.dips = nullptr;
	g.dip_count = 0;
	g.gadgets = GADGETS;
	g.gadget_count = GADGET_COUNT;
	g.validate_rom = v == cv1k::version::ddpdfk ? v_15 : v == cv1k::version::ddpdfk10 ? v_10 : v_bl;
	g.load = v == cv1k::version::ddpdfk ? l_15 : v == cv1k::version::ddpdfk10 ? l_10 : l_bl;
	g.unload = g_unload;
	g.reset = g_reset;
	g.set_dips = g_set_dips;
	g.set_inputs = g_set_inputs;
	g.run_frame = g_run;
	g.render = nullptr;
	g.render32 = g_render32;
	g.save_state = g_save;
	g.validate_state = g_validate_state;
	g.load_state = g_load_state;
	g.rom_fingerprint = g_fingerprint;
	g.runtime_version = DFK_VERSION;
	g.nvram_save = g_nvram_save;
	g.nvram_load = g_nvram_load;
	g.nvram_dirty = g_nvram_dirty;
	g.rewind_capture = g_rewind_capture;
	g.rewind_restore = g_rewind_restore;
	g.rewind_clear = g_rewind_clear;
	g.profile_name = vi.id;
	return g;
}

} // namespace

int main(int argc, char **argv)
{
	// --interpreter (or DFK_INTERPRETER=1): run the SH-3 on MAME's
	// interpreter; the flag is removed before the frontend parses the rest
	int out = 1;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--interpreter")) use_interpreter = true;
		else argv[out++] = argv[i];
		if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h"))
			std::printf("DoDonPachi Dai-Fukkatsu Recomp " DFK_VERSION "\n  --interpreter     run the SH-3 program on MAME's interpreter instead of the recompiled code\n");
	}
	argc = out;
	if (const char *e = std::getenv("DFK_INTERPRETER")) use_interpreter = use_interpreter || (*e && *e != '0');
	static fe_game variants[cv1k::VERSION_COUNT] = {
		make_game(cv1k::version::ddpdfk),
		make_game(cv1k::version::ddpdfk10),
		make_game(cv1k::version::dfkbl),
	};
	fe_suite suite = {};
	suite.id = "ddpdfk";
	suite.title = "DoDonPachi Dai-Fukkatsu Recomp";
	suite.item_word = nullptr;          // the frontend then says "Version" throughout (Switch Version, ...)
	suite.select_title = "Select Version";
	suite.about = ABOUT_SUITE;
	suite.variants = variants;
	suite.variant_count = cv1k::VERSION_COUNT;
	int rc = fe_run_suite(&suite, argc, argv);
	board.reset();
	return rc;
}
