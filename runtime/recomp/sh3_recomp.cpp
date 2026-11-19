// See sh3_recomp.h.
#include "sh3_recomp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

#include "libtcc.h"

#include "sh3_probe.inc"
#include "sh3_templates.inc"

namespace {

// ---------------------------------------------------------------- decoding

struct decoded { int16_t id = -1; uint8_t na = 0; uint32_t a[3] = {0, 0, 0}; };
decoded g_dec[65536];
bool g_dec_ready = false;

void build_decoder()
{
	if (g_dec_ready) return;
	for (uint32_t op = 0; op < 65536; op++) {
		sh3_decode_probe p;
		p.execute_one(uint16_t(op));
		decoded &d = g_dec[op];
		d.id = int16_t(p.id);
		d.na = uint8_t(p.na);
		for (int i = 0; i < 3; i++) d.a[i] = p.a[i];
	}
	g_dec_ready = true;
}

// control flow of the handlers the translator follows
enum flow_kind {
	F_NORMAL,        // falls through (a call-out may still change pc: checked at run time)
	F_COND,          // BT / BF: pc = target or falls through
	F_DELAY_STATIC,  // BRA / BSR / BTS / BFS: m_delay = static target (conditional for BTS/BFS)
	F_DELAY_DYN,     // BRAF / BSRF / JMP / JSR / RTS: m_delay = register value
	F_END,           // RTE / TRAPA / SLEEP: back to the interpreter's loop afterwards
};
struct flow { flow_kind kind = F_NORMAL; bool cond = false; bool call = false; int32_t disp = 0; };

flow flow_of(uint16_t op)
{
	const decoded &d = g_dec[op];
	flow f;
	if (d.id < 0) { f.kind = F_END; return f; }
	const char *n = sh3_op_names[d.id];
	auto is = [&](const char *s) { return !std::strcmp(n, s); };
	auto sext = [](uint32_t v, int w) { return int32_t(v << (32 - w)) >> (32 - w); };
	if (is("BT") || is("BF")) { f.kind = F_COND; f.disp = sext(d.a[0], 8); }
	else if (is("BTS") || is("BFS")) { f.kind = F_DELAY_STATIC; f.cond = true; f.disp = sext(d.a[0], 8); }
	else if (is("BRA")) { f.kind = F_DELAY_STATIC; f.disp = sext(d.a[0], 12); }
	else if (is("BSR")) { f.kind = F_DELAY_STATIC; f.call = true; f.disp = sext(d.a[0], 12); }
	else if (is("BRAF") || is("JMP") || is("RTS")) f.kind = F_DELAY_DYN;
	else if (is("BSRF") || is("JSR")) { f.kind = F_DELAY_DYN; f.call = true; }
	else if (is("RTE") || is("TRAPA") || is("SLEEP")) f.kind = F_END;
	return f;
}

// may this instruction sit in a translated delay slot?
bool slot_ok(uint16_t op)
{
	const decoded &d = g_dec[op];
	if (d.id < 0) return false;
	const flow f = flow_of(op);
	return f.kind == F_NORMAL;
}

void put_hex(std::string &s, uint32_t v)
{
	char b[16];
	std::snprintf(b, sizeof b, "%X", v);
	s += b;
}

void replace_all(std::string &s, const std::string &from, const std::string &to)
{
	size_t p = 0;
	while ((p = s.find(from, p)) != std::string::npos) {
		s.replace(p, from.size(), to);
		p += to.size();
	}
}

// ---------------------------------------------------------------- call-backs from the generated code

extern "C" {
static uint32_t rc_rb(void *d, uint32_t a) { return static_cast<sh3_device *>(d)->read_byte(a); }
static uint32_t rc_rw(void *d, uint32_t a) { return static_cast<sh3_device *>(d)->read_word(a); }
static uint32_t rc_rl(void *d, uint32_t a) { return static_cast<sh3_device *>(d)->read_long(a); }
static void rc_wb(void *d, uint32_t a, uint32_t v) { static_cast<sh3_device *>(d)->write_byte(a, uint8_t(v)); }
static void rc_ww(void *d, uint32_t a, uint32_t v) { static_cast<sh3_device *>(d)->write_word(a, uint16_t(v)); }
static void rc_wl(void *d, uint32_t a, uint32_t v) { static_cast<sh3_device *>(d)->write_long(a, v); }
static void rc_exec(void *d, uint32_t op) { static_cast<sh3_device *>(d)->execute_one(uint16_t(op)); }
static void rc_irq(void *d) { static_cast<sh3_device *>(d)->sh4_check_pending_irq("mame_sh4_execute"); }
}
sh3_recompiler *g_rc = nullptr;
extern "C" void rc_codew(void *, uint32_t phys, uint32_t bytes) { if (g_rc) g_rc->code_written(phys, bytes); }

// The C the instruction blocks are compiled with.  The register state is
// MAME's internal_sh2_state (runtime/sh3/sh3_regs.h, same layout); the
// memory macros are the device's read_byte ... write_long (sh4.cpp) with the
// RAM and ROM read directly, as 64-bit words in host order.
const char *const PRELUDE = R"(
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64;
typedef u8 uint8_t; typedef u16 uint16_t; typedef u32 uint32_t; typedef u64 uint64_t;
typedef s8 int8_t; typedef s16 int16_t; typedef s32 int32_t; typedef s64 int64_t;
#define bool _Bool
#define true 1
#define false 0
struct S_ {
	u32 pc, pr, sr, mach, macl, r[16], ea;
	u32 pending_irq, pending_nmi; s32 irqline; u32 evec, irqsr, target;
	int internal_irq_level, icount; u8 sleep_mode; u32 arg0, arg1, gbr, vbr;
	u32 m_delay, m_ppc, m_spc, m_ssr, m_rbnk[2][8], m_sgr, m_fr[16], m_xf[16];
	u32 m_cpu_off, m_pending_irq, m_test_irq, m_fpscr, m_fpul, m_dbr;
	int m_frt_input, m_fpu_sz, m_fpu_pr;
};
struct E_ { u32 vaddr, module, index; };
struct C_ { const u8 *rom; u8 *ram; const u8 *codemap; const u32 *dir; const struct E_ *ents; volatile s32 exit_req; };
static struct C_ *C;
static u32 ME;
void setup(struct C_ *c, u32 me) { C = c; ME = me; }
u32 rc_rb(void *, u32); u32 rc_rw(void *, u32); u32 rc_rl(void *, u32);
void rc_wb(void *, u32, u32); void rc_ww(void *, u32, u32); void rc_wl(void *, u32, u32);
void rc_exec(void *, u32); void rc_irq(void *); void rc_codew(void *, u32, u32);
#define SH_T 0x00000001
#define SH_S 0x00000002
#define SH_I 0x000000f0
#define SH_Q 0x00000100
#define SH_M 0x00000200
#define SH_FLAGS (SH_M|SH_Q|SH_I|SH_S|SH_T)
#define T_SHIFT 0
#define S_SHIFT 1
#define I_SHIFT 4
#define Q_SHIFT 8
#define M_SHIFT 9
#define REG_N ((opcode >> 8) & 15)
#define REG_M ((opcode >> 4) & 15)
#define BIT(x, n) (((x) >> (n)) & 1)
#define SEXT(v, w) ((s32)((u32)(v) << (32 - (w))) >> (32 - (w)))
static u32 rotl_32(u32 v, int s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }
static u32 rotr_32(u32 v, int s) { s &= 31; return s ? (v >> s) | (v << (32 - s)) : v; }
#define AM 0x1fffffffu
)";

} // namespace

