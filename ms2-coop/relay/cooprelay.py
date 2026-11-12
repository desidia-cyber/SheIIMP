#!/usr/bin/env python3
"""Mortal Shell II co-op relay.

The game-side Lua mod cannot open sockets, so it talks to this helper through a
state directory:

    <dir>/out_<player>.txt     mod -> relay   (one JSON object per line, rewritten every tick)
    <dir>/peer_<player>.txt    relay -> mod   (one JSON object per line per peer, rewritten)
    <dir>/relay_<player>.log   relay log

Round 7: the mod cuts one publish into several self-contained parts, each with
`{"v":2,"p":<i>,"n":<N>}` and its own share of the top-level members (part 0 is
the transform/appearance block).  Each part travels as its OWN datagram, and on
the receiving side the parts of the newest sequence are written back to the peer
file in order, so a lost part only costs the keys it carried -- the hot part is
never behind a slow one.  A line without "p" (an old mod, or WIRE_SPLIT_OFF.txt)
is sent as one datagram exactly as before.

Transports:
    --transport file    local only: bridge two state dirs (test stand-in)
    --transport udp     host/client over UDP (LAN, or two copies on one PC)
    --transport steam   Steam AppID 480 lobby + ISteamNetworkingMessages (friends)

Usage:
    # host on a LAN / same PC, port 47010
    python cooprelay.py --transport udp --role host --port 47010 --player A --dir ..\\coop
    python cooprelay.py --transport udp --role client --host 127.0.0.1 --port 47010 --player B --dir ..\\coop
"""

import argparse
import json
import os
import random
import socket
import sys
import threading
import time

TICK = 1.0 / 30.0
# every datagram this relay sends stays under this; the mod's own budget is 1150
MAX_DGRAM = 1200


def now_ms():
    return int(time.monotonic() * 1000)


class Log:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()

    def __call__(self, msg):
        line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
        print(line, flush=True)
        with self.lock:
            try:
                with open(self.path, "a", encoding="utf-8") as f:
                    f.write(line + "\n")
            except OSError:
                pass


class StateDir:
    """The mod's contract: the latest publish each way, one JSON object per line."""

    def __init__(self, path, player):
        self.path = path
        self.player = player
        os.makedirs(path, exist_ok=True)
        self.out_path = os.path.join(path, "out_%s.txt" % player)
        self.peer_path_cache = {}
        self._last_out = None
        self._out_mtime = None
        self._last_peer = {}

    def read_out(self):
        """Every line of out_<player>.txt, or None when it has not changed.

        Round 7: the mod writes one line per packet part, so this returns a list.
        """
        try:
            st = os.stat(self.out_path)
            if st.st_mtime == self._out_mtime:
                return None
            with open(self.out_path, "r", encoding="utf-8") as f:
                text = f.read().strip()
            self._out_mtime = st.st_mtime
            if not text or text == self._last_out:
                return None
            self._last_out = text
            return [ln for ln in text.split("\n") if ln.strip()]
        except (OSError, ValueError):
            return None

    def write_peer(self, peer_id, lines):
        """Write the parts of the newest sequence, in order, one per line.

        The whole file is replaced atomically, so the mod never reads a torn set;
        a part that did not arrive is simply not in it this time.
        """
        if isinstance(lines, str):     # legacy single-line call
            lines = [lines]
        if not lines:
            return
        text = "\n".join(lines) + "\n"
        if self._last_peer.get(peer_id) == text:
            return
        self._last_peer[peer_id] = text
        path = os.path.join(self.path, "peer_%s.txt" % peer_id)
        tmp = path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(text)
            os.replace(tmp, path)
        except OSError:
            pass


