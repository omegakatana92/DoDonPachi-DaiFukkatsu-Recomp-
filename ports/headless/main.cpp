// Deterministic headless runner for the DoDonPachi Dai-Fukkatsu port: runs
// the CV1000 board model for one version and writes frame hashes, PNG frame
// grabs, WAV audio, state and NVRAM files.  It is also the comparison tool
// against MAME 0.264 reference runs (see tools/cv1k_reference.lua).
#include "cv1k/machine.h"
#include "miniz.h"
#include "../../runtime/recomp/sh3_recomp.h"

#include <cinttypes>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct options {
	const char *rom = nullptr;
	const char *version = "ddpdfk";
	int frames = 600;
	const char *inputs = nullptr;
	bool mame_latency = false;
	int hash_every = 0;
	std::map<int, std::string> png;
	std::map<int, std::string> dump;
	const char *wav = nullptr;
	const char *compare = nullptr;
	int state_test = -1;
	int rewind_test = -1;
	int save_at = -1;
	std::string save_file;
	const char *load_file = nullptr;
	const char *eeprom = nullptr;
	const char *save_eeprom = nullptr;
	bool quiet = false;
	bool interp = false;
	bool rotated = false;
	int stop_after_diffs = 3;
	int trace_from = -1, trace_to = -1;
	const char *trace_file = nullptr;
};
FILE *g_trace = nullptr;
std::map<uint32_t, uint64_t> g_iprof;
void iprof(sh3_device *, uint32_t pc) { g_iprof[pc]++; }
int g_vram_at = -1;
const char *g_vram_file = nullptr;
void trace_hook(sh3_device *, offs_t pc) { if (g_trace) std::fprintf(g_trace, "%08X\n", pc); }

uint64_t block_hash(const uint8_t *p, size_t n)
{
	// tools/hashdump.c
	uint64_t h = 1469598103934665603ULL;
	size_t i = 0;
	for (; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, p + i, 8); h ^= w; h *= 0x9E3779B97F4A7C15ULL; h ^= h >> 29; }
	for (; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
	return h;
}

uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
	const uint8_t *p = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
	return h;
}

// the 320x240 screen; --rotated gives MAME's ROT270 snapshot orientation (240x320)
bool write_png(const char *path, const cv1k::machine &m, bool rot)
{
	const int sw = cv1k::VIS_W, sh = cv1k::VIS_H;
	const int w = rot ? sh : sw, h = rot ? sw : sh;
	std::vector<uint8_t> rgb(size_t(w) * h * 3);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int sx = rot ? sw - 1 - y : x, sy = rot ? x : y;
			uint32_t c = m.m_screen[size_t(sy) * sw + sx];
			uint8_t *p = &rgb[(size_t(y) * w + x) * 3];
			p[0] = uint8_t(c >> 16); p[1] = uint8_t(c >> 8); p[2] = uint8_t(c);
		}
	size_t zl = 0;
	void *z = tdefl_write_image_to_png_file_in_memory(rgb.data(), w, h, 3, &zl);
	if (!z) return false;
	FILE *f = std::fopen(path, "wb");
	if (!f) { mz_free(z); return false; }
	std::fwrite(z, 1, zl, f);
	std::fclose(f);
	mz_free(z);
	return true;
}

struct wav_writer {
	FILE *f = nullptr;
	uint32_t frames = 0, rate = 0;
	bool open(const char *path, uint32_t r)
	{
		rate = r;
		f = std::fopen(path, "wb");
		if (!f) return false;
		uint8_t hdr[44] = {};
		std::fwrite(hdr, 1, 44, f);
		return true;
	}
	void add(const std::vector<float> &s)
	{
		for (float v : s) {
			float c = v * 32767.0f;
			if (c > 32767.0f) c = 32767.0f;
			if (c < -32768.0f) c = -32768.0f;
			int16_t x = int16_t(c);
			std::fwrite(&x, 2, 1, f);
		}
		frames += uint32_t(s.size());
	}
	void close()
	{
		if (!f) return;
		auto le32 = [&](uint32_t v) { uint8_t b[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) }; std::fwrite(b, 1, 4, f); };
		auto le16 = [&](uint16_t v) { uint8_t b[2] = { uint8_t(v), uint8_t(v >> 8) }; std::fwrite(b, 1, 2, f); };
		std::fseek(f, 0, SEEK_SET);
		std::fwrite("RIFF", 1, 4, f); le32(36 + frames * 2); std::fwrite("WAVEfmt ", 1, 8, f);
		le32(16); le16(1); le16(1); le32(rate); le32(rate * 2); le16(2); le16(16);
		std::fwrite("data", 1, 4, f); le32(frames * 2);
		std::fclose(f);
		f = nullptr;
	}
};