namespace {
struct insn { uint32_t pc; uint16_t op; uint16_t slot_op; flow f; };
}

// a module on its way: found and written (main thread), compiled (any
// thread), installed (main thread)
struct sh3_recompiler::job {
	std::vector<insn> list;
	std::vector<uint32_t> roots;
	std::string src;
	TCCState *tcc = nullptr;
	sh3_device::native_fn fn = nullptr;
	void (*setup)(rc_ctx *, uint32_t) = nullptr;
	std::string err;
	double ms = 0;
};

// ---------------------------------------------------------------- recompiler

sh3_recompiler::sh3_recompiler()
{
	build_decoder();
	g_rc = this;
}

sh3_recompiler::~sh3_recompiler()
{
	if (m_thread.joinable()) m_thread.join();
	if (m_job && m_job->tcc) tcc_delete(m_job->tcc);
	for (auto &m : m_modules)
		if (m.tcc) tcc_delete((TCCState *)m.tcc);
	if (g_rc == this) g_rc = nullptr;
}

void sh3_recompiler::attach(const sh3_recomp_memory &mem)
{
	reset();
	m_mem = mem;
	const uint32_t hws = (mem.rom_bytes + mem.ram_bytes) / 2;
	m_dir.assign(hws, 0);
	m_no_code.assign(hws, 0);
	m_codemap.assign(mem.ram_bytes / 2 + 4, 0);
	m_ents.assign(1, entry{0, 0, 0});
	m_ctx.rom = reinterpret_cast<const uint8_t *>(mem.rom);
	m_ctx.ram = reinterpret_cast<uint8_t *>(mem.ram);
	m_ctx.codemap = m_codemap.data();
	m_ctx.dir = m_dir.data();
	m_ctx.ents = m_ents.data();
	m_ctx.exit_req = 0;
}

