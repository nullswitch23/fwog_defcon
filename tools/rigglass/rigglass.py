#!/usr/bin/env python3
"""Push this PC's load to RigGlass over the OG main CDC.

    python tools/rigglass/rigglass.py

Never open the port at 1200 baud.

Line shape (triples are now:2min:10min):

    RG cpu=12:10:8 ram=45:44:40 net=8:5:3 gpu=30:20:15 tmp=61:58:55 host=desk

NET is percent of the busiest NIC's link speed (Task Manager style).
GPU / TMP use 255 when Windows will not expose them.
"""
from __future__ import annotations

import ctypes
import pathlib
import socket
import sys
import time

_HERE = pathlib.Path(__file__).resolve().parent
_TOOLS = _HERE.parent
for _p in (_TOOLS, _HERE):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

RG_NA = 255
WINDOW_2M = 120.0
WINDOW_10M = 600.0

_NET_SKIP = (
    "loopback",
    "isatap",
    "teredo",
    "6to4",
    "bluetooth",
    "wan miniport",
    "pseudo-interface",
    "qos packet",
    "wfp native",
    "kernel debug",
    "virtualbox",
    "vpn",
)

PDH_MORE_DATA = 0x800007D2
PDH_FMT_DOUBLE = 0x00000200
ERROR_SUCCESS = 0

IF_TYPE_SOFTWARE_LOOPBACK = 24
IF_TYPE_TUNNEL = 131
MIB_IF_OPER_STATUS_CONNECTED = 4
MIB_IF_OPER_STATUS_OPERATIONAL = 5


def clamp_pct(v: float) -> int:
    if v < 0:
        return 0
    if v > 100:
        return 100
    return int(round(v))


def clamp_temp_c(v: float) -> int:
    if v < 0:
        return 0
    if v > 120:
        return 120
    return int(round(v))


def net_skip(name: str) -> bool:
    n = name.lower()
    return any(s in n for s in _NET_SKIP)


class Rolling:
    """1 Hz samples; avg() is the mean over the last `seconds` of wall time."""

    def __init__(self, clock=time.monotonic):
        self._clock = clock
        self._s: list[tuple[float, int]] = []

    def add(self, v: int) -> None:
        now = self._clock()
        self._s.append((now, int(v)))
        cut = now - WINDOW_10M
        while self._s and self._s[0][0] < cut:
            self._s.pop(0)

    def avg(self, seconds: float, default: int = 0) -> int:
        now = self._clock()
        vals = [v for t, v in self._s if t >= now - seconds]
        if not vals:
            return default
        return int(round(sum(vals) / len(vals)))

    def triple(self, now: int) -> tuple[int, int, int]:
        self.add(now)
        return now, self.avg(WINDOW_2M, now), self.avg(WINDOW_10M, now)


def fmt_triple(now: int, a2: int, a10: int) -> str:
    return f"{now}:{a2}:{a10}"


def format_line(
    host: str,
    cpu: tuple[int, int, int],
    ram: tuple[int, int, int],
    net: tuple[int, int, int],
    gpu: tuple[int, int, int],
    tmp: tuple[int, int, int],
) -> str:
    h = (host or "pc")[:15]
    return (
        f"RG cpu={fmt_triple(*cpu)} ram={fmt_triple(*ram)} "
        f"net={fmt_triple(*net)} gpu={fmt_triple(*gpu)} "
        f"tmp={fmt_triple(*tmp)} host={h}\n"
    )


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", ctypes.c_ulong), ("dwHighDateTime", ctypes.c_ulong)]


class MIB_IFROW(ctypes.Structure):
    _fields_ = [
        ("wszName", ctypes.c_wchar * 256),
        ("dwIndex", ctypes.c_ulong),
        ("dwType", ctypes.c_ulong),
        ("dwMtu", ctypes.c_ulong),
        ("dwSpeed", ctypes.c_ulong),
        ("dwPhysAddrLen", ctypes.c_ulong),
        ("bPhysAddr", ctypes.c_ubyte * 8),
        ("dwAdminStatus", ctypes.c_ulong),
        ("dwOperStatus", ctypes.c_ulong),
        ("dwLastChange", ctypes.c_ulong),
        ("dwInOctets", ctypes.c_ulong),
        ("dwInUcastPkts", ctypes.c_ulong),
        ("dwInNUcastPkts", ctypes.c_ulong),
        ("dwInDiscards", ctypes.c_ulong),
        ("dwInErrors", ctypes.c_ulong),
        ("dwInUnknownProtos", ctypes.c_ulong),
        ("dwOutOctets", ctypes.c_ulong),
        ("dwOutUcastPkts", ctypes.c_ulong),
        ("dwOutNUcastPkts", ctypes.c_ulong),
        ("dwOutDiscards", ctypes.c_ulong),
        ("dwOutErrors", ctypes.c_ulong),
        ("dwOutQLen", ctypes.c_ulong),
        ("dwDescrLen", ctypes.c_ulong),
        ("bDescr", ctypes.c_ubyte * 256),
    ]


