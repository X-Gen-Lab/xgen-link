"""Run two separately linked XGL SDKs against a deterministic byte stream."""

import argparse
import queue
import subprocess
import sys
import threading
import time
from contextlib import ExitStack, suppress
from pathlib import Path


# SDK 3 Boot HELLO captures and their RESET variants with the SDK 3 body CRC.
BOOT_HELLO = tuple(map(bytes.fromhex, (
    "a55a0329032208070100020053000000000000000000e1b508010e010c1d0000000000000000000000e7f6",
    "a55a0329032208070200010053000000000000000000a24708010e010c1d000000000000000000000020f7",
)))
BOOT_RESET = tuple(map(bytes.fromhex, (
    "a55a0329032208070100020053000000000000000000e1b508010f010c1d000000000000000000000026f6",
    "a55a0329032208070200010053000000000000000000a24708010f010c1d0000000000000000000000e1f7",
)))


class Peer:
    def __init__(self, executable, node, profile, stack, timeout, record):
        self.node = node
        self.timeout = timeout
        self.record = record
        self.process = subprocess.Popen(
            [executable, str(node), profile],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="ascii",
            errors="backslashreplace",
        )
        stack.callback(self.stop)
        self.delivered = []
        self.replies = queue.Queue(maxsize=64)
        threading.Thread(target=self.read_replies, daemon=True).start()

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=self.timeout)
        with suppress(BrokenPipeError):
            self.process.stdin.close()

    def read_replies(self):
        # The reader owns stdout, so a stuck child cannot block the controller.
        with self.process.stdout as stream:
            while line := stream.readline(8192):
                self.replies.put(line)
        self.replies.put(None)

    def reply(self, command, deadline):
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise queue.Empty
            line = self.replies.get(timeout=remaining)
        except queue.Empty:
            raise RuntimeError(f"peer {self.node}: timed out during {command!r}") from None
        if line is not None:
            self.record(f"peer {self.node} < {line.rstrip()}")
            if not line.endswith("\n"):
                raise RuntimeError(f"peer {self.node}: incomplete or oversized reply")
        return line

    def finish(self):
        self.process.stdin.close()
        deadline = time.monotonic() + self.timeout
        if self.reply("exit", deadline) is not None:
            raise RuntimeError(f"peer {self.node}: unexpected output after EOF")
        remaining = deadline - time.monotonic()
        status = self.process.wait(timeout=max(remaining, 0.0))
        self.record(f"peer {self.node} exit {status}")
        if status != 0:
            raise RuntimeError(f"peer {self.node}: exited with status {status}")

    def command(self, command):
        self.record(f"peer {self.node} > {command}")
        deadline = time.monotonic() + self.timeout
        try:
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
        except BrokenPipeError:
            raise RuntimeError(f"peer {self.node}: closed stdin during {command!r}") from None
        frames = []
        while (line := self.reply(command, deadline)) is not None:
            fields = line.split()
            if len(fields) != 2:
                raise RuntimeError(f"peer {self.node}: malformed reply {line.rstrip()!r}")
            event, value = fields
            if event == "end":
                if value != "0":
                    raise RuntimeError(f"peer {self.node}: {command!r} returned XGL error {value}")
                return frames
            if event not in ("rx", "tx"):
                raise RuntimeError(f"peer {self.node}: unexpected event {event!r}")
            try:
                payload = bytes.fromhex(value)
            except ValueError:
                raise RuntimeError(f"peer {self.node}: invalid {event} byte string") from None
            if event == "rx":
                self.delivered.append(payload)
            elif len(payload) >= 26 and payload[:3] == b"\xa5\x5a\x03":
                frames.append(payload)
            else:
                raise RuntimeError(f"peer {self.node}: invalid wire v3 frame {value}")
        raise RuntimeError(f"peer {self.node}: exited during {command!r}")


def transfer(peer, frames):
    for frame in frames:
        peer.command(f"put {frame.hex()}")


def extensions(frame):
    header_size = frame[3]
    offset = 24
    if not offset <= header_size <= len(frame) - 2:
        raise RuntimeError(f"Invalid v3 header size in {frame.hex()}")
    while offset < header_size:
        if offset + 2 > header_size:
            raise RuntimeError(f"Truncated v3 TLV in {frame.hex()}")
        end = offset + 2 + frame[offset + 1]
        if end > header_size:
            raise RuntimeError(f"Truncated v3 TLV value in {frame.hex()}")
        yield frame[offset], frame[offset + 2 : end]
        offset = end