// ---------------------------------------------------------------- MAME reference

struct reference {
	std::map<int, std::vector<uint32_t>> cpu;            // cpu.txt: frame -> registers
	std::map<int, std::vector<uint64_t>> hashes;         // hashes.bin: frame -> 256 block hashes
	bool load(const char *dir)
	{
		std::string p = std::string(dir) + "/cpu.txt";
		FILE *f = std::fopen(p.c_str(), "r");
		if (!f) return false;
		char line[1024];
		while (std::fgets(line, sizeof line, f)) {
			char *s = line;
			int fr = int(std::strtol(s, &s, 10));
			std::vector<uint32_t> v;
			for (;;) {
				char *e;
				unsigned long x = std::strtoul(s, &e, 16);
				if (e == s) break;
				v.push_back(uint32_t(x));
				s = e;
			}
			cpu[fr] = v;
		}
		std::fclose(f);
		p = std::string(dir) + "/hashes.bin";
		f = std::fopen(p.c_str(), "rb");
		if (f) {
			uint32_t hdr[2];
			while (std::fread(hdr, 4, 2, f) == 2) {
				std::vector<uint64_t> h(hdr[1]);
				if (std::fread(h.data(), 8, hdr[1], f) != hdr[1]) break;
				hashes[int(hdr[0])] = h;
			}
			std::fclose(f);
		}
		return true;
	}
};

const char *const REG_NAMES[] = { "PC","SR","PR","GBR","VBR","MACH","MACL","R0","R1","R2","R3","R4","R5","R6","R7",
	"R8","R9","R10","R11","R12","R13","R14","R15","SPC","SSR","SGR" };

std::vector<uint32_t> cpu_regs(cv1k::machine &m)
{
	internal_sh2_state *s = m.m_cpu.state();
	std::vector<uint32_t> v = { s->pc, s->sr, s->pr, s->gbr, s->vbr, s->mach, s->macl };
	for (int i = 0; i < 16; i++) v.push_back(s->r[i]);
	v.push_back(s->m_spc); v.push_back(s->m_ssr); v.push_back(s->m_sgr);
	return v;
}

struct run_ctx {
	options *o = nullptr;
	cv1k::machine *m = nullptr;
	reference ref;
	bool have_ref = false;
	int cpu_compared = 0, cpu_diff = 0, ram_compared = 0, ram_diff = 0, first_bad = -1;
	std::vector<uint8_t> ram;
	uint64_t frame_hash = 0;
	FILE *hashlog = nullptr;
};
run_ctx g;

