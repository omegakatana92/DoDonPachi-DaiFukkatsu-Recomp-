#!/usr/bin/env python3
"""Import the MAME 0.264 code this port reuses.

    tools/import_mame.py MAME_SOURCE_TREE

1. The SH-3 interpreter, into runtime/sh3/.

MAME's SH cores (src/devices/cpu/sh, BSD-3-Clause, R. Belmont, David Haywood
and others) spread the SH-3 over a class hierarchy (sh_common_execution ->
sh34_base_device -> sh3_base_device -> sh3be_device) and mix the interpreter
with the DRC.  This script copies the interpreter's function definitions
unchanged into one flat class, sh3_device:

  - every function of those four classes, except the DRC, debugger, state and
    construction functions and the SH-4-only class sh4_base_device;
  - the file-level tables and static helpers of the same files;
  - "Class::" qualifiers become "sh3_device::"; where two levels define the
    same function, the base version is renamed (sh34_device_reset,
    common_execute_one_0000, common_execute_one_4000) and the explicit calls
    to it follow the rename;
  - "inline" is dropped from member definitions (one translation unit).

Outputs runtime/sh3/sh3_mame_defs.inc (definitions) and
runtime/sh3/sh3_mame_decls.inc (the matching member declarations).  Nothing
else in the bodies is changed; the MAME names they use (attotime, emu_timer,
m_program, logerror, ...) are provided by runtime/sh3/sh3_shim.h.

2. The CV1000 board devices, into runtime/devices/: the member functions of
the EP1C12 blitter (src/mame/cave/ep1c12.cpp), the NAND flash
(src/devices/machine/nandflash.cpp), the RTC9701 (rtc9701.cpp) and the YMZ770
(src/devices/sound/ymz770.cpp), less their construction, start-up, save-state
and MAME-interface functions, which the hand-written device headers
reimplement; and, copied whole, the blitter's drawing functions
(ep1c12_blit0-8.cpp, ep1c12in.ipp, ep1c12pixel.ipp) and the MPEG audio
decoder (mpeg_audio.cpp/.h).
"""
import os
import re
import sys

FILES = ["sh.cpp", "sh4.cpp", "sh4comn.cpp", "sh3comn.cpp", "sh4tmu.cpp", "sh4dmac.cpp"]
CLASSES = ["sh_common_execution", "sh34_base_device", "sh3_base_device", "sh3be_device"]
EXCLUDE_PREFIX = ("func_", "generate_", "static_generate", "code_", "log_", "sh2drc_")
EXCLUDE = {
    "device_start", "drc_start", "load_fast_iregs", "save_fast_iregs", "epc", "alloc_handle",
    "init_drc_frontend", "get_desclist", "execute_run_drc", "state_import", "state_export",
    "state_string_export", "create_disassembler", "memory_space_config", "sh4_set_ftcsr_callback",
    "sh3_internal_map", "sh4_internal_map", "generate_update_cycles",
}
# (class, name) -> new name, for functions defined at two levels
RENAME = {
    ("sh34_base_device", "device_reset"): "sh34_device_reset",
    ("sh_common_execution", "execute_one_0000"): "common_execute_one_0000",
    ("sh_common_execution", "execute_one_4000"): "common_execute_one_4000",
}
# only one of the duplicates is kept
SKIP = {("sh34_base_device", "execute_run"),
        # written by hand in runtime/sh3/sh3_core.cpp: MAME's loop plus the
        # entry into recompiled code
        ("sh3be_device", "execute_run")}


def strip_if0(text):
    out, depth, skipping = [], 0, 0
    for line in text.split("\n"):
        s = line.strip()
        if skipping:
            if re.match(r"#\s*if", s):
                depth += 1
            elif re.match(r"#\s*endif", s):
                depth -= 1
                if depth == 0:
                    skipping = 0
            continue
        if re.match(r"#\s*if\s+0\b", s):
            skipping, depth = 1, 1
            continue
        out.append(line)
    return "\n".join(out)