class _PdhU(ctypes.Union):
    _fields_ = [
        ("longValue", ctypes.c_long),
        ("doubleValue", ctypes.c_double),
        ("largeValue", ctypes.c_longlong),
    ]


class PDH_FMT_COUNTERVALUE(ctypes.Structure):
    _fields_ = [("CStatus", ctypes.c_ulong), ("u", _PdhU)]


class PDH_FMT_COUNTERVALUE_ITEM_W(ctypes.Structure):
    _fields_ = [("szName", ctypes.c_wchar_p), ("FmtValue", PDH_FMT_COUNTERVALUE)]


def _ft(ft: FILETIME) -> int:
    return (ft.dwHighDateTime << 32) | ft.dwLowDateTime


def ram_pct() -> int:
    st = MEMORYSTATUSEX()
    st.dwLength = ctypes.sizeof(st)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st)):
        return 0
    return int(st.dwMemoryLoad)


def cpu_times() -> tuple[int, int]:
    idle = FILETIME()
    kernel = FILETIME()
    user = FILETIME()
    ctypes.windll.kernel32.GetSystemTimes(
        ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)
    )
    return _ft(idle), _ft(kernel) + _ft(user)


def cpu_pct(idle0: int, tot0: int, idle1: int, tot1: int) -> int:
    didle = idle1 - idle0
    dtot = tot1 - tot0
    if dtot <= 0:
        return 0
    return clamp_pct(100 - (100 * didle) / dtot)


def _if_descr(row: MIB_IFROW) -> str:
    n = int(row.dwDescrLen)
    if n > 256:
        n = 256
    raw = bytes(row.bDescr[:n])
    return raw.split(b"\x00", 1)[0].decode("latin-1", "replace")


def _if_table() -> list[MIB_IFROW]:
    iph = ctypes.windll.iphlpapi
    size = ctypes.c_ulong(0)
    iph.GetIfTable(None, ctypes.byref(size), True)
    if size.value == 0:
        return []
    buf = ctypes.create_string_buffer(size.value)
    err = iph.GetIfTable(buf, ctypes.byref(size), True)
    if err != 0:
        return []
    num = ctypes.c_ulong.from_buffer_copy(buf, 0).value
    if num == 0:
        return []
    rows = (MIB_IFROW * num).from_buffer_copy(buf, ctypes.sizeof(ctypes.c_ulong))
    return list(rows)


class NetSampler:
    """Task Manager-style: (in+out bits/s) / link speed on the busiest NIC."""

    def __init__(self):
        self._prev: dict[int, tuple[int, int]] = {}
        self._t = time.monotonic()

    def pct(self) -> int:
        now = time.monotonic()
        dt = now - self._t
        if dt <= 0:
            dt = 0.001
        best_pct = 0.0
        saw = False
        nxt: dict[int, tuple[int, int]] = {}
        for row in _if_table():
            if row.dwType in (IF_TYPE_SOFTWARE_LOOPBACK, IF_TYPE_TUNNEL):
                continue
            name = f"{row.wszName} {_if_descr(row)}"
            if net_skip(name):
                continue
            speed = float(row.dwSpeed)
            if speed <= 0:
                continue
            idx = int(row.dwIndex)
            inn, out = int(row.dwInOctets), int(row.dwOutOctets)
            nxt[idx] = (inn, out)
            if idx not in self._prev:
                continue
            pinn, pout = self._prev[idx]
            din = (inn - pinn) & 0xFFFFFFFF
            dout = (out - pout) & 0xFFFFFFFF
            bits = (din + dout) * 8.0 / dt
            saw = True
            pct = 100.0 * bits / speed
            if pct > best_pct:
                best_pct = pct
        self._prev = nxt
        self._t = now
        if not saw:
            return 0
        if best_pct > 0 and best_pct < 1:
            return 1
        return clamp_pct(best_pct)


def extra_cpu_temp_c() -> float | None:
    """LibreHardwareMonitor WMI, if that app is installed."""
    try:
        import win32com.client as w32  # type: ignore
    except ImportError:
        return None
    best: float | None = None
    try:
        svc = w32.GetObject(r"winmgmts:root\LibreHardwareMonitor")
        for z in svc.InstancesOf("Sensor"):
            if str(getattr(z, "SensorType", "")) != "Temperature":
                continue
            name = str(getattr(z, "Name", "")).lower()
            if not any(k in name for k in ("cpu", "package", "tdie", "tctl")):
                continue
            v = float(z.Value)
            if v < 0 or v > 120:
                continue
            if best is None or v > best:
                best = v
    except Exception:
        return None
    return best


def _kelvin_tenths_to_c(v: float) -> float | None:
    """PDH thermal is tenths of a kelvin; some machines already report C."""
    if v > 200:
        c = v / 10.0 - 273.15
    else:
        c = v
    if c < 0 or c > 120:
        return None
    return c


