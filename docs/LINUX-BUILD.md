# Building native Linux Max 9 externals

The reproducible build target is **Ubuntu 22.04 x86_64**. It builds one minimal Max external and one adaptation of the MIT-licensed `neural~`. The adaptation is applied in a temporary source checkout, never copied to the upstream repository.

## Prerequisites

```sh
sudo apt-get update
sudo apt-get install -y git build-essential gcc-12 g++-12 cmake ninja-build \
  pkg-config python3 binutils file
```

The pinned `neural~` repo fetches its pinned git submodules: Cycling '74 Min-DevKit, NeuralAudio and the dependencies of those projects. Build uses GCC 12 with C++20 and forces `SUPERBUILD=OFF` to prevent macOS/Windows builds.

```sh
CC=gcc-12 CXX=g++-12 bash scripts/build-push-m4l.sh
python3 -m unittest discover -s tests -v
```

Result: `dist/BRKNAM-Push3-M4L-linux-x86_64.tar.gz`.

Build checks:
- ELF x86_64, correct `.mxl_x86_64` extension;
- the `ext_main` entry point for the Max loader;
- glibc, GLIBCXX, CXXABI versioned symbol requirements, written to `ABI-REPORT.txt`.

**These static checks cannot confirm Max runtime compatibility.** C++ symbol resolution by the host, search-path behavior, and audio thread guarantees need a physical device.

The first build may need further toolchain adaptations. CI is the authority for whether an artifact is valid; **do not distribute failed or unverified builds as working firmware replacements**.