def scenario(first, second, profile, timeout, record):
    with ExitStack() as stack:
        a = Peer(first, 1, profile, stack, timeout, record)
        b = Peer(second, 2, profile, stack, timeout, record)
        now = 0
        for sender, receiver, node in ((a, b, 1), (b, a, 2)):
            size = 240 if profile == "full" else 12
            frames = sender.command(f"send {size} {now}")
            hello = [frame for frame in frames if frame[4] == 3]
            if len(hello) != 1 or (8, b"\x0e") not in extensions(hello[0]):
                raise RuntimeError("Expected a scoped HELLO")
            if profile == "boot" and hello[0] != BOOT_HELLO[node - 1]:
                raise RuntimeError("Boot HELLO differs from the SDK 3 wire vector")
            data = [i for i, frame in enumerate(frames) if frame[4] == 1]
            if profile == "full":
                if len(data) < 2 or not all(frames[i][5] & 0x10 for i in data):
                    raise RuntimeError("Expected authenticated fragments")
                i, j = data[:2]
                frames[i], frames[j] = frames[j], frames[i]
                record(f"link: reordered fragments from peer {sender.node}")
            transfer(receiver, frames)

            dropped = False
            sack_seen = False
            retry_seen = False
            for _ in range(50):
                now += 100
                outgoing = receiver.command(f"step {now}")
                sack_seen |= any(
                    kind == 3 for frame in outgoing for kind, _ in extensions(frame)
                )
                if profile == "boot" and not dropped:
                    ack = next(
                        (i for i, frame in enumerate(outgoing) if frame[4] == 2),
                        None,
                    )
                    if ack is not None:
                        outgoing.pop(ack)
                        record(f"link: dropped first ACK from peer {receiver.node}")
                        dropped = True
                transfer(sender, outgoing)
                outgoing = sender.command(f"step {now}")
                retry_seen |= any(frame[4] == 1 for frame in outgoing)
                transfer(receiver, outgoing)

            expected = bytes((i ^ node ^ 0xA5) & 0xFF for i in range(size))
            if receiver.delivered != [expected]:
                raise RuntimeError(f"{profile}: unexpected deliveries {receiver.delivered}")
            if (profile == "full" and not sack_seen) or (
                profile == "boot" and not (dropped and retry_seen)
            ):
                raise RuntimeError(f"{profile}: recovery branch did not execute")

        if profile == "boot":
            for receiver, reset in zip((b, a), BOOT_RESET):
                transfer(receiver, [reset, reset])
                # Boot polls only one small PHY chunk per step.
                for _ in range(8):
                    now += 100
                    if receiver.command(f"step {now}"):
                        raise RuntimeError("RESET emitted unexpected traffic")
                if receiver.command(f"reject send 12 {now}"):
                    raise RuntimeError("A reset epoch must not emit DATA")

        a.finish()
        b.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", help="First SDK's interop_peer executable")
    parser.add_argument("second", help="Second SDK's interop_peer executable")
    parser.add_argument("--profile", choices=("boot", "full"), required=True)
    parser.add_argument("--timeout-seconds", type=float, default=10.0,
                        help="Wall-clock limit for each command or peer exit (default: 10)")
    parser.add_argument("--transcript", type=Path,
                        help="Write commands, replies and deliberate link changes to this file")
    args = parser.parse_args()
    if not 0 < args.timeout_seconds <= threading.TIMEOUT_MAX:
        parser.error("--timeout-seconds must be positive and within the platform wait limit")
    with ExitStack() as stack:
        log = (stack.enter_context(args.transcript.open("w", encoding="utf-8", newline="\n"))
               if args.transcript is not None else None)

        def record(message):
            if log is not None:
                print(message, file=log, flush=True)

        record(f"profile {args.profile}")
        try:
            scenario(args.first, args.second, args.profile, args.timeout_seconds, record)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            record(f"FAIL {error}")
            print(f"{args.profile}: {error}", file=sys.stderr)
            return 1
        record("PASS")
    print(f"{args.profile}: two-way v3 interop passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
