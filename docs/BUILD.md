# Reproducible NAMix VST3 build for Push 3

The initial build uses upstream NAMix 0.9.0, pinned by full commit SHA in `scripts/build-linux.sh`; it **does not rebuild BRKNAM's old iPlug2 plugin**.

## Host / dependencies

Use Ubuntu 22.04 x86_64 (glibc 2.35). On Ubuntu 22.04:

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build pkg-config \
  libcairo2-dev libfreetype6-dev libfontconfig1-dev libx11-dev \
  libjack-jackd2-dev binutils gcc-12 g++-12
```

CMake >= 3.25, GCC 12+ (GCC 11's libstdc++ cannot compile std::atomic<std::shared_ptr>) and network access to clone NAMix and its submodules are required. For Apple Silicon use an Ubuntu 22.04 `linux/amd64` container or download the GitHub Actions artifact; macOS binaries cannot be loaded on Push.

```bash
./scripts/build-linux.sh
```

The build produces `dist/BRKNAM-Push3-NAMix-0.9.0-linux-x86_64.tar.gz`, with `NAMix.vst3`, bundled upstream notices and `BUILD-INFO.txt`. It checks the x86_64 Linux binary and rejects dependencies on GLIBC versions later than 2.35. A successful check does **not** prove that every shared library, libstdc++ version or runtime dependency is present on Push.

GitHub Actions also builds on `ubuntu-22.04`. Keep source and submodule SHAs pinned; update only with a tested commit and notice review.

## Test on the build host

```bash
bash -n scripts/build-linux.sh
python3 -m unittest discover -s tests -v
tar -tzf dist/BRKNAM-Push3-NAMix-0.9.0-linux-x86_64.tar.gz | head
```

Never distribute NAM captures or commercial IRs without permission. The binary is an experimental build of the original **NAMix**; its VST3 class identifier and product name remain unchanged.