void sh3_recompiler::reset()
{
	if (m_thread.joinable()) m_thread.join();
	if (m_job && m_job->tcc) tcc_delete(m_job->tcc);
	m_job.reset();
	m_job_done = false;
	for (auto &m : m_modules)
		if (m.tcc) tcc_delete((TCCState *)m.tcc);
	m_modules.clear();
	std::fill(m_dir.begin(), m_dir.end(), 0);
	std::fill(m_no_code.begin(), m_no_code.end(), 0);
	std::fill(m_codemap.begin(), m_codemap.end(), 0);
	m_ents.assign(1, entry{0, 0, 0});
	m_ctx.ents = m_ents.data();
	m_missed.clear();
	m_roots_done.clear();
	m_instructions = 0;
	m_modules_built = m_modules_dropped = 0;
	m_compile_ms = 0;
	m_source_bytes = 0;
}

bool sh3_recompiler::phys_ok(uint32_t phys) const
{
	return phys < m_mem.rom_bytes || (phys - m_mem.ram_base) < m_mem.ram_bytes;
}

uint16_t sh3_recompiler::fetch(uint32_t phys) const
{
	const uint64_t w = phys < m_mem.rom_bytes ? m_mem.rom[phys >> 3] : m_mem.ram[(phys - m_mem.ram_base) >> 3];
	return uint16_t(w >> (16 * (3 - ((phys >> 1) & 3))));
}

sh3_device::native_fn sh3_recompiler::lookup(uint32_t pc, int *entry_index)
{
	m_ctx.exit_req = 0;
	const uint32_t p = pc & SH34_AM;
	if ((pc & 1) || !phys_ok(p))
		return nullptr;
	const uint32_t di = dir_index(p);
	const uint32_t e = m_dir[di];
	if (e && m_ents[e].vaddr == pc) {
		*entry_index = int(m_ents[e].index);
		return m_modules[m_ents[e].module].fn;
	}
	if (!m_no_code[di]) {
		m_no_code[di] = 2;   // noted
		m_missed.push_back(pc);
	}
	return nullptr;
}

void sh3_recompiler::drop_module(uint32_t mi)
{
	module &m = m_modules[mi];
	if (!m.live) return;
	m.live = false;
	m_modules_dropped++;
	for (uint32_t pc : m.insns) {
		const uint32_t p = pc & SH34_AM;
		const uint32_t di = dir_index(p);
		const uint32_t e = m_dir[di];
		if (e && m_ents[e].module == mi) {
			m_dir[di] = 0;
			m_ents[e].vaddr = 1;   // never matches an instruction address
		}
		if (p >= m_mem.ram_base && m_codemap[(p - m_mem.ram_base) >> 1])
			m_codemap[(p - m_mem.ram_base) >> 1]--;
		m_no_code[di] = 0;
	}
}

void sh3_recompiler::code_written(uint32_t phys, uint32_t bytes)
{
	// the modules holding an instruction in the written range
	const uint32_t first = phys & ~1u, last = (phys + bytes - 1) & ~1u;
	bool any = false;
	for (uint32_t a = first; a <= last; a += 2) {
		if (a - m_mem.ram_base >= m_mem.ram_bytes || !m_codemap[(a - m_mem.ram_base) >> 1]) continue;
		for (uint32_t mi = 0; mi < m_modules.size(); mi++) {
			module &m = m_modules[mi];
			if (!m.live) continue;
			for (uint32_t pc : m.insns)
				if ((pc & SH34_AM) == a) { drop_module(mi); any = true; break; }
		}
	}
	if (any)
		m_ctx.exit_req = 1;   // the running code returns after this instruction
}