class PdhWild:
    def __init__(self):
        self._pdh = ctypes.windll.pdh
        self._q = ctypes.c_void_p()
        self._gpu = None
        self._tmp = None
        self._nbytes = None
        self._nbw = None
        ok = self._pdh.PdhOpenQueryW(None, None, ctypes.byref(self._q))
        if ok != ERROR_SUCCESS:
            self._q = None
            return
        self._gpu = self._add(r"\GPU Engine(*)\Utilization Percentage")
        self._tmp = self._add(
            r"\Thermal Zone Information(*)\High Precision Temperature"
        )
        if not self._tmp:
            self._tmp = self._add(r"\Thermal Zone Information(*)\Temperature")
        self._nbytes = self._add(r"\Network Interface(*)\Bytes Total/sec")
        self._nbw = self._add(r"\Network Interface(*)\Current Bandwidth")
        self._pdh.PdhCollectQueryData(self._q)

    def _add(self, path: str):
        h = ctypes.c_void_p()
        st = self._pdh.PdhAddEnglishCounterW(
            self._q, path, None, ctypes.byref(h)
        )
        return h if st == ERROR_SUCCESS and h else None

    def collect(self) -> None:
        if self._q:
            self._pdh.PdhCollectQueryData(self._q)

    def _items(self, counter) -> list[tuple[str, float]]:
        if not self._q or not counter:
            return []
        size = ctypes.c_ulong(0)
        count = ctypes.c_ulong(0)
        st = self._pdh.PdhGetFormattedCounterArrayW(
            counter,
            PDH_FMT_DOUBLE,
            ctypes.byref(size),
            ctypes.byref(count),
            None,
        )
        stu = st & 0xFFFFFFFF
        if stu != PDH_MORE_DATA and st != ERROR_SUCCESS:
            return []
        if size.value == 0:
            return []
        buf = ctypes.create_string_buffer(size.value)
        st = self._pdh.PdhGetFormattedCounterArrayW(
            counter,
            PDH_FMT_DOUBLE,
            ctypes.byref(size),
            ctypes.byref(count),
            buf,
        )
        if st != ERROR_SUCCESS or count.value == 0:
            return []
        arr = (PDH_FMT_COUNTERVALUE_ITEM_W * count.value).from_buffer(buf)
        out: list[tuple[str, float]] = []
        for i in range(count.value):
            it = arr[i]
            name = it.szName or ""
            if it.FmtValue.CStatus not in (0, 1):
                continue
            out.append((name, float(it.FmtValue.u.doubleValue)))
        return out

    def gpu_pct(self) -> int | None:
        items = self._items(self._gpu)
        if not items:
            return None
        three = [v for n, v in items if "engtype_3d" in n.lower()]
        use = three if three else [v for _, v in items]
        if not use:
            return None
        return clamp_pct(max(use))

    def net_pct(self) -> int | None:
        """Task Manager: 100 * (bytes/s * 8) / current bandwidth, busiest NIC."""
        by_name = {n: v for n, v in self._items(self._nbytes)}
        bw_name = {n: v for n, v in self._items(self._nbw)}
        if not by_name or not bw_name:
            return None
        best = 0.0
        any_ok = False
        for name, bps in by_name.items():
            if net_skip(name):
                continue
            bw = bw_name.get(name)
            if bw is None or bw <= 0:
                continue
            any_ok = True
            pct = 100.0 * (bps * 8.0) / bw
            if pct > best:
                best = pct
        if not any_ok:
            return None
        if best > 0 and best < 1:
            return 1
        return clamp_pct(best)

    def cpu_temp_c(self) -> int | None:
        extra = extra_cpu_temp_c()
        if extra is not None:
            return clamp_temp_c(extra)
        items = self._items(self._tmp)
        best: float | None = None
        for _, v in items:
            c = _kelvin_tenths_to_c(v)
            if c is None:
                continue
            if best is None or c > best:
                best = c
        if best is None:
            return None
        return clamp_temp_c(best)


def main() -> int:
    import fw  # noqa: WPS433 — only when talking to the board

    ports = fw._cpu_ports()
    port = fw._pick_cpu_port(ports, "main")
    if not port:
        print("no FWOG main CDC", file=sys.stderr)
        return 1
    import serial

    ser = serial.Serial(port, 115200, timeout=0.2)
    host = socket.gethostname()[:15]
    idle0, tot0 = cpu_times()
    net = NetSampler()
    pdh = PdhWild()
    r_cpu, r_ram, r_net = Rolling(), Rolling(), Rolling()
    r_gpu, r_tmp = Rolling(), Rolling()
    try:
        while True:
            time.sleep(1.0)
            pdh.collect()
            idle1, tot1 = cpu_times()
            cpu = cpu_pct(idle0, tot0, idle1, tot1)
            idle0, tot0 = idle1, tot1
            ram = ram_pct()
            n = pdh.net_pct()
            if n is None:
                n = net.pct()
            g = pdh.gpu_pct()
            t = pdh.cpu_temp_c()
            gpu_t = r_gpu.triple(g) if g is not None else (RG_NA, RG_NA, RG_NA)
            tmp_t = r_tmp.triple(t) if t is not None else (RG_NA, RG_NA, RG_NA)
            line = format_line(
                host,
                r_cpu.triple(cpu),
                r_ram.triple(ram),
                r_net.triple(n),
                gpu_t,
                tmp_t,
            )
            ser.write(line.encode("ascii", "replace"))
    except KeyboardInterrupt:
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
