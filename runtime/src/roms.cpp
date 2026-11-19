// ROM sets of DoDonPachi Dai-Fukkatsu (ddpdfk, ddpdfk10, dfkbl): validation
// and region assembly, from MAME 0.264's ROM_START(ddpdfk), ROM_START(ddpdfk10)
// and ROM_START(dfkbl) in src/mame/cave/cv1k.cpp.  The player supplies a zip
// archive or a folder; files are matched by name, or by size and CRC32 when
// the name differs (as MAME does).  A clone set may take files it shares
// with its parent from the parent's archive beside it.  Nothing is written
// anywhere.
#include "cv1k/machine.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <strings.h>
#include "miniz.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace cv1k {

typedef rom_file rom_entry;

// ------------------------------------------------------------------ sources

static FILE *u8fopen(const char *path)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
	if (n <= 0) return nullptr;
	std::unique_ptr<wchar_t[]> w(new wchar_t[size_t(n)]);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, w.get(), n);
	return _wfopen(w.get(), L"rb");
#else
	return std::fopen(path, "rb");
#endif
}

static bool read_file(const char *path, std::vector<uint8_t> &out, size_t limit = 512u << 20)
{
	FILE *f = u8fopen(path);
	if (!f) return false;
	out.clear();
	uint8_t buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
		out.insert(out.end(), buf, buf + n);
		if (out.size() > limit) { std::fclose(f); return false; }
	}
	std::fclose(f);
	return true;
}

static bool is_directory(const char *path)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
	if (n <= 0) return false;
	std::unique_ptr<wchar_t[]> w(new wchar_t[size_t(n)]);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, w.get(), n);
	DWORD a = GetFileAttributesW(w.get());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static std::vector<std::string> list_dir(const char *path)
{
	std::vector<std::string> names;
#ifdef _WIN32
	std::string pat = std::string(path) + "\\*";
	int n = MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, nullptr, 0);
	std::unique_ptr<wchar_t[]> w(new wchar_t[size_t(n)]);
	MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, w.get(), n);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(w.get(), &fd);
	if (h == INVALID_HANDLE_VALUE) return names;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		char u[1024];
		if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, u, sizeof u, nullptr, nullptr) > 0)
			names.push_back(u);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(path);
	if (!d) return names;
	while (struct dirent *e = readdir(d)) {
		std::string full = std::string(path) + "/" + e->d_name;
		struct stat st;
		if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode))
			names.push_back(e->d_name);
	}
	closedir(d);
#endif
	return names;
}

namespace {
// a zip archive (read into memory, so UTF-8 paths work on Windows) or a folder
struct rom_source {
	struct item { std::string name; uint32_t size = 0, crc = 0; int zip_index = -1; bool crc_known = false; };
	bool is_zip = false, ok = false;
	std::string dir;
	std::vector<uint8_t> zipdata;
	mz_zip_archive z;
	std::vector<item> items;

	explicit rom_source(const char *path)
	{
		std::memset(&z, 0, sizeof z);
		if (!path || !*path) return;
		if (is_directory(path)) {
			dir = path;
			for (const std::string &n : list_dir(path)) {
				item it;
				it.name = n;
				std::vector<uint8_t> d;
				std::string full = dir + "/" + n;
				FILE *f = u8fopen(full.c_str());
				if (!f) continue;
				std::fseek(f, 0, SEEK_END);
				long sz = std::ftell(f);
				std::fclose(f);
				if (sz < 0 || sz > (256 << 20)) continue;   // not a ROM of these sets
				it.size = uint32_t(sz);
				items.push_back(it);
			}
			ok = true;
			return;
		}
		if (!read_file(path, zipdata)) return;
		if (!mz_zip_reader_init_mem(&z, zipdata.data(), zipdata.size(), 0)) return;
		is_zip = true;
		int n = int(mz_zip_reader_get_num_files(&z));
		for (int i = 0; i < n; i++) {
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&z, mz_uint(i), &st) || st.m_is_directory) continue;
			const char *base = std::strrchr(st.m_filename, '/');
			if (!base) base = std::strrchr(st.m_filename, '\\');
			base = base ? base + 1 : st.m_filename;
			item it;
			it.name = base;
			it.size = uint32_t(st.m_uncomp_size);
			it.crc = st.m_crc32;
			it.crc_known = true;
			it.zip_index = i;
			items.push_back(it);
		}
		ok = true;
	}
	~rom_source() { if (is_zip) mz_zip_reader_end(&z); }

	bool read(const item &it, std::vector<uint8_t> &out)
	{
		if (is_zip) {
			size_t size = 0;
			void *p = mz_zip_reader_extract_to_heap(&z, mz_uint(it.zip_index), &size, 0);
			if (!p) return false;
			out.assign((uint8_t *)p, (uint8_t *)p + size);
			mz_free(p);
			return true;
		}
		std::string full = dir + "/" + it.name;
		return read_file(full.c_str(), out);
	}
	// folders: the CRC is computed on first use
	uint32_t crc_of(item &it)
	{
		if (!it.crc_known) {
			std::vector<uint8_t> d;
			if (read(it, d))
				it.crc = uint32_t(mz_crc32(MZ_CRC32_INIT, d.data(), d.size()));
			it.crc_known = true;
		}
		return it.crc;
	}
	// MAME's matching: by name (any case); otherwise any file with the same size and CRC
	item *find(const rom_entry &e)
	{
		for (auto &it : items)
			if (!strcasecmp(it.name.c_str(), e.name))
				return &it;
		for (auto &it : items)
			if (it.size == e.size && crc_of(it) == e.crc)
				return &it;
		return nullptr;
	}
};

} // namespace