void on_vblank(cv1k::machine &m, void *)
{
	const int fr = int(m.frame_number());
	if (g.have_ref) {
		auto it = g.ref.cpu.find(fr);
		if (it != g.ref.cpu.end()) {
			std::vector<uint32_t> mine = cpu_regs(m);
			g.cpu_compared++;
			bool same = true;
			for (size_t i = 0; i < mine.size() && i < it->second.size(); i++)
				same = same && mine[i] == it->second[i];
			if (!same) {
				g.cpu_diff++;
				if (g.first_bad < 0) g.first_bad = fr;
				if (g.cpu_diff <= g.o->stop_after_diffs) {
					std::printf("frame %d: CPU registers differ:", fr);
					for (size_t i = 0; i < mine.size() && i < it->second.size(); i++)
						if (mine[i] != it->second[i])
							std::printf(" %s %08X (MAME %08X)", REG_NAMES[i], mine[i], it->second[i]);
					std::printf("\n");
				}
			}
		}
		auto hit = g.ref.hashes.find(fr);
		if (hit != g.ref.hashes.end()) {
			m.main_ram_bytes(g.ram);
			g.ram_compared++;
			int bad = 0;
			std::string blocks;
			for (size_t b = 0; b < hit->second.size() && b * 65536 < g.ram.size(); b++)
				if (block_hash(&g.ram[b * 65536], 65536) != hit->second[b]) {
					if (bad < 8) { char t[16]; std::snprintf(t, sizeof t, " %06zX", b * 65536); blocks += t; }
					bad++;
				}
			if (bad) {
				g.ram_diff++;
				if (g.first_bad < 0) g.first_bad = fr;
				if (g.ram_diff <= g.o->stop_after_diffs)
					std::printf("frame %d: main RAM differs in %d blocks of 64 KB:%s\n", fr, bad, blocks.c_str());
			}
		}
	}
	if (g.o->trace_file && fr == g.o->trace_from) { g_trace = std::fopen(g.o->trace_file, "w"); m.m_cpu.m_trace_hook = trace_hook; }
	if (g_trace && fr == g.o->trace_to) { std::fclose(g_trace); g_trace = nullptr; m.m_cpu.m_trace_hook = nullptr; }
	auto d = g.o->dump.find(fr);
	if (d != g.o->dump.end()) {
		m.main_ram_bytes(g.ram);
		FILE *f = std::fopen(d->second.c_str(), "wb");
		if (f) { std::fwrite(g.ram.data(), 1, g.ram.size(), f); std::fclose(f); }
	}
	if (g.o->hash_every > 0 && fr % g.o->hash_every == 0) {
		m.main_ram_bytes(g.ram);
		uint64_t h = fnv1a(0xcbf29ce484222325ull, g.ram.data(), g.ram.size());
		h = fnv1a(h, m.m_screen.data(), m.m_screen.size() * 4);
		g.frame_hash = fnv1a(g.frame_hash ? g.frame_hash : 0xcbf29ce484222325ull, &h, 8);
		if (g.hashlog) std::fprintf(g.hashlog, "%d %016" PRIx64 "\n", fr, h);
	}
}

// the next frame produced is `frame`
bool fr_next_is(const cv1k::machine &m, int frame) { return frame >= 0 && int(m.frame_number()) == frame; }

std::vector<std::pair<int, uint32_t>> read_inputs(const char *path)
{
	std::vector<std::pair<int, uint32_t>> ev;
	FILE *f = std::fopen(path, "r");
	if (!f) return ev;
	int fr; unsigned long mask;
	while (std::fscanf(f, "%d %lu", &fr, &mask) == 2)
		ev.push_back({ fr, uint32_t(mask) });
	std::fclose(f);
	return ev;
}

bool read_file(const char *path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path, "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize(size_t(n < 0 ? 0 : n));
	bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
	std::fclose(f);
	return ok;
}
bool write_file(const char *path, const std::vector<uint8_t> &d)
{
	FILE *f = std::fopen(path, "wb");
	if (!f) return false;
	bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
	std::fclose(f);
	return ok;
}

void usage()
{
	std::printf(
		"cv1k_headless --version ID --rom PATH [options]\n"
		"  --version ID        ddpdfk | ddpdfk10 | dfkbl\n"
		"  --frames N          frames to run (default 600)\n"
		"  --inputs FILE       \"FRAME MASK\" lines (bit order of cv1k::IN_*)\n"
		"  --mame-latency      apply input changes one frame later, as MAME's Lua does\n"
		"  --rtc \"Y-M-D h:m:s\" clock at power-on (default: the host's time)\n"
		"  --compare DIR       compare with a MAME reference (tools/cv1k_reference.lua)\n"
		"  --png FRAME FILE    write the screen at FRAME (repeatable); --rotated as MAME's snapshot\n"
		"  --dump FRAME FILE   write main RAM at FRAME\n"
		"  --wav FILE          write the audio\n"
		"  --hash N            hash RAM + screen every N frames (printed at the end)\n"
		"  --hash-log FILE     write the per-frame hashes\n"
		"  --interp            run the SH-3 on MAME's interpreter only\n"
		"  --state-test FRAME  save at FRAME, run 300 frames with other inputs, load, re-save: must be identical\n"
		"  --rewind-test FRAME states every 4 frames for 120 frames, reloaded newest first\n"
		"  --save-state FRAME FILE / --load-state FILE\n"
		"  --eeprom FILE / --save-eeprom FILE\n");
}

} // namespace

