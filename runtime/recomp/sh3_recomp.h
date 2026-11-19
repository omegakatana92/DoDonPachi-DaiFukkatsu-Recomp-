// Recompiler for the SH-3 program.
//
// The game's machine code is never shipped.  The boot program runs from the
// program ROM; the game itself is copied by it from the NAND flash into main
// RAM.  Code is translated where it runs: an address reached for the first
// time runs on the interpreter and is noted; at the end of the frame the
// recompiler walks the code from the noted addresses (following branches and
// calls), writes one C block per SH-3 instruction and compiles the result in
// memory with the embedded TinyCC.  Each block is MAME 0.264's handler for
// that instruction (tools/gen_sh3_templates.py) with the opcode's fields
// fixed, framed by MAME's execute loop: the delay-slot hand-over, the
// interrupt check and the cycle count, so the native code does exactly what
// the interpreter would.  Control passes between instructions by direct
// jumps.  A write to an address that holds translated code drops the module
// that contains it; the code is translated again when it is next reached.
#ifndef DFK_SH3_RECOMP_H
#define DFK_SH3_RECOMP_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "../sh3/sh3.h"

// what the generated code needs from the board: the memory it may read and
// write directly, and the one 64-bit word that must go through the bus
struct sh3_recomp_memory {
	const uint64_t *rom = nullptr;     // program ROM at 0, 64-bit words as the board holds them
	uint32_t rom_bytes = 0;
	uint64_t *ram = nullptr;           // main RAM at ram_base
	uint32_t ram_base = 0, ram_bytes = 0;
	uint32_t bus_only = 0;             // address of the 8 bytes read through the bus (the idle-loop handler)
};

class sh3_recompiler {
public:
	sh3_recompiler();
	~sh3_recompiler();
	void attach(const sh3_recomp_memory &mem);
	void reset();                           // drop all translated code
	// translate everything reachable from `roots` (virtual addresses)
	bool translate(const std::vector<uint32_t> &roots, std::string &err);

	// the translated code entered at instruction address `pc`, or nullptr
	// (an address that could hold code is then noted for translation)
	sh3_device::native_fn lookup(uint32_t pc, int *entry);
	bool has_misses() const { return !m_missed.empty() || m_job; }
	// called between frames: installs the module compiled in the background,
	// if it is ready, and starts the next one from the addresses noted since
	bool translate_misses(std::string &err);
	bool m_async = true;                    // compile on a worker thread
	void finish_background();              // wait for and install the running job
	// the board's writes to main RAM: drop translated code at the address
	void code_written(uint32_t phys, uint32_t bytes);
	// after RAM was replaced (a save state): drop the modules whose
	// instructions are not there any more
	void verify_code();
	const uint8_t *code_map() const { return m_codemap.data(); }   // 1 per RAM halfword holding translated code

	// statistics
	uint32_t instructions() const { return m_instructions; }
	uint32_t modules_built() const { return m_modules_built; }
	uint32_t modules_dropped() const { return m_modules_dropped; }
	uint32_t modules_discarded() const { return m_jobs_discarded; }
	double compile_ms() const { return m_compile_ms; }
	size_t source_bytes() const { return m_source_bytes; }
	const std::vector<uint32_t> &all_roots() const { return m_roots_done; }

	// shared with the generated code (see the prelude in sh3_recomp.cpp)
	struct entry { uint32_t vaddr; uint32_t module; uint32_t index; };
	struct rc_ctx {
		const uint8_t *rom;                 // byte view of the ROM words
		uint8_t *ram;                       // byte view of the RAM words
		const uint8_t *codemap;
		const uint32_t *dir;                // per ROM / RAM halfword: index into ents (0 = none)
		const entry *ents;
		volatile int32_t exit_req;          // set when translated code was overwritten
	};

	struct job;
private:
	bool generate(const std::vector<uint32_t> &roots, job &j);
	static void compile(job &j);
	bool install(job &j);
	bool translated_at(uint32_t pc) const
	{
		const uint32_t p = pc & SH34_AM;
		if (!phys_ok(p)) return false;
		const uint32_t e = m_dir[dir_index(p)];
		return e && m_ents[e].vaddr == pc;
	}
	std::unique_ptr<job> m_job;
	std::thread m_thread;
	std::atomic<bool> m_job_done { false };
	uint32_t m_jobs_started = 0, m_jobs_discarded = 0;
	struct module { void *tcc = nullptr; sh3_device::native_fn fn = nullptr; std::vector<uint32_t> insns; std::vector<uint16_t> ops; bool live = false; };
	bool build_module(const std::vector<uint32_t> &roots, std::string &err);
	bool phys_ok(uint32_t phys) const;
	uint32_t dir_index(uint32_t phys) const
	{
		return phys < m_mem.rom_bytes ? phys >> 1 : (m_mem.rom_bytes + (phys - m_mem.ram_base)) >> 1;
	}
	uint16_t fetch(uint32_t phys) const;
	void drop_module(uint32_t m);

	sh3_recomp_memory m_mem;
	rc_ctx m_ctx {};
	std::vector<uint32_t> m_dir;           // see rc_ctx::dir
	std::vector<entry> m_ents;             // [0] unused
	std::vector<uint8_t> m_codemap;
	std::vector<uint8_t> m_no_code;        // per halfword: not translatable here (as a non-slot start)
	std::vector<module> m_modules;
	std::vector<uint32_t> m_missed;
	std::vector<uint32_t> m_roots_done;
	uint32_t m_instructions = 0, m_modules_built = 0, m_modules_dropped = 0;
	double m_compile_ms = 0;
	size_t m_source_bytes = 0;
};

#endif