def skip_ws_comments(t, i):
    while i < len(t):
        if t.startswith("//", i):
            i = t.find("\n", i)
            if i < 0:
                return len(t)
        elif t.startswith("/*", i):
            i = t.find("*/", i) + 2
        elif t[i].isspace():
            i += 1
        else:
            break
    return i


def match_brace(t, i):
    """t[i] == '{'; return the index after the matching '}'."""
    depth = 0
    while i < len(t):
        c = t[i]
        if t.startswith("//", i):
            i = t.find("\n", i)
            continue
        if t.startswith("/*", i):
            i = t.find("*/", i) + 2
            continue
        if c in "\"'":
            j = i + 1
            while t[j] != c:
                j += 2 if t[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced braces")


HEAD = re.compile(r"^(?P<pre>(?:inline\s+)?[\w:<>\*&\s]*?)\b(?P<cls>\w+)::(?P<name>~?\w+)\s*\(", re.M)
TCM = re.compile(r"^TIMER_CALLBACK_MEMBER\(\s*(?P<cls>\w+)::(?P<name>\w+)\s*\)", re.M)


def items(text):
    """Split a file into top-level chunks: ('func', cls, name, text) or ('other', text)."""
    res, pos = [], 0
    while True:
        m1, m2 = HEAD.search(text, pos), TCM.search(text, pos)
        cands = [m for m in (m1, m2) if m]
        if not cands:
            break
        m = min(cands, key=lambda x: x.start())
        if m.start() > pos:
            res.append(("other", text[pos:m.start()]))
        # the body: first '{' after the signature at depth 0 of parentheses
        i, par = m.end() - (1 if m is m1 else 0), 0
        while True:
            c = text[i]
            if c == "(":
                par += 1
            elif c == ")":
                par -= 1
            elif c == "{" and par == 0:
                break
            elif c == ";" and par == 0:
                i = -1
                break
            i += 1
        if i < 0:   # a declaration, not a definition
            end = text.find(";", m.end()) + 1
            res.append(("other", text[m.start():end]))
            pos = end
            continue
        end = match_brace(text, i)
        res.append(("func", m.group("cls"), m.group("name"), text[m.start():end]))
        pos = end
    res.append(("other", text[pos:]))
    return res


def keep_other(chunk, drop=("cfunc_",)):
    out = []
    for line in chunk.split("\n"):
        s = line.strip()
        if s.startswith("#include") or s.startswith("DEFINE_DEVICE_TYPE") or s.startswith("ALLOW_SAVE_TYPE"):
            continue
        if any(d in s for d in drop):
            continue
        out.append(line)
    return "\n".join(out)


BANNER = ("// Generated by tools/import_mame.py from MAME 0.264 %s\n"
          "// (BSD-3-Clause; see LICENSES/README.md for the authors).  Do not edit.\n")


def import_sh3(mame, outdir):
    src = os.path.join(mame, "src/devices/cpu/sh")
    defs, decls, seen = [BANNER % "src/devices/cpu/sh"], [], set()
    for fn in FILES:
        text = strip_if0(open(os.path.join(src, fn), encoding="utf-8").read())
        defs.append("\n// ---------------------------------------------------------------- %s\n" % fn)
        for it in items(text):
            if it[0] == "other":
                defs.append(keep_other(it[1]))
                continue
            _, cls, name, body = it
            if cls not in CLASSES or (cls, name) in SKIP:
                continue
            if name in EXCLUDE or name.startswith(EXCLUDE_PREFIX) or name == cls:
                continue
            new = RENAME.get((cls, name), name)
            if new in seen:
                raise SystemExit("duplicate function %s::%s" % (cls, name))
            seen.add(new)
            # signature
            head_end = body.index("{")
            head = body[:head_end]
            rest = body[head_end:]
            head = re.sub(r"^(\s*)inline\s+", r"\1", head, flags=re.M)
            head = head.replace("%s::%s" % (cls, name), "sh3_device::" + new, 1)
            for c in CLASSES:
                rest = rest.replace(c + "::device_reset()", "sh34_device_reset()")
                rest = rest.replace(c + "::execute_one_0000(", "common_execute_one_0000(")
                rest = rest.replace(c + "::execute_one_4000(", "common_execute_one_4000(")
                rest = rest.replace(c + "::", "sh3_device::")
            defs.append(head + rest)
            # declaration
            d = re.sub(r"//[^\n]*", "", head).strip()
            if d.startswith("TIMER_CALLBACK_MEMBER"):
                d = "TIMER_CALLBACK_MEMBER(%s)" % new
            else:
                d = d.replace("sh3_device::", "")
            d = re.sub(r"\s*\n\s*", " ", d)
            decls.append("\t" + d + ";")
    open(os.path.join(outdir, "sh3_mame_defs.inc"), "w").write("\n".join(defs))
    open(os.path.join(outdir, "sh3_mame_decls.inc"), "w").write(
        "// Generated by tools/import_mame.py: declarations of sh3_mame_defs.inc.\n" + "\n".join(decls) + "\n")
    # headers copied as they are
    for h in ("sh4regs.h", "sh3comn.h"):
        open(os.path.join(outdir, h), "w").write(open(os.path.join(src, h), encoding="utf-8").read())
    return len(seen)


# device: (source file, class, functions left out, other-text lines dropped)
DEVICES = [
    ("src/mame/cave/ep1c12.cpp", "ep1c12_device", "ep1c12_mame.inc",
     {"ep1c12_device", "device_start", "device_reset", "install_handlers", "blit_request_callback",
      "gfx_exec_w", "draw_screen"}, ()),
    ("src/devices/machine/nandflash.cpp", "nand_device", "nandflash_mame.inc",
     {"device_start", "nvram_read", "nvram_write", "nvram_default"}, ("_device::",)),
    ("src/devices/machine/rtc9701.cpp", "rtc9701_device", "rtc9701_mame.inc",
     {"device_start", "device_validity_check", "nvram_read", "nvram_write"}, ()),
    ("src/devices/sound/ymz770.cpp", "ymz770_device", "ymz770_mame.inc",
     {"device_start"}, ()),
]


def import_device(mame, outdir, path, cls, out, exclude, drop):
    text = strip_if0(open(os.path.join(mame, path), encoding="utf-8").read())
    defs, n = [BANNER % path], 0
    for it in items(text):
        if it[0] == "other":
            defs.append(keep_other(it[1], drop or ("@@none@@",)))
            continue
        _, c, name, body = it
        if c != cls or name in exclude or name == cls:
            continue
        head_end = body.index("{")
        defs.append(re.sub(r"^inline\s+", "", body[:head_end]) + body[head_end:])
        n += 1
    open(os.path.join(outdir, out), "w").write("\n".join(defs))
    return n


def main():
    mame = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "runtime")
    print("sh3: %d functions" % import_sh3(mame, os.path.join(root, "sh3")))
    dev = os.path.join(root, "devices")
    for path, cls, out, exclude, drop in DEVICES:
        print("%s: %d functions" % (cls, import_device(mame, dev, path, cls, out, exclude, drop)))
    # copied whole
    os.makedirs(os.path.join(dev, "ep1c12"), exist_ok=True)
    for f in ["ep1c12_blit%d.cpp" % i for i in range(9)] + ["ep1c12in.ipp", "ep1c12pixel.ipp"]:
        open(os.path.join(dev, "ep1c12", f), "w").write(
            open(os.path.join(mame, "src/mame/cave", f), encoding="utf-8").read())
    for f in ("mpeg_audio.cpp", "mpeg_audio.h"):
        open(os.path.join(dev, f), "w").write(
            open(os.path.join(mame, "src/devices/sound", f), encoding="utf-8").read())


if __name__ == "__main__":
    main()