int main(int argc, char **argv)
{
	options o;
	const char *rtc = nullptr;
	const char *hashlog = nullptr;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--rom") o.rom = next();
		else if (a == "--version") o.version = next();
		else if (a == "--frames") o.frames = std::atoi(next());
		else if (a == "--inputs") o.inputs = next();
		else if (a == "--mame-latency") o.mame_latency = true;
		else if (a == "--rtc") rtc = next();
		else if (a == "--compare") o.compare = next();
		else if (a == "--png") { int f = std::atoi(next()); o.png[f] = next(); }
		else if (a == "--dump") { int f = std::atoi(next()); o.dump[f] = next(); }
		else if (a == "--rotated") o.rotated = true;
		else if (a == "--wav") o.wav = next();
		else if (a == "--hash") o.hash_every = std::atoi(next());
		else if (a == "--hash-log") hashlog = next();
		else if (a == "--interp") o.interp = true;
		else if (a == "--state-test") o.state_test = std::atoi(next());
		else if (a == "--rewind-test") o.rewind_test = std::atoi(next());
		else if (a == "--save-state") { o.save_at = std::atoi(next()); o.save_file = next(); }
		else if (a == "--load-state") o.load_file = next();
		else if (a == "--eeprom") o.eeprom = next();
		else if (a == "--save-eeprom") o.save_eeprom = next();
		else if (a == "--diffs") o.stop_after_diffs = std::atoi(next());
		else if (a == "--quiet") o.quiet = true;
		else if (a == "--vram-dump") { g_vram_at = std::atoi(next()); g_vram_file = next(); }
		else if (a == "--trace") { o.trace_from = std::atoi(next()); o.trace_to = std::atoi(next()); o.trace_file = next(); }
		else { usage(); return 2; }
	}
	const cv1k::version_info *vi = cv1k::find_version(o.version);
	if (!vi || !o.rom) { usage(); return 2; }

	static cv1k::machine m;
	std::string err;
	auto t0 = std::chrono::steady_clock::now();
	if (!m.load(vi->v, o.rom, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
	if (rtc) {
		int Y, M, D, h, mi, s;
		if (std::sscanf(rtc, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &mi, &s) == 6)
			m.set_rtc_time(Y, M, D, h, mi, s);
	}
	m.m_use_recompiler = !o.interp;
	std::vector<uint8_t> eeprom;
	if (o.eeprom) read_file(o.eeprom, eeprom);
	m.power_on(eeprom.empty() ? nullptr : eeprom.data(), eeprom.size());
	auto t1 = std::chrono::steady_clock::now();
	if (!o.quiet) {
		std::printf("%s: loaded and started in %.2f s", vi->title, std::chrono::duration<double>(t1 - t0).count());
		if (!o.interp) std::printf(" (recompiler: %s)", m.m_recomp_ready ? "ready" : m.m_recomp_error.c_str());
		std::printf("\n");
	}

	g.o = &o;
	g.m = &m;
	if (o.compare) {
		g.have_ref = g.ref.load(o.compare);
		if (!g.have_ref) { std::fprintf(stderr, "cannot read %s/cpu.txt\n", o.compare); return 1; }
	}
	if (hashlog) g.hashlog = std::fopen(hashlog, "w");
	if (std::getenv("CV1K_IPROF")) m.m_cpu.m_interp_profile = iprof;
	m.on_vblank_hook = on_vblank;

	std::vector<std::pair<int, uint32_t>> ev;
	if (o.inputs) ev = read_inputs(o.inputs);
	size_t ei = 0;
	uint32_t held = 0;
	wav_writer wav;
	if (o.wav) wav.open(o.wav, m.audio_rate());

	if (o.load_file) {
		std::vector<uint8_t> st;
		if (!read_file(o.load_file, st) || !m.load_state(st.data(), st.size())) { std::fprintf(stderr, "cannot load %s\n", o.load_file); return 1; }
	}
	int start = int(m.frame_number());
	int status = 0, status_state = 0;
	for (int f = start; f < o.frames; f++) {
		// frame f + 1 is being produced.  MAME's Lua sets the inputs of event
		// frame E at the frame notifier of frame E, after the input manager's
		// own notifier has latched the ports: they reach the game at the
		// notifier of frame E + 1, so from frame E + 2 on
		const int applies = o.mame_latency ? f - 1 : f + 1;
		while (ei < ev.size() && ev[ei].first <= applies) { held = ev[ei].second; ei++; }
		m.set_inputs(held);
		if (fr_next_is(m, o.state_test)) {
			// save, run 300 frames with other inputs, load, save again
			std::vector<uint8_t> a, b;
			auto ts = std::chrono::steady_clock::now();
			m.save_state(a);
			const double save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
			auto saved_hook = m.on_vblank_hook;
			m.on_vblank_hook = nullptr;
			for (int k = 0; k < 300; k++) { m.set_inputs(uint32_t(k * 2654435761u) & 0x7ffff); m.run_frame(); }
			ts = std::chrono::steady_clock::now();
			const bool ok = m.load_state(a.data(), a.size());
			const double load_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
			m.on_vblank_hook = saved_hook;
			m.save_state(b);
			const bool same = ok && a == b;
			std::printf("state test at frame %d: %s (%.1f MB, save %.0f ms, load %.0f ms)\n", int(m.frame_number()),
				!ok ? "LOAD FAILED" : same ? "re-save identical" : "re-save DIFFERENT", a.size() / 1e6, save_ms, load_ms);
			if (!same) status_state = 4;
			m.set_inputs(held);
		}
		if (fr_next_is(m, o.rewind_test)) {
			// the frontend's rewind: a capture every 4 frames for 120 frames,
			// then stepped back newest first (each dropped from the history,
			// but the oldest).  Each must give the full state of its time
			// exactly; then the same 120 frames again must end the same.
			auto saved_hook = m.on_vblank_hook;
			m.on_vblank_hook = nullptr;
			std::vector<std::vector<uint8_t>> full, cap;
			std::vector<uint32_t> in;
			m.rewind_reset();
			size_t cap_bytes = 0;
			double cap_ms = 0, rest_ms = 0;
			for (int k = 0; k <= 30; k++) {
				full.emplace_back();
				m.save_state(full.back());
				cap.emplace_back();
				auto ts = std::chrono::steady_clock::now();
				m.rewind_capture(cap.back());
				cap_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
				cap_bytes += cap.back().size();
				if (k == 30) break;
				for (int j = 0; j < 4; j++) { uint32_t x = uint32_t((k * 4 + j) * 2654435761u) & 0x3ffff; in.push_back(x); m.set_inputs(x); m.run_frame(); }
			}
			int bad = 0;
			for (int k = 30; k >= 0; k--) {
				auto ts = std::chrono::steady_clock::now();
				const bool ok = m.rewind_restore(cap[size_t(k)].data(), cap[size_t(k)].size(), k > 0);
				rest_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
				std::vector<uint8_t> x;
				m.save_state(x);
				if (!ok || x != full[size_t(k)]) { bad++; if (bad < 4) std::printf("  rewind to capture %d: %s\n", k, ok ? "state differs" : "restore failed"); }
			}
			for (int j = 0; j < 120; j++) { m.set_inputs(in[size_t(j)]); m.run_frame(); }
			std::vector<uint8_t> x;
			m.save_state(x);
			if (x != full[30]) { bad++; std::printf("  replay after rewinding: state differs\n"); }
			m.load_state(full[0].data(), full[0].size());
			m.on_vblank_hook = saved_hook;
			std::printf("rewind test at frame %d: %s (31 captures, %.1f MB in all, %.1f ms per capture, %.1f ms per step)\n",
				int(m.frame_number()), bad ? "DIFFERENT" : "every state reproduced", cap_bytes / 1e6, cap_ms / 31, rest_ms / 31);
			if (bad) status_state = 4;
			m.set_inputs(held);
		}
		m.run_frame();
		if (o.wav) wav.add(m.m_audio);
		const int fr = int(m.frame_number());
		auto p = o.png.find(fr);
		if (p != o.png.end()) write_png(p->second.c_str(), m, o.rotated);
		if (fr == g_vram_at) {
			m.m_blitter.wait_for_blit();
			FILE *vf = std::fopen(g_vram_file, "wb");
			if (vf) { std::fwrite(&m.m_blitter.m_bitmaps->pix(0, 0), 4, size_t(0x2000) * 0x1000, vf); std::fclose(vf); }
		}
		if (fr == o.save_at) {
			std::vector<uint8_t> st;
			m.save_state(st);
			write_file(o.save_file.c_str(), st);
		}
		if (g.have_ref && g.first_bad >= 0 && (g.cpu_diff + g.ram_diff) >= o.stop_after_diffs * 2 + 4)
			break;
	}
	auto t2 = std::chrono::steady_clock::now();
	if (o.wav) wav.close();
	if (o.save_eeprom) { std::vector<uint8_t> e; m.eeprom_save(e); write_file(o.save_eeprom, e); }
	const double secs = std::chrono::duration<double>(t2 - t1).count();
	const int ran = int(m.frame_number()) - start;
	if (!o.quiet)
		std::printf("%d frames in %.2f s (%.1f fps, %.0f%% of real time)\n", ran, secs, ran / secs, 100.0 * ran / secs / 60.024);
	if (!o.interp && !o.quiet && m.m_cpu.m_native_cycles + m.m_cpu.m_interp_cycles)
		std::printf("SH-3 cycles: %.3f%% in recompiled code (%llu instructions on the interpreter)\n",
			100.0 * double(m.m_cpu.m_native_cycles) / double(m.m_cpu.m_native_cycles + m.m_cpu.m_interp_cycles),
			(unsigned long long)m.m_cpu.m_interp_insns);
	if (m.m_recomp && !o.quiet)
		std::printf("recompiler: %u instructions in %u modules (%u dropped), %.0f ms compiling, %.1f MB of C, %llu entries\n",
			m.m_recomp->instructions(), m.m_recomp->modules_built(), m.m_recomp->modules_dropped(),
			m.m_recomp->compile_ms(), m.m_recomp->source_bytes() / 1e6, (unsigned long long)m.m_cpu.m_native_calls);
	if (!m.m_recomp_error.empty() && !o.interp)
		std::printf("recompiler error: %s\n", m.m_recomp_error.c_str());
	if (!g_iprof.empty()) {
		std::vector<std::pair<uint64_t, uint32_t>> v;
		for (auto &e : g_iprof) v.push_back({e.second, e.first});
		std::sort(v.rbegin(), v.rend());
		for (size_t i = 0; i < v.size() && i < 12; i++) std::printf("interp %08X %llu\n", v[i].second, (unsigned long long)v[i].first);
	}
	if (o.hash_every > 0) std::printf("hash %016" PRIx64 "\n", g.frame_hash);
	if (g.have_ref) {
		std::printf("compared: CPU registers at %d frames (%d differ), main RAM at %d frames (%d differ)\n",
			g.cpu_compared, g.cpu_diff, g.ram_compared, g.ram_diff);
		if (g.cpu_diff || g.ram_diff) { std::printf("first difference at frame %d\n", g.first_bad); status = 3; }
		else std::printf("identical to MAME\n");
	}
	if (g.hashlog) std::fclose(g.hashlog);
	return status ? status : status_state;
}