// ------------------------------------------------------------------ the sets

// ROM_START(ddpdfk) / (ddpdfk10) / (dfkbl), MAME 0.264 src/mame/cave/cv1k.cpp
static const rom_file DDPDFK_FILES[] = {
	{ "ddpdfk_u4", 0x400000, 0x9976d699, 0, 0, true },
	{ "ddpdfk_u2", 0x8400000, 0x84a51a4f, 1, 0, false },
	{ "u23", 0x400000, 0x27032cde, 2, 0, true },
	{ "u24", 0x400000, 0xa6178c2c, 2, 0x400000, true },
	{ nullptr, 0, 0, 0, 0, false },
};
static const rom_file DDPDFK10_FILES[] = {
	{ "ddpdfk10_u4", 0x400000, 0xa3d650b2, 0, 0, true },
	{ "ddpdfk10_u2", 0x8400000, 0xd349cb2a, 1, 0, false },
	{ "u23", 0x400000, 0x27032cde, 2, 0, true },
	{ "u24", 0x400000, 0xa6178c2c, 2, 0x400000, true },
	{ nullptr, 0, 0, 0, 0, false },
};
static const rom_file DFKBL_FILES[] = {
	{ "u4", 0x400000, 0x8092ca9d, 0, 0, true },
	{ "u2", 0x8400000, 0x29f9d73a, 1, 0, false },
	{ "u23", 0x400000, 0x36d4093b, 2, 0, true },
	{ "u24", 0x400000, 0x31f9eb0a, 2, 0x400000, true },
	{ nullptr, 0, 0, 0, 0, false },
};

static const version_info VERSIONS[VERSION_COUNT] = {
	{ version::ddpdfk, "ddpdfk", "ddpdfk", "Dai-Fukkatsu Ver 1.5",
	  "DoDonPachi Dai-Fukkatsu Ver 1.5 (2008/06/23 MASTER VER 1.5)", "ddpdfk.zip", DDPDFK_FILES, 0x02310, 0xc1d1346 },
	{ version::ddpdfk10, "ddpdfk10", "ddpdfk10", "Dai-Fukkatsu Ver 1.0",
	  "DoDonPachi Dai-Fukkatsu Ver 1.0 (2008/05/16 MASTER VER)", "ddpdfk10.zip", DDPDFK10_FILES, 0x02310, 0xc1d1346 },
	{ version::dfkbl, "dfkbl", "dfkbl", "Dai-Fukkatsu Black Label",
	  "DoDonPachi Dai-Fukkatsu Black Label (2010/1/18 BLACK LABEL)", "dfkbl.zip", DFKBL_FILES, 0x02310, 0xc1d1346 },
};

const version_info &info(version v) { return VERSIONS[int(v)]; }

const version_info *find_version(const char *s)
{
	if (!s) return nullptr;
	for (const auto &v : VERSIONS)
		if (!std::strcmp(s, v.id) || !std::strcmp(s, v.mame_set))
			return &v;
	return nullptr;
}