class Parts:
    """Reassembles the round-7 split on the receiving side.

    One publish = N datagrams, each carrying `{"v":2,"p":i,"n":N}` and its own
    slice of the top-level members.  The parts of the newest sequence are handed
    back in order; the previous sequence stays until a new one starts, which is
    the "latest value wins" behaviour the file contract always had.
    """

    def __init__(self, log, label=""):
        self.log = log
        self.label = label
        self.by = {}            # (peer, seq) -> {p: line}
        self.count = {}         # (peer, seq) -> n
        self.done = set()       # sequences already accounted for in merged/lost
        self.rx = 0             # datagrams that were a part
        self.lost = 0           # parts that never arrived (counted from "n")
        self.merged = 0         # sequences handed to the mod
        self.legacy = 0

    def add(self, peer, msg, line):
        """Store one datagram; return the lines to write, or None."""
        p = msg.get("p")
        if p is None:
            self.legacy += 1
            return [line]       # pre-round-7 datagram: pass it straight through
        seq = msg.get("sq")
        n = msg.get("n")
        key = (peer, seq)
        d = self.by.get(key)
        if d is None:
            d = {}
            self.by[key] = d
            self.count[key] = n
        if p not in d:
            d[p] = line
            self.rx += 1
        return None

    def flush(self, peer):
        """Lines for the newest sequence of `peer`, counting what is missing.

        A sequence is accounted for once: when every part is in, or when a newer
        one has started (its missing parts then count as lost).  Older sequences
        are dropped here, after they have been accounted for.
        """
        best = None
        for (pp, seq) in list(self.by):
            if pp != peer or not isinstance(seq, int):
                continue
            if best is None or seq > best:
                best = seq
        for (pp, seq), d in list(self.by.items()):
            if pp != peer:
                continue
            n = self.count.get((pp, seq)) or 0
            complete = isinstance(n, int) and n > 0 and len(d) >= n
            if (complete or seq != best) and (pp, seq) not in self.done:
                self.done.add((pp, seq))
                if isinstance(n, int) and n > len(d):
                    self.lost += n - len(d)
                self.merged += 1
            if best is not None and isinstance(seq, int) and seq < best:
                del self.by[(pp, seq)]
                self.count.pop((pp, seq), None)
                self.done.discard((pp, seq))
        if best is None:
            return None
        d = self.by.get((peer, best)) or {}
        return [d[i] for i in sorted(d)]


def stats_line(tag, sent, rx, max_sent, parts_out, losses, extra):
    return ("wire: %s sent=%d recv=%d max=%d parts=%d dropped=%d %s"
            % (tag, sent, rx, max_sent, parts_out, losses, extra))


# ---------------------------------------------------------------------------
# udp transport
# ---------------------------------------------------------------------------
class UdpLink:
    def __init__(self, role, host, port, log, loss=0.0):
        self.role = role
        self.log = log
        self.loss = loss
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # the host owns the fixed port; a client must take an ephemeral one or two
        # copies on one PC fight over the same UDP port (that cost a test round)
        bind_port = port if role == "host" else 0
        self.sock.bind(("0.0.0.0", bind_port))
        self.sock.settimeout(0.2)
        self.host_addr = None
        self.client_addr = None
        # wire counters (run-two.ps1's WIRE verdict reads the "wire:" log lines)
        self.sent = 0
        self.max_sent = 0
        self.dropped = 0
        self.oversize = 0
        if role == "host":
            self.log("udp host on 0.0.0.0:%d" % port)
        else:
            self.sock.sendto(b'{"t":"hello"}', (host, port))
            self.host_addr = (host, port)
            self.log("udp client -> %s:%d" % (host, port))

    def poll(self, outbox):
        """Send every pending line as its own datagram; return the received lines.

        Round 7: `outbox` is the list of parts of the newest publish, so one
        datagram per part and a lost one only loses that part.  --loss drops
        outgoing datagrams at that percentage (the lossy-transport test).
        """
        if outbox:
            if isinstance(outbox, str):
                outbox = [outbox]
            for line in outbox:
                payload = line.encode("utf-8")
                if len(payload) > MAX_DGRAM:
                    self.oversize += 1
                if self.loss > 0.0 and random.random() * 100.0 < self.loss:
                    self.dropped += 1
                    continue
                self.sent += 1
                if len(payload) > self.max_sent:
                    self.max_sent = len(payload)
                if self.role == "host":
                    if self.client_addr:
                        try:
                            self.sock.sendto(payload, self.client_addr)
                        except OSError:
                            pass
                else:
                    try:
                        self.sock.sendto(payload, self.host_addr)
                    except OSError:
                        pass
        received = []
        while True:
            try:
                data, addr = self.sock.recvfrom(65535)
            except socket.timeout:
                break
            except OSError:
                break
            if self.role == "host":
                if self.client_addr != addr:
                    self.client_addr = addr
                    self.log("client joined from %s:%d" % addr)
            else:
                self.host_addr = addr
            try:
                received.append(data.decode("utf-8", "replace").strip())
            except Exception:
                pass
        return received

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# file transport (no sockets): two state dirs bridged by this process
# ---------------------------------------------------------------------------
class FileLink:
    def __init__(self, peer_out, log):
        self.peer_out = peer_out
        self.log = log
        self._mtime = None
        self.log("file bridge from %s" % peer_out)

    def poll(self, outbox):
        received = []
        try:
            st = os.stat(self.peer_out)
            if st.st_mtime != self._mtime:
                self._mtime = st.st_mtime
                with open(self.peer_out, "r", encoding="utf-8") as f:
                    text = f.read().strip()
                received = [ln for ln in text.split("\n") if ln.strip()]
        except OSError:
            pass
        return received

    def close(self):
        pass


