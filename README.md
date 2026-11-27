# buddy

[![CI](https://github.com/aneesh-pradhan/buddy/actions/workflows/ci.yml/badge.svg)](https://github.com/aneesh-pradhan/buddy/actions/workflows/ci.yml)

Terminal UI for the [CMF Buds Pro 2](https://nothing.tech/). It speaks the buds' Bluetooth RFCOMM control protocol, so noise cancelling, Ultra Bass, and battery status can be changed from a laptop instead of the Nothing X phone app.

The header dial works like the case's Smart Dial. Scroll, drag, or `-`/`+` changes the Bluetooth sink volume through `pactl` (PipeWire or PulseAudio). Click the dial, or press `p`, to play or pause the active MPRIS player through `busctl`.

## Prerequisites

- Linux. Pairing uses BlueZ (`bluetoothctl`). Volume and playback use `pactl` and `busctl` (systemd). The UI itself builds and runs without those commands.
- CMake **3.30** or newer, and Ninja. Ubuntu 24.04 ships CMake 3.28, which cannot pass `-std=c++26` to GCC.
- `git`. Configure clones [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v6.1.9.
- One of these compiler and standard-library pairs. The build rejects anything older.

  | Compiler | Standard library |
  | --- | --- |
  | GCC 14 or newer | libstdc++ that ships with that GCC (recommended) |
  | Clang 18 or newer | libstdc++ from GCC 13 or newer |
  | Clang 18 or newer | libc++ from LLVM 20 or newer |

  libc++ 18 and 19 only declare `std::jthread` with `-fexperimental-library`. This project does not pass that flag. Clang 17 accepts `-std=c++26` but does not compile the libstdc++ headers from GCC 14, which is the library Clang picks on current Ubuntu once the GCC 14 runtime is installed.

## Build

Verified from a clean clone on Ubuntu 24.04. If `g++ --version` is already 14 or newer and `cmake --version` is already 3.30 or newer, skip the `g++-14` package and the CMake tarball, and pass `g++` instead of `g++-14`.

```bash
sudo apt update
sudo apt install -y g++-14 ninja-build ca-certificates curl tar git
curl -fsSL -o /tmp/cmake-3.30.9-linux-x86_64.tar.gz \
  https://github.com/Kitware/CMake/releases/download/v3.30.9/cmake-3.30.9-linux-x86_64.tar.gz
sudo tar -C /usr/local --strip-components=1 -xzf /tmp/cmake-3.30.9-linux-x86_64.tar.gz

git clone https://github.com/aneesh-pradhan/buddy.git
cd buddy
CXX=g++-14 cmake --preset default
cmake --build --preset default
ctest --preset default
./build/test_protocol
```

`cmake --preset default` configures Ninja, RelWithDebInfo, C++26, and libstdc++. It does not force a compiler, libc++, or lld. On Ubuntu 24.04, `CXX=g++-14` matters because the default `g++` is GCC 13.

`ctest` runs the protocol tests. The test binary prints:

```
all protocol tests passed
```

libc++ 20 or newer is the other supported library. On Ubuntu 24.04:

```bash
sudo apt install -y clang-20 libc++-20-dev libc++abi-20-dev libunwind-20-dev
CXX=clang++-20 cmake --preset libc++
cmake --build --preset libc++
ctest --preset libc++
```

That binary is `build-libcxx/buddy`. The default preset's binary is `build/buddy`.

## Run

```bash
./build/buddy --help
```

```
usage: ./build/buddy [--address MAC] [--channel N]

  --address MAC  Bluetooth MAC (default: auto-detect paired buds)
  --channel N    RFCOMM channel (default: 15)
```

With no paired CMF Buds Pro 2, it exits like this:

```bash
./build/buddy
```

```
no paired 'CMF Buds Pro 2' found; pair them first or pass --address
```

After pairing with `bluetoothctl`:

```bash
./build/buddy
./build/buddy --address AA:BB:CC:DD:EE:FF --channel 15
```

The UI is full screen: noise-cancelling mode, Ultra Bass, battery for each bud and the case, and the volume dial. `-`/`+` change volume, `p` toggles play/pause, `r` refreshes, `c` reconnects, and `q` quits.

## License

MIT. See [LICENSE](LICENSE).
