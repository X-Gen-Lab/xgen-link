"""Run two XGL Wire v1 endpoints against a deterministic byte stream."""

import argparse
import queue
import subprocess
import sys
import threading
import time
from contextlib import ExitStack, suppress
from pathlib import Path


WIRE_BASE_HEADER_SIZE = 24
WIRE_PREAMBLE = b"\xa5\x5a\x01"
PACKET_DATA = 1
PACKET_ACK = 2
PACKET_CONTROL = 3
EXT_ACK_RANGE = 2
EXT_SACK = 3
EXT_DATA_TYPE = 8
FLAG_AUTHENTICATED = 0x10
CONTROL_HELLO = b"\x0e"
CONTROL_RESET = b"\x0f"


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
            elif (len(payload) >= WIRE_BASE_HEADER_SIZE + 2 and
                  payload.startswith(WIRE_PREAMBLE)):
                frames.append(payload)
            else:
                raise RuntimeError(f"peer {self.node}: invalid Wire v1 frame {value}")
        raise RuntimeError(f"peer {self.node}: exited during {command!r}")


def transfer(peer, frames):
    for frame in frames:
        peer.command(f"put {frame.hex()}")


def extensions(frame):
    header_size = frame[3]
    offset = WIRE_BASE_HEADER_SIZE
    if not offset <= header_size <= len(frame) - 2:
        raise RuntimeError(f"Invalid Wire v1 header size in {frame.hex()}")
    while offset < header_size:
        if offset + 2 > header_size:
            raise RuntimeError(f"Truncated Wire v1 TLV in {frame.hex()}")
        end = offset + 2 + frame[offset + 1]
        if end > header_size:
            raise RuntimeError(f"Truncated Wire v1 TLV value in {frame.hex()}")
        yield frame[offset], frame[offset + 2 : end]
        offset = end


def crc16(data):
    value = 0xFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value.to_bytes(2, "little")


def refresh_checksums(frame):
    header_crc = slice(WIRE_BASE_HEADER_SIZE - 2, WIRE_BASE_HEADER_SIZE)
    frame[header_crc] = b"\0\0"
    frame[header_crc] = crc16(frame[:WIRE_BASE_HEADER_SIZE])
    frame[-2:] = crc16(frame[:-2])
    return bytes(frame)


def reset_from_hello(hello):
    reset = bytearray(hello)
    offset = WIRE_BASE_HEADER_SIZE
    for kind, value in extensions(hello):
        if kind == EXT_DATA_TYPE and value == CONTROL_HELLO:
            reset[offset + 2 : offset + 3] = CONTROL_RESET
            return refresh_checksums(reset)
        offset += 2 + len(value)
    raise RuntimeError("Expected an operation TLV in HELLO")


def reject_old_wire_version(receiver, data, now, record):
    old = bytearray(data)
    old[2] = 3
    record(f"link: sent checksum-consistent Wire v3 DATA to peer {receiver.node}")
    transfer(receiver, [refresh_checksums(old)])
    previous_deliveries = len(receiver.delivered)
    for _ in range(8):
        now += 1
        outgoing = receiver.command(f"step {now}")
        if outgoing or len(receiver.delivered) != previous_deliveries:
            raise RuntimeError("Wire v3 DATA was accepted or acknowledged")
    return now


def scenario(first, second, profile, timeout, record):
    with ExitStack() as stack:
        a = Peer(first, 1, profile, stack, timeout, record)
        b = Peer(second, 2, profile, stack, timeout, record)
        now = 0
        resets = []
        for sender, receiver, node in ((a, b, 1), (b, a, 2)):
            sizes = (12, 19) if profile == "full" else (12,)
            frames = []
            for size in sizes:
                frames.extend(sender.command(f"send {size} {now}"))
            hello = [frame for frame in frames if frame[4] == PACKET_CONTROL]
            if (len(hello) != 1 or
                    (EXT_DATA_TYPE, CONTROL_HELLO) not in extensions(hello[0])):
                raise RuntimeError("Expected one HELLO per sending direction")
            data = [frame for frame in frames if frame[4] == PACKET_DATA]
            lengths = tuple(int.from_bytes(frame[20:22], "little") for frame in data)
            if len(frames) != len(sizes) + 1 or lengths != sizes:
                raise RuntimeError("Expected one DATA frame per application submission")
            if profile == "full":
                if not all(frame[5] & FLAG_AUTHENTICATED for frame in frames):
                    raise RuntimeError("Expected authenticated HELLO and DATA")
                data.reverse()
                record(f"link: reordered DATA frames from peer {sender.node}")
            else:
                resets.append((receiver, reset_from_hello(hello[0])))
                if node == 1:
                    now = reject_old_wire_version(receiver, data[0], now, record)
            transfer(receiver, hello + data)

            dropped = False
            ack_seen = False
            sack_seen = False
            retry_seen = False
            for _ in range(50):
                now += 100
                outgoing = receiver.command(f"step {now}")
                feedback = [frame for frame in outgoing if frame[4] == PACKET_ACK]
                kinds = {kind for frame in feedback for kind, _ in extensions(frame)}
                ack_seen |= EXT_ACK_RANGE in kinds
                sack_seen |= EXT_SACK in kinds
                if profile == "full" and any(
                    not frame[5] & FLAG_AUTHENTICATED for frame in feedback
                ):
                    raise RuntimeError("Expected authenticated feedback")
                if profile == "boot" and not dropped:
                    ack = next(
                        (i for i, frame in enumerate(outgoing) if frame[4] == PACKET_ACK),
                        None,
                    )
                    if ack is not None:
                        outgoing.pop(ack)
                        record(f"link: dropped first ACK from peer {receiver.node}")
                        dropped = True
                transfer(sender, outgoing)
                outgoing = sender.command(f"step {now}")
                retry_seen |= any(frame[4] == PACKET_DATA for frame in outgoing)
                transfer(receiver, outgoing)

            expected = [bytes((i ^ node ^ 0xA5) & 0xFF for i in range(size))
                        for size in sizes]
            if receiver.delivered != expected:
                raise RuntimeError(f"{profile}: unexpected deliveries {receiver.delivered}")
            recovery_seen = sack_seen if profile == "full" else dropped and retry_seen
            if not ack_seen or not recovery_seen:
                raise RuntimeError(f"{profile}: recovery branch did not execute")

        for receiver, reset in resets:
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
    parser.add_argument("first", help="First xgl_interop_peer executable")
    parser.add_argument("second", help="Second xgl_interop_peer executable")
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
    print(f"{args.profile}: two-way Wire v1 interop passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
