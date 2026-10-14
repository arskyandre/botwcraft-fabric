"""Read-only load sample of the owned Cemu and Minecraft processes.

Title-bar FPS, process and per-thread CPU time, per-process GPU engine
utilisation (Windows performance counters) and the bridge's own counters over
one interval. Nothing is injected and no process is changed.
"""
import ctypes as C, ctypes.wintypes as W, json, pathlib, re, statistics, subprocess, time

k = C.WinDLL('kernel32', use_last_error=True)
u = C.WinDLL('user32', use_last_error=True)
k.OpenThread.restype = W.HANDLE; k.OpenThread.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k.OpenProcess.restype = W.HANDLE; k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k.CreateToolhelp32Snapshot.restype = W.HANDLE; k.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
k.GetThreadTimes.argtypes = k.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
k.GetThreadDescription.argtypes = [W.HANDLE, C.POINTER(C.c_wchar_p)]; k.GetThreadDescription.restype = C.c_long
k.CloseHandle.argtypes = [W.HANDLE]; k.LocalFree.argtypes = [C.c_void_p]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]


class THREADENTRY32(C.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD), ('th32OwnerProcessID', W.DWORD),
                ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG), ('dwFlags', W.DWORD)]


def _seconds(*filetimes): return sum(t.dwLowDateTime + (t.dwHighDateTime << 32) for t in filetimes) / 1e7


def threads(pid):
    """{thread id: (cpu seconds, description)} for one process."""
    snap = k.CreateToolhelp32Snapshot(4, 0); entry = THREADENTRY32(); entry.dwSize = C.sizeof(entry); found = {}
    try:
        ok = k.Thread32First(snap, C.byref(entry))
        while ok:
            if entry.th32OwnerProcessID == pid:
                h = k.OpenThread(0x0800, False, entry.th32ThreadID)  # THREAD_QUERY_LIMITED_INFORMATION
                if h:
                    t = [W.FILETIME() for _ in range(4)]
                    if k.GetThreadTimes(h, *[C.byref(x) for x in t]):
                        name = C.c_wchar_p()
                        text = ''
                        if k.GetThreadDescription(h, C.byref(name)) >= 0 and name.value is not None:
                            text = name.value; k.LocalFree(C.cast(name, C.c_void_p))
                        found[entry.th32ThreadID] = (_seconds(t[2], t[3]), text)
                    k.CloseHandle(h)
            ok = k.Thread32Next(snap, C.byref(entry))
    finally: k.CloseHandle(snap)
    return found


def process_cpu(pid):
    h = k.OpenProcess(0x1000, False, pid)
    if not h: return None
    try:
        t = [W.FILETIME() for _ in range(4)]
        return _seconds(t[2], t[3]) if k.GetProcessTimes(h, *[C.byref(x) for x in t]) else None
    finally: k.CloseHandle(h)


GPU_SCRIPT = r"""
$samples = Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples %d -ErrorAction SilentlyContinue
$sum = @{}
foreach ($s in $samples) { foreach ($c in $s.CounterSamples) {
  if ($c.InstanceName -match 'pid_(\d+)_.*engtype_(\w+)') { $key = $Matches[1] + ' ' + $Matches[2]; $sum[$key] = [double]$sum[$key] + $c.CookedValue } } }
$n = [Math]::Max(1, @($samples).Count)
$sum.GetEnumerator() | Where-Object { $_.Value / $n -ge 0.5 } | ForEach-Object { '{0} {1:F2}' -f $_.Key, ($_.Value / $n) }
"""



