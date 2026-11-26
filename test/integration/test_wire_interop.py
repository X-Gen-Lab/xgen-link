"""Run two separately linked XGL SDKs against a deterministic byte stream."""

import argparse
import subprocess
from contextlib import ExitStack


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
    def __init__(self, executable, node, profile, stack):
        self.process = stack.enter_context(
            subprocess.Popen(
                [executable, str(node), profile],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            )
        )
        stack.callback(self.process.kill)
        self.delivered = []

    def command(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        frames = []
        while line := self.process.stdout.readline():
            event, value = line.split()
            if event == "end":
                if int(value) != 0:
                    raise RuntimeError(f"{command}: peer returned XGL error {value}")
                return frames
            if event == "rx":
                self.delivered.append(bytes.fromhex(value))
            elif event == "tx":
                frames.append(bytes.fromhex(value))
            else:
                raise RuntimeError(f"Unexpected peer event {line}")
        raise RuntimeError(f"Peer exited while running {command}")


def transfer(peer, frames):
    for frame in frames:
        if frame[:3] != bytes.fromhex("a55a03"):
            raise RuntimeError(f"Invalid v3 header in {frame.hex()}")
        peer.command(f"put {frame.hex()}")


def extensions(frame):
    offset = 24
    while offset < frame[3]:
        end = offset + 2 + frame[offset + 1]
        yield frame[offset], frame[offset + 2 : end]
        offset = end


def scenario(first, second, profile):
    with ExitStack() as stack:
        a = Peer(first, 1, profile, stack)
        b = Peer(second, 2, profile, stack)
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", help="First SDK's interop_peer executable")
    parser.add_argument("second", help="Second SDK's interop_peer executable")
    parser.add_argument("--profile", choices=("boot", "full"), required=True)
    args = parser.parse_args()
    scenario(args.first, args.second, args.profile)
    print(f"{args.profile}: two-way v3 interop passed")


if __name__ == "__main__":
    main()