void sh3_recompiler::verify_code()
{
	for (uint32_t mi = 0; mi < m_modules.size(); mi++) {
		module &m = m_modules[mi];
		if (!m.live) continue;
		for (size_t k = 0; k < m.insns.size(); k++)
			if (fetch(m.insns[k] & SH34_AM) != m.ops[k]) { drop_module(mi); break; }
	}
	m_ctx.exit_req = 1;
}

bool sh3_recompiler::translate(const std::vector<uint32_t> &roots, std::string &err)
{
#if !(defined(__x86_64__) || defined(_M_X64))
	(void)roots;
	err = "the recompiler generates x86-64 code only";
	return false;
#endif
	std::vector<uint32_t> r = roots;
	while (!r.empty()) {
		// modules of at most ~24000 instructions
		std::vector<uint32_t> part(r.begin(), r.begin() + std::min<size_t>(r.size(), 2000));
		r.erase(r.begin(), r.begin() + part.size());
		if (!build_module(part, err))
			return false;
	}
	return true;
}

void sh3_recompiler::finish_background()
{
	if (!m_job) return;
	if (m_thread.joinable()) m_thread.join();
	if (m_job->err.empty()) install(*m_job);
	m_job.reset();
	m_job_done = false;
}

bool sh3_recompiler::translate_misses(std::string &err)
{
	// a module compiling in the background: install it once it is done
	if (m_job) {
		if (!m_job_done) return true;
		if (m_thread.joinable()) m_thread.join();
		const std::string e = m_job->err;
		if (e.empty()) install(*m_job);
		m_job.reset();
		m_job_done = false;
		if (!e.empty()) { err = e; return false; }
	}
	// free the code of dropped modules (never running here: called between
	// frames, and no compile is under way)
	for (auto &m : m_modules)
		if (!m.live && m.tcc) { tcc_delete((TCCState *)m.tcc); m.tcc = nullptr; m.fn = nullptr; m.insns.clear(); m.ops.clear(); }
	if (m_missed.empty())
		return true;
	std::vector<uint32_t> roots;
	roots.swap(m_missed);
	if (std::getenv("CV1K_RECOMP_DEBUG")) {
		std::fprintf(stderr, "translate %zu misses:", roots.size());
		for (size_t i = 0; i < roots.size() && i < 8; i++) std::fprintf(stderr, " %08X", roots[i]);
		std::fprintf(stderr, "\n");
	}
	for (uint32_t pc : roots) {
		const uint32_t di = dir_index(pc & SH34_AM);
		if (m_no_code[di] == 2) m_no_code[di] = 0;
	}
	if (roots.size() > 2000) {
		// more than one module's worth: the rest waits for the next round
		m_missed.assign(roots.begin() + 2000, roots.end());
		roots.resize(2000);
	}
	std::unique_ptr<job> j(new job());
	if (!generate(roots, *j))
		return true;
	if (!m_async) {
		compile(*j);
		if (!j->err.empty()) { err = j->err; return false; }
		install(*j);
		return true;
	}
	m_job = std::move(j);
	m_job_done = false;
	job *jp = m_job.get();
	m_thread = std::thread([this, jp] { compile(*jp); m_job_done = true; });
	return true;
}

static void tcc_error_sink(void *opaque, const char *msg)
{
	std::string *e = static_cast<std::string *>(opaque);
	if (e->size() < 2000) { *e += msg; *e += "\n"; }
}

bool sh3_recompiler::build_module(const std::vector<uint32_t> &roots, std::string &err)
{
	job j;
	if (!generate(roots, j))
		return true;   // nothing new
	compile(j);
	if (!j.err.empty()) { err = j.err; return false; }
	install(j);
	return true;
}