def run_link(link, state, log, players, loss=0.0):
    log("running; player=%s dir=%s" % (state.player, state.path))
    last_peer_tick = now_ms()
    last_stats = now_ms()
    parts = Parts(log)
    torn = 0            # a packet read while the mod was rewriting the file
    torn_logged = 0
    while True:
        out = state.read_out()
        touched = []
        for line in link.poll(out):
            try:
                msg = json.loads(line)
            except ValueError:
                # A packet part caught mid-write is not an error: the mod rewrites
                # out_<id>.txt every tick and the next tick replaces it.  Anything
                # else is a broken state line and must be reported (a stray quote
                # in the mod's format string cost a test round).
                if line.startswith('{"t":"p"'):
                    torn += 1
                    if torn_logged < 3:
                        torn_logged += 1
                        log("torn part (%d bytes), skipped" % len(line))
                    continue
                log("bad json from mod (%d bytes): %r" % (len(line), line[:160]))
                continue
            if msg.get("t") == "hello":
                continue
            peer = msg.get("id") or "peer"
            if peer == state.player:
                continue
            got = parts.add(peer, msg, line)
            if got:
                state.write_peer(peer, got)
            if peer not in touched:
                touched.append(peer)
            last_peer_tick = now_ms()
        # one write per poll: the whole set of parts that arrived for the newest
        # sequence, so the mod never sees a half-updated file
        for peer in touched:
            lines = parts.flush(peer)
            if lines:
                state.write_peer(peer, lines)
        if now_ms() - last_peer_tick > 3000:
            log("no peer traffic for 3 s")
            last_peer_tick = now_ms()
        if now_ms() - last_stats >= 5000:
            last_stats = now_ms()
            sent = getattr(link, "sent", 0)
            dropped = getattr(link, "dropped", 0)
            maxs = getattr(link, "max_sent", 0)
            over = getattr(link, "oversize", 0)
            log(stats_line("tx", sent, 0, maxs, parts.merged, dropped,
                           "parts_rx=%d parts_lost=%d legacy=%d oversize=%d torn=%d loss=%g"
                           % (parts.rx, parts.lost, parts.legacy, over, torn, loss)))
        time.sleep(TICK)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--transport", choices=["file", "udp", "steam"], default="udp")
    ap.add_argument("--role", choices=["host", "client"], default="host")
    ap.add_argument("--port", type=int, default=47010)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--player", default="A")
    ap.add_argument("--dir", default=".")
    ap.add_argument("--peer-dir", default=None, help="file transport: the other state dir")
    ap.add_argument("--lobby", default=None, help="steam: lobby id to join")
    ap.add_argument("--game", default=None, help="steam: game install dir (for its steam_api64.dll)")
    ap.add_argument("--loss", type=float, default=0.0,
                    help="udp: drop this percentage of outgoing datagrams (loss test)")
    args = ap.parse_args(argv)

    log = Log(os.path.join(args.dir, "relay_%s.log" % args.player))
    state = StateDir(args.dir, args.player)

    if args.transport == "udp":
        link = UdpLink(args.role, args.host, args.port, log, loss=args.loss)
        if args.loss > 0.0:
            log("LOSSY: dropping %.1f%% of outgoing datagrams" % args.loss)
    elif args.transport == "file":
        if not args.peer_dir:
            ap.error("--transport file needs --peer-dir")
        link = FileLink(os.path.join(args.peer_dir, "out_%s.txt" % ("B" if args.player == "A" else "A")), log)
    else:  # steam
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from steamlink import SteamLink

        try:
            sl = SteamLink(args.role, args.player, args.lobby, args.game, log)
            sl.establish()
        except Exception as exc:  # a failure here must be loud and specific
            log("STEAM UNAVAILABLE: %s" % exc)
            return 2

        class SteamAdapter:
            def __init__(self, link):
                self.link = link

            def poll(self, outbox):
                if outbox:
                    if isinstance(outbox, str):
                        outbox = [outbox]
                    for line in outbox:
                        self.link.send(line)
                self.link._pump()
                lines = []
                for line in self.link.receive():
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        continue
                    if msg.get("id") == self.link.player:
                        continue
                    lines.append(line)
                return lines

            def close(self):
                self.link.close()

        link = SteamAdapter(sl)

    try:
        run_link(link, state, log, None, loss=args.loss)
    except KeyboardInterrupt:
        log("stopped")
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
