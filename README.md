# buddy

Helper application for the CMF Buds Pro 2 (and possibly other Nothing/CMF TWS)

Currently supports bass tuning, toggling ANC modes, and a live volume display. The dial in the header works like the case's Smart Dial: turn it (scroll, drag, or `-`/`+`) to change volume, click it (or `p`) to play/pause. Volume goes through `pactl` and play/pause through MPRIS via `busctl`. I plan on adding more features down the line. 

Primarily built this as a way to control my buds from my laptop without being tethered to the Nothing X mobile app. 

## Building

Needs CMake 3.24+, Ninja, and a C++26 compiler. The default preset uses clang++ with libc++ and lld. [FTXUI](https://github.com/ArthurSonzogni/FTXUI) is fetched at configure time if it isn't installed.

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default   # protocol tests
```

## Running

Pair the buds with `bluetoothctl` first, then:

```bash
./build/buddy
```

It finds the paired "CMF Buds Pro 2" on its own and talks to them over RFCOMM. To pick a device or channel yourself:

```bash
./build/buddy --address AA:BB:CC:DD:EE:FF --channel N
```

Volume and play/pause need `pactl` (PulseAudio or PipeWire) and `busctl` (systemd).

License is MIT.