static std::string dir_of(const char *path)
{
	std::string p(path ? path : "");
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

namespace {
// the set's own source, then (for ddpdfk10) the parent's archive beside it
struct sources {
	std::vector<std::unique_ptr<rom_source>> list;
	explicit sources(version v, const char *path)
	{
		list.emplace_back(new rom_source(path));
		if (v == version::ddpdfk10) {
			std::string d = dir_of(path);
			for (const char *n : { "ddpdfk.zip", "ddpdfk" }) {
				std::string p = d + "/" + n;
				if (p != path)
					list.emplace_back(new rom_source(p.c_str()));
			}
		}
	}
	bool ok() const { return list[0]->ok; }
	rom_source::item *find(const rom_entry &e, rom_source **src)
	{
		for (auto &s : list) {
			if (!s->ok) continue;
			// the parent only supplies what the sets share (same CRC)
			rom_source::item *it = s->find(e);
			if (it && (s == list[0] || (it->size == e.size && s->crc_of(*it) == e.crc))) {
				*src = s.get();
				return it;
			}
		}
		return nullptr;
	}
};
} // namespace

bool check_roms(version v, const char *path, std::string &err, const version_info **found_other)
{
	if (found_other) *found_other = nullptr;
	sources S(v, path);
	if (!S.ok()) { err = std::string("cannot open ") + (path ? path : "(none)"); return false; }
	std::string missing, bad;
	for (const rom_file *f = info(v).files; f->name; f++) {
		rom_source *src = nullptr;
		rom_source::item *it = S.find(*f, &src);
		if (!it) { missing += missing.empty() ? f->name : std::string(", ") + f->name; continue; }
		if (it->size != f->size || src->crc_of(*it) != f->crc)
			bad += bad.empty() ? f->name : std::string(", ") + f->name;
	}
	if (missing.empty() && bad.empty())
		return true;
	// is it another version's set?
	if (found_other)
		for (const auto &o : VERSIONS)
			if (o.v != v) {
				std::string e2;
				if (check_roms(o.v, path, e2, nullptr)) { *found_other = &o; break; }
			}
	err = std::string(info(v).title) + " (MAME set '" + info(v).mame_set + "'): ";
	if (!missing.empty()) err += "missing " + missing;
	if (!bad.empty()) err += (missing.empty() ? "" : "; ") + std::string("wrong size or CRC: ") + bad;
	if (found_other && *found_other)
		err += std::string(" - this looks like the set for ") + (*found_other)->title;
	return false;
}

const version_info *identify_roms(const char *path)
{
	for (const auto &v : VERSIONS) {
		std::string e;
		if (check_roms(v.v, path, e, nullptr))
			return &v;
	}
	return nullptr;
}

bool load_roms(version v, const char *path, rom_set &out, std::string &err)
{
	if (!check_roms(v, path, err))
		return false;
	sources S(v, path);
	out = rom_set();
	out.v = v;
	// ROM_REGION( 0x400000, "maincpu", ROMREGION_ERASEFF ): a big-endian 64-bit
	// region, held as MAME holds it (one host-order 64-bit word per 8 bytes)
	std::vector<uint8_t> prog(0x400000, 0xff), nand(0x8400000, 0xff), ymz(0x800000, 0xff);
	for (const rom_file *f = info(v).files; f->name; f++) {
		rom_source *src = nullptr;
		rom_source::item *it = S.find(*f, &src);
		std::vector<uint8_t> d;
		if (!it || !src->read(*it, d) || d.size() != f->size) { err = std::string("cannot read ") + f->name; return false; }
		std::vector<uint8_t> &region = f->region == 0 ? prog : f->region == 1 ? nand : ymz;
		for (uint32_t i = 0; i < f->size; i++)
			region[f->offset + i] = d[f->word_swap ? (i ^ 1) : i];
		if (f->region == 0)
			out.fingerprint = uint32_t(mz_crc32(MZ_CRC32_INIT, d.data(), d.size()));
	}
	out.prog.resize(0x400000 / 8);
	for (size_t i = 0; i < out.prog.size(); i++) {
		uint64_t w = 0;
		for (int b = 0; b < 8; b++)
			w = (w << 8) | prog[i * 8 + b];
		out.prog[i] = w;
	}
	out.nand.swap(nand);
	out.ymz.swap(ymz);
	return true;
}

} // namespace cv1k
