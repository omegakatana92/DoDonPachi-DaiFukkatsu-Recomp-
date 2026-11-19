// The SH-3 core's own stand-ins (see emu/mame_shim.h for the common ones).
#ifndef DFK_SH3_SHIM_H
#define DFK_SH3_SHIM_H

#include "../emu/mame_shim.h"

// machine(): only the debugger flags are looked at
enum { DEBUG_FLAG_ENABLED = 0x00000001 };
struct sh3_machine_stub {
	uint32_t debug_flags = 0;
	void debug_break() {}
};

// The CPU's view of a 64-bit big-endian address space: the board supplies
// one handler per access width (address_space::read_byte ... write_qword).
struct sh3_space {
	void *ctx = nullptr;
	u8 (*r8)(void *, offs_t) = nullptr;
	u16 (*r16)(void *, offs_t) = nullptr;
	u32 (*r32)(void *, offs_t) = nullptr;
	u64 (*r64)(void *, offs_t) = nullptr;
	void (*w8)(void *, offs_t, u8) = nullptr;
	void (*w16)(void *, offs_t, u16) = nullptr;
	void (*w32)(void *, offs_t, u32) = nullptr;
	void (*w64)(void *, offs_t, u64) = nullptr;
	u8 read_byte(offs_t a) { return r8(ctx, a); }
	u16 read_word(offs_t a) { return r16(ctx, a); }
	u32 read_dword(offs_t a) { return r32(ctx, a); }
	u64 read_qword(offs_t a) { return r64(ctx, a); }
	void write_byte(offs_t a, u8 d) { w8(ctx, a, d); }
	void write_word(offs_t a, u16 d) { w16(ctx, a, d); }
	void write_dword(offs_t a, u32 d) { w32(ctx, a, d); }
	void write_qword(offs_t a, u64 d) { w64(ctx, a, d); }
};

#endif
