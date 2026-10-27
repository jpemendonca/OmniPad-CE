#!/bin/sh
# OmniPad CE — set up the same toolchain as the Dockerfile inside a WSL/Ubuntu distro (run as root)
set -e

apt-get install -y -qq clang-18 lld-18 make unzip curl >/dev/null

ln -sf /usr/bin/clang-18 /usr/local/bin/clang
ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++
ln -sf /usr/bin/ld.lld-18 /usr/local/bin/ld.lld
if ! command -v llvm-config >/dev/null; then
    printf '#!/bin/sh\ncase "$1" in --bindir) echo /usr/lib/llvm-18/bin;; --version) echo 18.1.8;; *) exit 1;; esac\n' > /usr/local/bin/llvm-config
    chmod +x /usr/local/bin/llvm-config
fi

SDK=/opt/ps5-payload-sdk
if [ ! -d "$SDK/toolchain" ]; then
    tmp=$(mktemp -d)
    curl -sSL https://github.com/ps5-payload-dev/sdk/releases/download/v0.43/ps5-payload-sdk.zip -o "$tmp/sdk.zip"
    unzip -q "$tmp/sdk.zip" -d "$tmp"
    mv "$tmp/ps5-payload-sdk" "$SDK"
fi
echo "[+] toolchain ready, SDK at $SDK"