def timeline_summary(rows):
    """Where late frames differ from frames on time (frame_timeline rows).

    Offsets are milliseconds after the previous present; a late frame is one
    presented more than 40 ms after it. `after_harvest` counts frames in which
    a native collision harvest ran, split the same way.
    """
    keys = ('t_wait', 't_start', 't_post', 't_input', 't_camera', 't_sample', 't_harvest', 't_harvest_end', 't_draw')
    def describe(group):
        out = {'frames': len(group)}
        for key in keys:
            values = sorted(r[key] for r in group if r.get(key, -1) >= 0)
            if values: out[key] = {'n': len(values), 'median': round(values[len(values) // 2], 1), 'p90': round(values[int(len(values) * .9)], 1)}
        spans = sorted(r['t_harvest_end'] - r['t_harvest'] for r in group if r['t_harvest'] >= 0 and r['t_harvest_end'] >= 0)
        if spans: out['harvest_ms'] = {'n': len(spans), 'median': round(spans[len(spans) // 2], 2), 'p90': round(spans[int(len(spans) * .9)], 2), 'max': round(spans[-1], 2)}
        return out
    late = [r for r in rows if r['interval_ms'] > 40]; timely = [r for r in rows if r['interval_ms'] <= 40]
    harvested = [r for r in rows if r['t_harvest'] >= 0]; plain = [r for r in rows if r['t_harvest'] < 0]
    share = lambda group: round(sum(r['interval_ms'] > 40 for r in group) / len(group), 3) if group else None
    return {'on_time': describe(timely), 'late': describe(late), 'late_share_with_harvest': share(harvested), 'late_share_without_harvest': share(plain),
            'frames_with_harvest': len(harvested), 'frames_without_harvest': len(plain)}

def sample(lab, seconds, pids):
    """pids: {'cemu': pid, 'minecraft': pid or None}. Returns one phase record."""
    import rig
    seconds = max(4, int(seconds))
    gpu = subprocess.Popen(['powershell', '-NoProfile', '-Command', GPU_SCRIPT % seconds], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    smi = subprocess.Popen(['nvidia-smi', '--query-gpu=utilization.gpu,memory.used', '--format=csv,noheader,nounits', '-l', '1'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    hwnd, _ = rig.window(lab)
    frames = lab / 'frames.jsonl'; start_offset = frames.stat().st_size
    before = {name: (process_cpu(pid), threads(pid)) for name, pid in pids.items() if pid}
    began = time.perf_counter(); fps = []
    while time.perf_counter() - began < seconds:
        title = C.create_unicode_buffer(1024); u.GetWindowTextW(hwnd, title, 1024)
        match = re.search(r'FPS: ([0-9.]+)', title.value)
        if match: fps.append(float(match[1]))
        time.sleep(.25)
    elapsed = time.perf_counter() - began
    record = {'seconds': elapsed, 'fps_median': statistics.median(fps) if fps else None, 'fps_min': min(fps, default=None), 'fps_max': max(fps, default=None)}
    for name, pid in pids.items():
        if not pid: continue
        cpu0, threads0 = before[name]; cpu1, threads1 = process_cpu(pid), threads(pid)
        busiest = sorted(((threads1[t][0] - threads0.get(t, (0, ''))[0]) / elapsed, t, threads1[t][1]) for t in threads1)[::-1][:8]
        record[name] = {'cpu_cores': (cpu1 - cpu0) / elapsed if cpu0 is not None and cpu1 is not None else None,
                        'threads': [{'core_share': round(share, 3), 'tid': tid, 'name': text} for share, tid, text in busiest if share >= .02]}
    smi.terminate(); lines = [l.split(',') for l in smi.communicate()[0].splitlines() if ',' in l]
    if lines:
        record['gpu_total_percent'] = statistics.mean(float(l[0]) for l in lines); record['gpu_memory_mb'] = max(float(l[1]) for l in lines)
    engines = {}
    for line in gpu.communicate(timeout=seconds + 30)[0].splitlines():
        parts = line.split()
        if len(parts) == 3:
            owner = next((n for n, p in pids.items() if p and str(p) == parts[0]), 'pid ' + parts[0])
            engines[f'{owner} {parts[1]}'] = float(parts[2].replace(',', '.'))
    record['gpu_engine_percent'] = engines
    # Bridge counters over the same interval.
    first, last, draws = {}, {}, []
    uploads = {'hud': 0, 'atlas': 0, 'section': 0}; bytes0 = bytes1 = None
    pacing = {'frames': 0, 'late': 0, 'max_ms': 0.0}; work = [0] * 14; timeline = []; phases = {}; paced = {'frames': 0, 'early': 0, 'held': 0, 'budget_ms': None}
    with frames.open('rb') as f:
        if frames.stat().st_size >= start_offset: f.seek(start_offset)
        for raw in f:
            if b'"callback_cpu"' in raw or b'"draw"' in raw or b'_upload"' in raw or b'"live_gpu_generation"' in raw or b'"frame_pacing"' in raw or b'"frame_timeline"' in raw or b'"frame_phase"' in raw or b'"frame_pace"' in raw:
                try: e = json.loads(raw)
                except ValueError: continue
                name = e.get('event')
                if name == 'callback_cpu':
                    key = e['callback']; first.setdefault(key, e); last[key] = e
                elif name == 'frame_pacing':
                    pacing['frames'] += e['frames']; pacing['late'] += e['late']; pacing['max_ms'] = max(pacing['max_ms'], e['max_ms'])
                    for index, count in enumerate(e.get('work', [])): work[index] += count
                elif name == 'frame_timeline': timeline.append(e)
                elif name == 'frame_pace':
                    for key in ('frames', 'early', 'held'): paced[key] += e.get(key, 0)
                    paced['budget_ms'] = e['budget_ms']
                elif name == 'frame_phase':
                    total = phases.setdefault(e['name'], [0] * 60)
                    for index, count in enumerate(e['ms']): total[index] += count
                elif name == 'draw': draws.append(e['cpu_qpc'] / 1e4)
                elif name == 'live_hud_upload': uploads['hud'] += 1
                elif name == 'live_atlas_upload': uploads['atlas'] += 1
                elif name == 'live_section_upload': uploads['section'] += 1
                elif name == 'live_gpu_generation':
                    bytes0 = e['upload_bytes'] if bytes0 is None else bytes0; bytes1 = e['upload_bytes']
    bridge = {}
    for key in first:
        dt = (last[key]['qpc'] - first[key]['qpc']) / 1e7
        if dt > 0:
            calls = last[key]['calls'] - first[key]['calls']
            bridge[key] = {'calls_per_s': round(calls / dt), 'ms_per_s': round((last[key]['ticks'] - first[key]['ticks']) / 1e4 / dt, 2)}
    record['bridge_callbacks'] = bridge
    # Frames as presented: "late" ones missed 33.3 ms and were shown after 50.
    record['frame_pacing'] = dict(pacing, late_share=round(pacing['late'] / pacing['frames'], 3) if pacing['frames'] else None)
    if timeline: record['frame_timeline'] = timeline_summary(timeline)
    # Frame loop decisions: frames begun, begun early (frame-pace budget), swaps held to the middle blank.
    record['frame_pace'] = paced
    # Calls per whole millisecond since the last present, for a few callbacks.
    if phases: record['frame_phase'] = phases
    # Work time per frame (controller read to present, without the vertical-blank wait), 3 ms buckets.
    total = sum(work)
    if total:
        def quantile(q):
            seen = 0
            for index, count in enumerate(work):
                seen += count
                if seen >= q * total: return index * 3 + 1.5
        record['frame_work_ms'] = {'median': quantile(.5), 'p90': quantile(.9), 'p99': quantile(.99), 'over_30ms_share': round(sum(work[10:]) / total, 3), 'buckets_3ms': work}
    if draws:
        draws.sort()
        record['mc_draw_cpu_ms'] = {'count': len(draws), 'mean': round(statistics.mean(draws), 3), 'median': round(draws[len(draws) // 2], 3), 'p95': round(draws[int(len(draws) * .95)], 3), 'max': round(draws[-1], 3)}
    record['uploads_per_s'] = {key: round(value / elapsed, 1) for key, value in uploads.items()}
    if bytes0 is not None: record['upload_mb_per_s'] = round((bytes1 - bytes0) / elapsed / 1e6, 1)
    return record
