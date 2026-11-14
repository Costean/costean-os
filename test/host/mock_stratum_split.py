#!/usr/bin/env python3
"""Stand up several fake Stratum V1 servers and verify Costean split
share routing and ratios using the real pool_split_core scheduler.

This is a host-level test (no QEMU / no hardware). Each fake pool accepts
connections, issues mining.notify, and records which shares it received.
A simulated miner uses pool_split_pick() (via a small C helper, or a
faithful Python port checked against the C binary) to choose the job
source and submits the share only to that pool.
"""
import ctypes
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE_C = ROOT / "main/pool_split_core.c"
CORE_H = ROOT / "main/pool_split_core.h"

def build_lib():
    d = Path(tempfile.mkdtemp(prefix="costean-split-"))
    so = d / "libsplit.so"
    subprocess.check_call([
        "gcc", "-std=c11", "-shared", "-fPIC", "-O2",
        "-I", str(CORE_H.parent),
        str(CORE_C), "-o", str(so),
    ])
    lib = ctypes.CDLL(str(so))
    lib.pool_split_pick.argtypes = [
        ctypes.POINTER(ctypes.c_uint16),
        ctypes.POINTER(ctypes.c_bool),
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_int,
        ctypes.c_uint32,
    ]
    lib.pool_split_pick.restype = ctypes.c_int
    lib.pool_split_redistribute.argtypes = [
        ctypes.POINTER(ctypes.c_uint16),
        ctypes.POINTER(ctypes.c_bool),
        ctypes.POINTER(ctypes.c_uint16),
        ctypes.c_int,
    ]
    lib.pool_split_redistribute.restype = None
    return lib