bool sh3_recompiler::generate(const std::vector<uint32_t> &roots, job &j)
{
	// ---- discover: the instructions reachable from the roots, not translated yet
	std::vector<insn> list;
	std::set<uint32_t> seen;
	std::vector<uint32_t> work;
	auto translated = [&](uint32_t pc) { return translated_at(pc); };
	for (uint32_t r : roots)
		if (!(r & 1) && phys_ok(r & SH34_AM)) work.push_back(r);
	const size_t LIMIT = 24000;
	while (!work.empty() && list.size() < LIMIT) {
		uint32_t pc = work.back();
		work.pop_back();
		for (;;) {
			const uint32_t p = pc & SH34_AM;
			if (!phys_ok(p) || !phys_ok(p + 2) || seen.count(pc) || translated(pc) || m_no_code[dir_index(p)] == 1)
				break;
			const uint16_t op = fetch(p);
			const flow f = flow_of(op);
			if (g_dec[op].id < 0) { m_no_code[dir_index(p)] = 1; break; }
			insn in { pc, op, 0, f };
			if (f.kind == F_DELAY_STATIC || f.kind == F_DELAY_DYN) {
				const uint16_t sop = fetch(p + 2);
				if (!slot_ok(sop)) { m_no_code[dir_index(p)] = 1; break; }   // left to the interpreter
				in.slot_op = sop;
			}
			seen.insert(pc);
			list.push_back(in);
			if (f.kind == F_COND) {
				work.push_back(pc + 4 + uint32_t(f.disp * 2));
				pc += 2;
				continue;
			}
			if (f.kind == F_DELAY_STATIC) {
				work.push_back(pc + 4 + uint32_t(f.disp * 2));
				if (f.cond || f.call) work.push_back(pc + 4);
				break;
			}
			if (f.kind == F_DELAY_DYN) {
				if (f.call) work.push_back(pc + 4);
				break;
			}
			if (f.kind == F_END)
				break;
			pc += 2;
		}
	}
	if (list.empty())
		return false;
	std::sort(list.begin(), list.end(), [](const insn &a, const insn &b) { return a.pc < b.pc; });
	std::set<uint32_t> here;
	for (const insn &in : list) here.insert(in.pc);

	// ---- source
	const uint32_t mi = m_modules_built + uint32_t(m_jobs_started);
	m_jobs_started++;
	std::string src = PRELUDE;
	src.reserve(list.size() * 600);
	src += "void m(void *D, struct S_ *S, int k)\n{\n";
	// the memory macros' temporaries and the per-instruction constants, once
	// for the whole function (see static_locals in tools/gen_sh3_templates.py)
	src += "u32 PCV, opcode, _ra, _ro, _rr, _wa, _wo, _wv;\n(void)opcode;\nstatic void *const T[] = {\n";
	for (const insn &in : list) { src += "&&L"; put_hex(src, in.pc); src += ",\n"; }
	src += "};\ngoto *T[k];\n";
	uint32_t uniq = 0;
	// What the interpreter's loop keeps up to date at every instruction, the
	// generated code writes only where it can be seen: S->pc before anything
	// that reads it (the handler itself, a call into the interpreter or a
	// device, the interrupt check) and on the way out; S->m_ppc (the address
	// of the last instruction run) only on the way out.
	//
	// One instruction's block.  `slot`: it runs as the delay slot of the
	// branch before it (pc from m_delay, as MAME's loop does).  Returns
	// whether S->pc holds the next address after the block; otherwise it is
	// pc + 2 and has not been stored.
	auto exit_code = [&](uint32_t pc, bool pc_stored) {
		std::string e;
		if (!pc_stored) { e += "S->pc = 0x"; put_hex(e, pc + 2); e += "; "; }
		e += "S->m_ppc = 0x"; put_hex(e, pc & SH34_AM); e += ";";
		return e;
	};
	auto block = [&](uint32_t pc, uint16_t op, bool slot) -> bool {
		const decoded &d = g_dec[op];
		const sh3_template &t = sh3_templates[d.id];
		bool pc_stored = slot || t.callout;
		bool writes = t.callout;
		src += "{\n";
		if (slot)
			src += "if (S->m_delay) { S->pc = S->m_delay; S->m_delay = 0; } else S->pc += 2;\nPCV = S->pc;\n";
		else {
			src += "PCV = 0x"; put_hex(src, pc + 2); src += ";\n";
		}
		if (t.callout) {
			if (!slot) src += "S->pc = PCV;\n";
			src += "rc_exec(D, 0x"; put_hex(src, op); src += ");\n";
		} else {
			std::string b = t.body;
			for (int i = 0; i < t.nparams; i++) {
				char v[16];
				std::snprintf(v, sizeof v, "0x%X", d.a[i]);
				replace_all(b, std::string("@") + t.params[i] + "@", v);
			}
			const std::string endl = "E" + std::to_string(uniq++);
			replace_all(b, "@END@", endl);
			if (b.find("WB(") != std::string::npos || b.find("WW(") != std::string::npos || b.find("WL(") != std::string::npos)
				writes = true;
			if (b.find("S->pc") != std::string::npos && !slot) {
				src += "S->pc = PCV;\n";
				pc_stored = true;
			}
			if (b.find("opcode") != std::string::npos || b.find("REG_") != std::string::npos) {
				src += "opcode = 0x"; put_hex(src, op); src += ";\n";
			}
			src += b;
			src += "\n" + endl + ":;\n";
		}
		src += "}\n";
		// MAME's loop: the interrupt check (not between a delayed branch and
		// its slot), the cycle, the loop condition
		const std::string ex = exit_code(pc, pc_stored);
		src += "if (S->m_test_irq) { " + std::string(pc_stored ? "" : "S->pc = 0x");
		if (!pc_stored) { put_hex(src, pc + 2); src += "; "; }
		src += "if (!S->m_delay) rc_irq(D); S->m_ppc = 0x"; put_hex(src, pc & SH34_AM);
		src += "; if (--S->icount <= 0) return; goto DYN; }\n";
		src += "if (--S->icount <= 0) { " + ex + " return; }\n";
		if (writes)
			src += "if (C->exit_req) { " + ex + " return; }\n";
		return pc_stored;
	};
	// to `target`, known at translation time, with S->pc not yet stored
	auto go_static = [&](uint32_t from, uint32_t target, bool pc_stored, uint32_t next_in_source) {
		if (here.count(target)) {
			if (target != next_in_source) { src += "goto L"; put_hex(src, target); src += ";\n"; }
		} else {
			if (!pc_stored) { src += "S->pc = 0x"; put_hex(src, target); src += "; "; }
			src += "S->m_ppc = 0x"; put_hex(src, from & SH34_AM); src += "; return;\n";
		}
	};
	// to S->pc (stored), one of `targets` if it is in this module
	auto go_dynamic = [&](uint32_t from, std::initializer_list<uint32_t> targets) {
		for (uint32_t t : targets)
			if (here.count(t)) { src += "if (S->pc == 0x"; put_hex(src, t); src += ") goto L"; put_hex(src, t); src += ";\n"; }
		src += "S->m_ppc = 0x"; put_hex(src, from & SH34_AM); src += "; goto DYN;\n";
	};
	for (size_t li = 0; li < list.size(); li++) {
		const insn &in = list[li];
		const uint32_t next_src = li + 1 < list.size() ? list[li + 1].pc : 0xffffffffu;
		src += "L"; put_hex(src, in.pc); src += ":\n";
		const bool stored = block(in.pc, in.op, false);
		const bool callout = sh3_templates[g_dec[in.op].id].callout;
		switch (in.f.kind) {
		case F_NORMAL:
			if (callout) {
				src += "if (S->m_delay) { S->m_ppc = 0x"; put_hex(src, in.pc & SH34_AM); src += "; return; }\n";
				go_dynamic(in.pc, { in.pc + 2 });
			} else if (stored)
				go_dynamic(in.pc, { in.pc + 2 });
			else
				go_static(in.pc, in.pc + 2, false, next_src);
			break;
		case F_COND:
			go_dynamic(in.pc, { in.pc + 4 + uint32_t(in.f.disp * 2), in.pc + 2 });
			break;
		case F_DELAY_STATIC:
		case F_DELAY_DYN: {
			// the slot, inline
			block(in.pc + 2, in.slot_op, true);
			if (sh3_templates[g_dec[in.slot_op].id].callout) {
				src += "if (S->m_delay) { S->m_ppc = 0x"; put_hex(src, (in.pc + 2) & SH34_AM); src += "; return; }\n";
			}
			if (in.f.kind == F_DELAY_STATIC) {
				const uint32_t t = in.pc + 4 + uint32_t(in.f.disp * 2);
				if (in.f.cond) go_dynamic(in.pc + 2, { t, in.pc + 4 });
				else if (sh3_templates[g_dec[in.slot_op].id].callout) go_dynamic(in.pc + 2, { t });
				else go_static(in.pc + 2, t, true, next_src);
			} else
				go_dynamic(in.pc + 2, {});
			break;
		}
		case F_END:
			src += "S->m_ppc = 0x"; put_hex(src, in.pc & SH34_AM); src += "; return;\n";
			break;
		}
	}
	// any other successor: an instruction of this module, or back to the loop
	src += "DYN:\nif (S->m_delay) return;\n{\nu32 a = S->pc, p = a & AM, i, e;\n";
	src += "if (p < 0x"; put_hex(src, m_mem.rom_bytes); src += ") i = p >> 1;\n";
	src += "else if (p - 0x"; put_hex(src, m_mem.ram_base); src += "u < 0x"; put_hex(src, m_mem.ram_bytes);
	src += "u) i = (0x"; put_hex(src, m_mem.rom_bytes); src += " + (p - 0x"; put_hex(src, m_mem.ram_base); src += ")) >> 1;\n";
	src += "else return;\ne = C->dir[i];\nif (e && C->ents[e].vaddr == a && C->ents[e].module == ME) goto *T[C->ents[e].index];\n}\nreturn;\n}\n";

	// memory macros, with this board's addresses as constants: RAM and ROM
	// read and written directly (64-bit words in host order), everything
	// else through the device's read_byte ... write_long, with S->pc stored
	// first (a device may look at it)
	std::string mem;
	auto H = [&](uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "0x%llXull", (unsigned long long)v); return std::string(b); };
	auto H32 = [&](uint32_t v) { char b[32]; std::snprintf(b, sizeof b, "0x%Xu", v); return std::string(b); };
	const std::string RAMB = H32(m_mem.ram_base), RAMS = H32(m_mem.ram_bytes), ROMS = H32(m_mem.rom_bytes);
	const std::string BUSO = H32(m_mem.bus_only - m_mem.ram_base);
	const std::string RAMP = "((u8 *)" + H(uint64_t(uintptr_t(m_mem.ram))) + ")";
	const std::string ROMP = "((const u8 *)" + H(uint64_t(uintptr_t(m_mem.rom))) + ")";
	const std::string CMAP = "((const u8 *)" + H(uint64_t(uintptr_t(m_codemap.data()))) + ")";
	auto rd = [&](const char *name, const char *ty, const char *mask, const char *x, const char *slow) {
		mem += std::string("#define ") + name + "(a) ({ _ra = (a); _ro = (_ra & AM) - " + RAMB + "; "
			"if (_ra < 0xe0000000u && _ro < " + RAMS + " && (_ro & ~7u) != " + BUSO + ") _rr = *(const " + ty + " *)(" + RAMP + " + ((_ro & " + mask + ") ^ " + x + ")); "
			"else if (_ra < 0xe0000000u && (_ra & AM) < " + ROMS + ") _rr = *(const " + ty + " *)(" + ROMP + " + ((_ra & AM & " + mask + ") ^ " + x + ")); "
			"else { S->pc = PCV; _rr = " + slow + "(D, _ra); } _rr; })\n";
	};
	rd("RB", "u8", "~0u", "7", "rc_rb");
	rd("RW", "u16", "~1u", "6", "rc_rw");
	rd("RL", "u32", "~3u", "4", "rc_rl");
	auto wr = [&](const char *name, const char *ty, const char *mask, const char *x, const char *slow, int bytes) {
		std::string check = bytes == 4 ? (CMAP + "[_wo >> 1] | " + CMAP + "[(_wo >> 1) + 1]") : (CMAP + "[_wo >> 1]");
		// the value first: it may itself read memory
		mem += std::string("#define ") + name + "(a, v) do { _wv = (v); _wa = (a); _wo = ((_wa & AM) - " + RAMB + ") & " + mask + "; "
			"if (_wa < 0xe0000000u && _wo < " + RAMS + ") { *(" + ty + " *)(" + RAMP + " + (_wo ^ " + x + ")) = (" + ty + ")_wv; "
			"if (" + check + ") rc_codew(D, " + RAMB + " + _wo, " + std::to_string(bytes) + "); } "
			"else { S->pc = PCV; " + slow + "(D, _wa, _wv); } } while (0)\n";
	};
	wr("WB", "u8", "~0u", "7", "rc_wb", 1);
	wr("WW", "u16", "~1u", "6", "rc_ww", 2);
	wr("WL", "u32", "~3u", "4", "rc_wl", 4);
	src.insert(std::string(PRELUDE).size(), mem);

	m_source_bytes += src.size();
	if (const char *dump = std::getenv("CV1K_RECOMP_DUMP")) {     // development: keep the generated C
		std::string fn = std::string(dump) + "/module" + std::to_string(mi) + ".c";
		(void)fn;
		if (FILE *f = std::fopen(fn.c_str(), "w")) { std::fwrite(src.data(), 1, src.size(), f); std::fclose(f); }
	}

	j.list.swap(list);
	j.roots = roots;
	j.src.swap(src);
	return true;
}

