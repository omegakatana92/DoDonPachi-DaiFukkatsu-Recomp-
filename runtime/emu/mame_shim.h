// Stand-ins for the MAME 0.264 framework names used by the MAME code this
// port reuses (the SH-3 core and the CV1000 devices, imported by
// tools/import_mame.py), so that their function bodies compile unchanged.
#ifndef DFK_MAME_SHIM_H
#define DFK_MAME_SHIM_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES   // M_PI on Windows
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <algorithm>
#include <memory>
#include <vector>
#include "kemu.h"

#ifndef LSB_FIRST
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LSB_FIRST 1
#elif defined(_WIN32)
#define LSB_FIRST 1
#endif
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint32_t offs_t;

using kemu::attoseconds_t;
using kemu::CLEAR_LINE;
using kemu::ASSERT_LINE;
using kemu::HOLD_LINE;
enum { INPUT_LINE_NMI = kemu::INPUT_LINE_RESET - 1 };

template <typename... T> inline void logerror(const char *, T &&...) {}
template <typename... T> [[noreturn]] inline void fatalerror(const char *fmt, T &&... args)
{
	std::fprintf(stderr, "core: fatal error: ");
	if constexpr (sizeof...(T) == 0)
		std::fputs(fmt, stderr);
	else
		std::fprintf(stderr, fmt, std::forward<T>(args)...);
	std::abort();
}
#define LOG(...) do { } while (0)

constexpr u32 rotl_32(u32 val, int shift) { shift &= 31; return shift ? (val << shift) | (val >> (32 - shift)) : val; }
constexpr u32 rotr_32(u32 val, int shift) { shift &= 31; return shift ? (val >> shift) | (val << (32 - shift)) : val; }
template <typename T, typename U> constexpr T BIT(T x, U n) { return (x >> n) & T(1); }

namespace util {
template <typename T, typename U> constexpr typename std::make_signed<T>::type sext(T value, U width)
{
	typedef typename std::make_signed<T>::type S;
	return S(value << (8 * sizeof(T) - width)) >> (8 * sizeof(T) - width);
}
}

#define COMBINE_DATA(varptr) (*(varptr) = (*(varptr) & ~mem_mask) | (data & mem_mask))
#define ACCESSING_BITS_0_7 ((mem_mask & 0x000000ffU) != 0)
template <typename... T> inline void popmessage(const char *, T &&...) {}
#define NATIVE_ENDIAN_VALUE_LE_BE(leval, beval) (leval)

// attotime with MAME's spelling (attotime::never, attotime::zero, ...)
struct attotime : kemu::attotime {
	constexpr attotime() = default;
	constexpr attotime(kemu::seconds_t s, attoseconds_t a) : kemu::attotime(s, a) {}
	attotime(const kemu::attotime &a) : kemu::attotime(a) {}
	static const attotime never;
	static const attotime zero;
	kemu::seconds_t seconds() const { return sec; }
	attoseconds_t attoseconds() const { return atto; }
	// attotime::from_hz(u32/int): integer division, as in MAME
	static attotime from_hz(uint32_t f) { return attotime(kemu::attotime::from_hz_u(f)); }
	static attotime from_hz(int f) { return from_hz(uint32_t(f)); }
	static attotime from_hz(double f) { return attotime(kemu::attotime::from_hz(f)); }
	static attotime from_nsec(int64_t nsec)
	{
		return attotime(kemu::seconds_t(nsec / 1000000000), attoseconds_t(nsec % 1000000000) * 1000000000LL);
	}
	static attotime from_usec(int64_t usec)
	{
		return attotime(kemu::seconds_t(usec / 1000000), attoseconds_t(usec % 1000000) * 1000000000000LL);
	}
};

typedef kemu::timer emu_timer;
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)

// emu/rendutil-free stand-ins for the bitmap and rectangle the blitter uses
struct rectangle {
	int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
	rectangle() = default;
	rectangle(int x0, int x1, int y0, int y1) : min_x(x0), max_x(x1), min_y(y0), max_y(y1) {}
	void set(int x0, int x1, int y0, int y1) { min_x = x0; max_x = x1; min_y = y0; max_y = y1; }
	int width() const { return max_x + 1 - min_x; }
	int height() const { return max_y + 1 - min_y; }
};

struct bitmap_rgb32 {
	std::unique_ptr<uint32_t[]> m_base;
	int m_width = 0, m_height = 0, m_rowpixels = 0;
	bitmap_rgb32() = default;
	bitmap_rgb32(int w, int h) { allocate(w, h); }
	void allocate(int w, int h)
	{
		m_width = w; m_height = h; m_rowpixels = w;
		m_base.reset(new uint32_t[size_t(w) * size_t(h)]());
	}
	uint32_t &pix(int y, int x = 0) { return m_base[size_t(y) * m_rowpixels + x]; }
	const uint32_t &pix(int y, int x = 0) const { return m_base[size_t(y) * m_rowpixels + x]; }
	int width() const { return m_width; }
	int height() const { return m_height; }
	int rowpixels() const { return m_rowpixels; }
	rectangle cliprect() const { return rectangle(0, m_width - 1, 0, m_height - 1); }
};

// the address_space argument the blitter's handlers pass along unused
struct address_space {};

#endif