class FakePool(threading.Thread):
    def __init__(self, name, port):
        super().__init__(daemon=True)
        self.name = name
        self.port = port
        self.shares = []  # (job_id, nonce)
        self.jobs_issued = 0
        self._stop = False
        self.ready = threading.Event()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(8)
        self.sock.settimeout(0.3)

    def run(self):
        self.ready.set()
        while not self._stop:
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()

    def handle(self, conn):
        conn.settimeout(0.5)
        buf = b""
        job_id = f"{self.name}-job"
        try:
            while not self._stop:
                try:
                    data = conn.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if not line.strip():
                        continue
                    msg = json.loads(line.decode())
                    method = msg.get("method")
                    mid = msg.get("id")
                    if method == "mining.subscribe":
                        resp = {"id": mid, "result": [[["mining.set_difficulty", "1"], ["mining.notify", "1"]], "abcd", 4], "error": None}
                        conn.sendall((json.dumps(resp) + "\n").encode())
                    elif method == "mining.authorize":
                        conn.sendall((json.dumps({"id": mid, "result": True, "error": None}) + "\n").encode())
                        # issue a notify so the miner has a job from this pool
                        self.jobs_issued += 1
                        notify = {"id": None, "method": "mining.notify", "params": [
                            job_id, "0"*64, "00", "00", [], "20000000", "1704e5", "5c0b8e00", False
                        ]}
                        conn.sendall((json.dumps(notify) + "\n").encode())
                    elif method == "mining.submit":
                        params = msg.get("params") or []
                        self.shares.append({"job": params[1] if len(params) > 1 else "?", "nonce": params[-1] if params else "?"})
                        conn.sendall((json.dumps({"id": mid, "result": True, "error": None}) + "\n").encode())
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def stop(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass

class MinerConn:
    def __init__(self, port, pool_index):
        self.port = port
        self.pool_index = pool_index
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.sock.settimeout(1)
        self.uid = 1
        self.job_id = None
        self._subscribe()

    def send(self, obj):
        self.sock.sendall((json.dumps(obj) + "\n").encode())

    def recv_until(self, pred, tries=20):
        buf = b""
        for _ in range(tries):
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                msg = json.loads(line.decode())
                if pred(msg):
                    return msg
        return None

    def _subscribe(self):
        self.send({"id": self.uid, "method": "mining.subscribe", "params": ["Costean/2.15.4"]})
        self.uid += 1
        self.recv_until(lambda m: "result" in m)
        self.send({"id": self.uid, "method": "mining.authorize", "params": ["demo", "x"]})
        self.uid += 1
        # wait for notify
        n = self.recv_until(lambda m: m.get("method") == "mining.notify")
        if n:
            self.job_id = n["params"][0]

    def submit(self, nonce):
        if not self.job_id:
            raise RuntimeError(f"pool {self.pool_index} has no job")
        self.send({"id": self.uid, "method": "mining.submit",
                   "params": ["demo", self.job_id, "00000001", "5c0b8e00", nonce]})
        self.uid += 1
        return self.recv_until(lambda m: "result" in m and m.get("id") == self.uid - 1)

    def close(self):
        self.sock.close()

def c_array(typ, values):
    arr = (typ * len(values))(*values)
    return arr

def main():
    lib = build_lib()
    ports = [17331, 17332, 17333]
    percents = [50, 30, 20]
    pools = [FakePool(f"P{i}", ports[i]) for i in range(3)]
    for p in pools:
        p.start()
        p.ready.wait(2)

    miners = [MinerConn(ports[i], i) for i in range(3)]
    # Every miner has a job tagged with that pool.
    jobs_sent = [0, 0, 0]
    total = 0
    N = 200
    for i in range(N):
        ready = c_array(ctypes.c_bool, [True, True, True])
        pct = c_array(ctypes.c_uint16, percents)
        sent = c_array(ctypes.c_uint32, jobs_sent)
        pick = lib.pool_split_pick(pct, ready, sent, 3, total)
        if pick < 0 or pick > 2:
            raise SystemExit(f"invalid pick {pick}")
        nonce = f"{i:08x}"
        # Share is submitted only to the pool whose job produced it.
        ok = miners[pick].submit(nonce)
        if not ok or ok.get("result") is not True:
            raise SystemExit(f"share rejected on pool {pick}: {ok}")
        jobs_sent[pick] += 1
        total += 1

    time.sleep(0.2)
    counts = [len(p.shares) for p in pools]
    print("shares routed:", dict(zip(["P0","P1","P2"], counts)))
    print("jobs_sent:", jobs_sent)
    if sum(counts) != N:
        raise SystemExit(f"expected {N} shares, got {sum(counts)}")
    # Each share must carry that pool's job id — never another pool's.
    for i, p in enumerate(pools):
        for s in p.shares:
            if s["job"] != f"P{i}-job":
                raise SystemExit(f"share misrouted to {p.name}: {s}")
    # Ratios should be near 50/30/20 with slack for discrete scheduling
    expected = [int(N * x / 100) for x in percents]
    for i, exp in enumerate(expected):
        if abs(counts[i] - exp) > 8:
            raise SystemExit(f"ratio off for P{i}: {counts[i]} vs ~{exp}")

    # Failover: drop pool 0, redistribute, remaining shares go to 1 and 2 only.
    configured = c_array(ctypes.c_uint16, percents)
    connected = c_array(ctypes.c_bool, [False, True, True])
    effective = c_array(ctypes.c_uint16, [0, 0, 0])
    lib.pool_split_redistribute(configured, connected, effective, 3)
    eff = list(effective)
    print("failover effective percents:", eff)
    if eff[0] != 0 or eff[1] + eff[2] != 100 or eff[1] == 0 or eff[2] == 0:
        raise SystemExit(f"bad failover redistribution: {eff}")
    # 30:20 of 100 -> 60:40
    if not (eff[1] == 60 and eff[2] == 40):
        # allow rounding but must be proportional
        if abs(eff[1] / 30 - eff[2] / 20) > 0.15:
            raise SystemExit(f"failover not proportional: {eff}")

    extra = 40
    sent_after = [0, 0, 0]
    ready2 = c_array(ctypes.c_bool, [False, True, True])
    for i in range(extra):
        sent = c_array(ctypes.c_uint32, sent_after)
        pick = lib.pool_split_pick(effective, ready2, sent, 3, sum(sent_after))
        if pick == 0:
            raise SystemExit("downed pool was picked")
        miners[pick].submit(f"f{i:07x}")
        sent_after[pick] += 1
    time.sleep(0.2)
    # New shares on P0 should still be the original 50% set
    if len(pools[0].shares) != counts[0]:
        raise SystemExit("downed pool received a share after failover")
    print("failover jobs:", sent_after)
    print("PASS mock stratum split routing and ratios")

    for m in miners:
        m.close()
    for p in pools:
        p.stop()
    return 0

if __name__ == "__main__":
    sys.exit(main() or 0)
