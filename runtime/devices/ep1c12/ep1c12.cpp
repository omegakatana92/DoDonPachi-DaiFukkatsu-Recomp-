// MAME 0.264's EP1C12 blitter (src/mame/cave/ep1c12.cpp, BSD-3-Clause,
// David Haywood, Luca Elia, MetalliC).  The member functions in
// ep1c12_mame.inc are MAME's, imported by tools/import_mame.py; this file
// adds the parts that were tied to the MAME framework: construction, start,
// reset, and gfx_exec_w / draw_screen, whose osd_work_queue is replaced by
// one worker thread with the same wait points.  The blit list is drawn from
// the shadow copy made when it is started, so the picture does not depend on
// when the worker runs it.
#include "ep1c12.h"

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#define LOG_DEBUG (1U << 1)
#define LOGMASKED(...) do { } while (0)

#include "../ep1c12_mame.inc"

// ---------------------------------------------------------------- worker

struct ep1c12_device::blit_worker {
	std::mutex m;
	std::condition_variable cv;
	std::thread th;
	bool job = false, quit = false;
	ep1c12_device *dev = nullptr;
	bool threaded = true;

	void run()
	{
		std::unique_lock<std::mutex> lk(m);
		for (;;) {
			cv.wait(lk, [&] { return job || quit; });
			if (quit)
				return;
			lk.unlock();
			dev->gfx_exec();
			lk.lock();
			job = false;
			cv.notify_all();
		}
	}
	void start(ep1c12_device *d)
	{
		dev = d;
		const char *e = std::getenv("CV1K_BLIT_THREAD");
		threaded = !(e && e[0] == '0');
		if (threaded)
			th = std::thread([this] { run(); });
	}
	void queue()
	{
		if (!threaded) {
			dev->gfx_exec();
			return;
		}
		std::lock_guard<std::mutex> lk(m);
		job = true;
		cv.notify_all();
	}
	void wait()
	{
		if (!threaded)
			return;
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&] { return !job; });
	}
	~blit_worker()
	{
		if (th.joinable()) {
			{
				std::lock_guard<std::mutex> lk(m);
				quit = true;
				cv.notify_all();
			}
			th.join();
		}
	}
};

ep1c12_device::ep1c12_device()
	: m_ram16(nullptr), m_gfx_size(0), m_bitmaps(nullptr), m_use_ram(nullptr)
	, m_main_ramsize(0), m_main_rammask(0), m_ram16_copy(nullptr)
{
	m_blitter_delay_timer = nullptr;
	m_blitter_busy = 0;
	m_gfx_addr = 0;
	m_gfx_scroll_x = 0;
	m_gfx_scroll_y = 0;
	m_gfx_clip_x = 0;
	m_gfx_clip_y = 0;
	m_gfx_addr_shadowcopy = 0;
	m_gfx_clip_x_shadowcopy = 0;
	m_gfx_clip_y_shadowcopy = 0;
	m_blit_delay_ns = 0;
	m_blit_idle_op_bytes = 0;
}

static void t_blitter_delay(void *ctx, int32_t p) { static_cast<ep1c12_device *>(ctx)->blitter_delay_callback(p); }

void ep1c12_device::device_start(kemu::scheduler &sched)
{
	m_gfx_size = 0x2000 * 0x1000;
	m_bitmaps = std::make_unique<bitmap_rgb32>(0x2000, 0x1000);
	m_clip = m_bitmaps->cliprect();
	m_clip.set(0, 0x2000 - 1, 0, 0x1000 - 1);

	m_ram16_copy = std::make_unique<u16[]>(m_main_ramsize / 2);
	m_dirty_tiles.assign(256 * 128, 0);

	m_blitter_delay_timer = sched.timer_alloc(t_blitter_delay, this, 200);
	m_blitter_delay_timer->adjust(attotime::never);

	m_firmware_pos = 0;
	m_firmware.clear();
	m_firmware.resize(290405, 0);
	m_firmware_port = 0;
	m_firmware_version = -1;

	delete m_worker;
	m_worker = new blit_worker();
	m_worker->start(this);
}

void ep1c12_device::device_reset()
{
	m_use_ram = m_ram16_copy.get();

	// cache table to avoid divides in blit code, also pre-clamped
	for (int y = 0; y < 0x40; y++)
	{
		for (int x = 0; x < 0x20; x++)
		{
			colrtable[x][y] = std::min((x * y) / 0x1f, 0x1f);
			colrtable_rev[x ^ 0x1f][y] = std::min((x * y) / 0x1f, 0x1f);
		}
	}

	// preclamped add table
	for (int y = 0; y < 0x20; y++)
	{
		for (int x = 0; x < 0x20; x++)
		{
			colrtable_add[x][y] = std::min((x + y), 0x1f);
		}
	}

	m_blitter_busy = 0;
}

void ep1c12_device::wait_for_blit()
{
	if (m_blitter_request)
	{
		m_worker->wait();
		m_blitter_request = false;
	}
}