void sh3_recompiler::compile(job &j)
{
	auto t0 = std::chrono::steady_clock::now();
	TCCState *s = tcc_new();
	if (!s) { j.err = "TinyCC could not start"; return; }
	std::string tccerr;
	tcc_set_error_func(s, &tccerr, tcc_error_sink);
	tcc_set_options(s, "-nostdlib -nostdinc");
	tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
	tcc_add_symbol(s, "rc_rb", (const void *)rc_rb);
	tcc_add_symbol(s, "rc_rw", (const void *)rc_rw);
	tcc_add_symbol(s, "rc_rl", (const void *)rc_rl);
	tcc_add_symbol(s, "rc_wb", (const void *)rc_wb);
	tcc_add_symbol(s, "rc_ww", (const void *)rc_ww);
	tcc_add_symbol(s, "rc_wl", (const void *)rc_wl);
	tcc_add_symbol(s, "rc_exec", (const void *)rc_exec);
	tcc_add_symbol(s, "rc_irq", (const void *)rc_irq);
	tcc_add_symbol(s, "rc_codew", (const void *)rc_codew);
	if (tcc_compile_string(s, j.src.c_str()) < 0 || tcc_relocate(s) < 0) {
		j.err = "TinyCC: " + tccerr;
		tcc_delete(s);
		return;
	}
	j.fn = (sh3_device::native_fn)tcc_get_symbol(s, "m");
	j.setup = (void (*)(rc_ctx *, uint32_t))tcc_get_symbol(s, "setup");
	if (!j.fn || !j.setup) { j.err = "TinyCC: no entry symbol"; tcc_delete(s); return; }
	j.tcc = s;
	j.src.clear();
	j.src.shrink_to_fit();
	j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool sh3_recompiler::install(job &j)
{
	// the code may have changed since it was read: then the module is dropped
	// (its addresses run on the interpreter and are noted again)
	for (const insn &in : j.list) {
		const uint32_t p = in.pc & SH34_AM;
		bool slot = in.f.kind == F_DELAY_STATIC || in.f.kind == F_DELAY_DYN;
		if (fetch(p) != in.op || (slot && fetch(p + 2) != in.slot_op) || translated_at(in.pc)) {
			tcc_delete(j.tcc);
			j.tcc = nullptr;
			m_jobs_discarded++;
			return false;
		}
	}
	const uint32_t mi = uint32_t(m_modules.size());
	j.setup(&m_ctx, mi);
	m_compile_ms += j.ms;
	const std::vector<insn> &list = j.list;
	const std::vector<uint32_t> &roots = j.roots;
	TCCState *s = j.tcc;
	sh3_device::native_fn fn = j.fn;
	// ---- install
	module m;
	m.tcc = s;
	m.fn = fn;
	m.live = true;
	for (size_t k = 0; k < list.size(); k++) {
		const insn &in = list[k];
		const uint32_t p = in.pc & SH34_AM;
		m_ents.push_back(entry{in.pc, mi, uint32_t(k)});
		m_dir[dir_index(p)] = uint32_t(m_ents.size() - 1);
		m.insns.push_back(in.pc);
		m.ops.push_back(in.op);
		if (p - m_mem.ram_base < m_mem.ram_bytes) m_codemap[(p - m_mem.ram_base) >> 1]++;
		if (in.f.kind == F_DELAY_STATIC || in.f.kind == F_DELAY_DYN) {
			m.insns.push_back(in.pc + 2);   // the inline slot
			m.ops.push_back(in.slot_op);
			if (p + 2 - m_mem.ram_base < m_mem.ram_bytes) m_codemap[(p + 2 - m_mem.ram_base) >> 1]++;
		}
	}
	m_ctx.ents = m_ents.data();
	m_ctx.dir = m_dir.data();
	m_modules.push_back(std::move(m));
	m_modules_built++;
	m_instructions += uint32_t(list.size());
	for (uint32_t r : roots) m_roots_done.push_back(r);
	return true;
}
