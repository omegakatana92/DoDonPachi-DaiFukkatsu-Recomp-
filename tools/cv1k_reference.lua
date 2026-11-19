-- MAME 0.264 reference capture for the CV1000 port (ddpdfk, ddpdfk10, dfkbl).
--   CV_FRAMES=3600 CV_INPUTS=inputs.txt CV_SNAP=600 CV_DUMPS="100 200" \
--   mame ddpdfk -nodrc -video none -sound none -nothrottle \
--        -nvram_directory nv -autoboot_script cv1k_reference.lua
-- Every frame: one line of SH-3 registers in cpu.txt.  Every CV_EVERY frames
-- (default 1): main RAM (0x0C000000-0x0CFFFFFF) piped to tools/hashdump
-- (64 KB block hashes in hashes.bin, full dumps of the frames in CV_DUMPS).
-- (0xF0000000-0xF0FFFFFF, the other RAM in the map, stays unused.)
-- CV_INPUTS: "FRAME MASK" lines, MASK bits in the order of names below
-- (runtime/include/cv1k/machine.h IN_*); MAME applies set_value() at the next
-- frame (compare with --mame-latency).
local frame = 0
local N = tonumber(os.getenv("CV_FRAMES") or "3600")
local snap = tonumber(os.getenv("CV_SNAP") or "600")
local every = tonumber(os.getenv("CV_EVERY") or "1")
local hd = os.getenv("CV_HASHDUMP") or "hashdump"
local names = {"P1 Up","P1 Down","P1 Left","P1 Right","P1 Button 1","P1 Button 2","P1 Button 3","P1 Button 4",
  "P2 Up","P2 Down","P2 Left","P2 Right","P2 Button 1","P2 Button 2","P2 Button 3","P2 Button 4",
  "Coin 1","Coin 2","1 Player Start","2 Players Start","Service 1","Service 2","Service Mode"}
local fields = {}
for tag, port in pairs(manager.machine.ioport.ports) do
  for name, f in pairs(port.fields) do fields[name] = f end
end
for i, n in ipairs(names) do
  if not fields[n] then print("cv1k_reference.lua: no input field '" .. n .. "'") end
end
local ev = {}
local input_file = os.getenv("CV_INPUTS")
if input_file then
  for line in io.lines(input_file) do local a, b = line:match("(%d+) (%d+)"); ev[#ev+1] = {tonumber(a), tonumber(b)} end
end
local ei = 1
local cpu = manager.machine.devices[":maincpu"]
local share = manager.machine.memory.shares[":mainram"]
local sp = cpu.spaces["program"]
local regs = {"PC","SR","PR","GBR","VBR","MACH","MACL","R0","R1","R2","R3","R4","R5","R6","R7","R8","R9","R10","R11","R12","R13","R14","R15","SPC","SSR","SGR"}
local cpuf = io.open("cpu.txt", "w")
local pipe = io.popen(hd .. " hashes.bin dumps " .. (os.getenv("CV_DUMPS") or ""), "w")
fs = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  while ei <= #ev and ev[ei][1] <= frame do
    local m = ev[ei][2]
    for i, n in ipairs(names) do
      local f = fields[n]
      if f then f:set_value(((m >> (i - 1)) & 1)) end
    end
    ei = ei + 1
  end
  local t = {tostring(frame)}
  for _, r in ipairs(regs) do t[#t+1] = string.format("%08X", cpu.state[r].value) end
  cpuf:write(table.concat(t, " "), "\n")
  if frame % every == 0 then
    -- 0x0C002310-7 is read through the share: a read there through the
    -- address space calls cv1k_state::speedup_r, which may suspend the CPU
    local idle = {}
    for i = 0, 7 do idle[#idle+1] = string.char(share:read_u8(0x2310 + i)) end
    local a = sp:read_range(0x0c000000, 0x0c00230f, 8)
    local b = sp:read_range(0x0c002318, 0x0cffffff, 8)
    pipe:write("DFKR", string.pack("<I4I4", frame, #a + 8 + #b), a, table.concat(idle), b)
  end
  if snap > 0 and frame % snap == 0 then manager.machine.video:snapshot() end
  if frame >= N then cpuf:close(); pipe:close(); manager.machine:exit() end
end)