void ep1c12_device::gfx_exec_w(address_space &space, offs_t offset, u32 data, u32 mem_mask)
{
	if (ACCESSING_BITS_0_7)
	{
		if (data & 1)
		{
			// make sure we've not already got a request running
			wait_for_blit();

			m_gfx_clip_x_shadowcopy = m_gfx_clip_x;
			m_gfx_clip_y_shadowcopy = m_gfx_clip_y;

			// Create a copy of the blit list so we can safely thread it.
			// Copying the Blitter operations will also estimate the delay needed for processing.
			m_blit_delay_ns = 0;
			gfx_create_shadow_copy(space);

			// Every EP1C_VRAM_H_LINE_PERIOD_NANOSEC, the Blitter will block other operations, due
			// to fetching a horizontal line from VRAM for output.
			m_blit_delay_ns += std::floor( m_blit_delay_ns / EP1C_VRAM_H_LINE_PERIOD_NANOSEC ) * EP1C_VRAM_H_LINE_DURATION_NANOSEC;

			// Check if Blitter takes longer than a frame to render.
			// In practice, there's a bit less time than this to allow for lack of slowdown but
			// for debugging purposes this is an ok approximation.
			if (m_blit_delay_ns > EP1C_FRAME_DURATION_NANOSEC)
				LOGDBG("Blitter delay! Blit duration %lld ns.\n", m_blit_delay_ns);

			m_blitter_busy = 1;
			m_blitter_delay_timer->adjust(attotime::from_nsec(m_blit_delay_ns));

			m_gfx_addr_shadowcopy = m_gfx_addr;
			mark_dirty_from_list();
			m_blitter_request = true;
			m_vram_generation++;
			m_worker->queue();
		}
	}
}

void ep1c12_device::mark_dirty_rect(int x0, int y0, int x1, int y1)
{
	x0 = std::max(x0, 0); y0 = std::max(y0, 0);
	x1 = std::min(x1, 0x2000 - 1); y1 = std::min(y1, 0x1000 - 1);
	if (x0 > x1 || y0 > y1 || m_dirty_tiles.empty())
		return;
	for (int ty = y0 >> 5; ty <= (y1 >> 5); ty++)
		std::memset(&m_dirty_tiles[size_t(ty) * 256 + size_t(x0 >> 5)], 1, size_t((x1 >> 5) - (x0 >> 5) + 1));
}

// Walks the blit list the worker is about to draw (gfx_exec's parse of the
// shadow copy) and marks the tiles each upload or draw may write: an upload's
// rectangle (a row running past x = 8191 continues on the next row), a
// draw's destination within the clipping rectangle in force.
void ep1c12_device::mark_dirty_from_list()
{
	offs_t addr = m_gfx_addr_shadowcopy & 0x1fffffff;
	auto W = [&](offs_t a) -> u16 { return m_ram16_copy[((a & m_main_rammask) >> 1) ^ NATIVE_ENDIAN_VALUE_LE_BE(3, 0)]; };
	rectangle clip;
	clip.set(m_gfx_clip_x_shadowcopy - EP1C_CLIP_MARGIN, m_gfx_clip_x_shadowcopy + 320 - 1 + EP1C_CLIP_MARGIN,
		m_gfx_clip_y_shadowcopy - EP1C_CLIP_MARGIN, m_gfx_clip_y_shadowcopy + 240 - 1 + EP1C_CLIP_MARGIN);
	for (int guard = 0; guard < (1 << 22); guard++)
	{
		const u16 data = W(addr);
		switch (data & 0xf000)
		{
			case 0x0000:
			case 0xf000:
				return;
			case 0xc000:
				if (W(addr + 2))
					clip.set(m_gfx_clip_x_shadowcopy - EP1C_CLIP_MARGIN, m_gfx_clip_x_shadowcopy + 320 - 1 + EP1C_CLIP_MARGIN,
						m_gfx_clip_y_shadowcopy - EP1C_CLIP_MARGIN, m_gfx_clip_y_shadowcopy + 240 - 1 + EP1C_CLIP_MARGIN);
				else
					clip.set(0, 0x2000 - 1, 0, 0x1000 - 1);
				addr += 4;
				break;
			case 0x2000:
			{
				const int x = W(addr + 8) & 0x1fff, y = W(addr + 10) & 0x0fff;
				const int dimx = (W(addr + 12) & 0x1fff) + 1, dimy = (W(addr + 14) & 0x0fff) + 1;
				if (x + dimx <= 0x2000)
					mark_dirty_rect(x, y, x + dimx - 1, y + dimy - 1);
				else
					mark_dirty_rect(0, y, 0x2000 - 1, y + dimy);
				addr += 16 + offs_t(dimx) * offs_t(dimy) * 2;
				break;
			}
			case 0x1000:
			{
				const u16 dx = W(addr + 8), dy = W(addr + 10);
				const int x = (dx & 0x7fff) - (dx & 0x8000), y = (dy & 0x7fff) - (dy & 0x8000);
				const int dimx = (W(addr + 12) & 0x1fff) + 1, dimy = (W(addr + 14) & 0x0fff) + 1;
				mark_dirty_rect(std::max(x, clip.min_x), std::max(y, clip.min_y),
					std::min(x + dimx - 1, clip.max_x), std::min(y + dimy - 1, clip.max_y));
				addr += 20;
				break;
			}
			default:
				return;
		}
	}
}

void ep1c12_device::draw_screen(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	wait_for_blit();

	// bitmap.fill(0, cliprect); copyscrollbitmap(bitmap, *m_bitmaps, 1, &scroll_x, 1, &scroll_y, cliprect);
	// with one scroll value each: a wrapped copy of the 0x2000 x 0x1000 VRAM
	const int scroll_x = -int(m_gfx_scroll_x);
	const int scroll_y = -int(m_gfx_scroll_y);
	const int sw = m_bitmaps->width(), sh = m_bitmaps->height();
	for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
	{
		const int sy = ((y - scroll_y) % sh + sh) % sh;
		u32 *dst = &bitmap.pix(y, 0);
		const u32 *src = &m_bitmaps->pix(sy, 0);
		for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
			dst[x] = src[((x - scroll_x) % sw + sw) % sw];
	}
}
