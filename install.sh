#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build/Release"

# Format: "name:description:binary:type:deps"
#   type = "service" -> systemd service; "bin" -> install binary only
APPS=(
    "v2_main:V2 Engine Main App:v2_main:service:"
    "v2_cli:V2 Engine CLI:v2_cli:bin:"
    "v2_tui:V2 Engine TUI:v2_tui:bin:"
)

# ============================================
# Usage
# ============================================
usage() {
    echo "Usage: $0 [app...]"
    echo ""
    echo "Apps:"
    for app in "${APPS[@]}"; do
        IFS=':' read -r name desc _ _ _ <<< "$app"
        printf "  %-12s %s\n" "$name" "$desc"
    done
    echo ""
    echo "Examples:"
    echo "  $0            # Install all apps"
    echo "  $0 v2_main    # Install only main service"
    exit 1
}

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    usage
fi

# ============================================
# Install dependencies
# ============================================
install_deps() {
    local packages=(
        "g++-14"
        "cmake"
        "git"
        "ninja-build"
        "binutils-gold"
        "ccache"
        "libsystemd-dev"
        "pkg-config"
        "linux-libc-dev"
    )

    echo "============================================"
    echo "  V2-Engine Dependency Installation"
    echo "============================================"
    echo "  The following packages will be installed:"
    for pkg in "${packages[@]}"; do
        echo "    - $pkg"
    done
    echo ""


    if [[ -t 0 ]]; then
        read -p "  Proceed with installation? [y/N] " confirm
        [[ "$confirm" != "y" && "$confirm" != "Y" ]] && { echo "  Skipping dependency installation."; return; }
    fi

    if ! command -v g++-14 >/dev/null 2>&1; then
        echo "==> g++-14 not found, adding ubuntu-toolchain-r/test PPA..."
        sudo apt install -y software-properties-common
        sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
        sudo apt-get update
    fi

    echo "==> Installing dependencies..."
    sudo apt install -y "${packages[@]}"
    echo ""
}

TARGETS=("$@")
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(all)

# ============================================
# Build
# ============================================
build() {
    local log_level="${LOG_LEVEL:-4}"
    local build_type="${BUILD_TYPE:-Release}"
    local cc="/usr/bin/gcc-14"
    local cxx="/usr/bin/g++-14"

    if ! command -v "${cxx}" >/dev/null 2>&1; then
        echo "==> ${cxx} not found. Run install_deps first or install g++-14."
        return 1
    fi

    # Invalidate the CMake cache if the cached compiler differs
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && \
       grep -q "CMAKE_CXX_COMPILER:.*=${cxx}" "$BUILD_DIR/CMakeCache.txt"; then
        :
    else
        echo "==> Compiler changed or cache missing, cleaning build cache..."
        rm -rf "$BUILD_DIR"
    fi

    echo "==> Building... (build_type=${build_type}, log_level=${log_level})"
    cmake -B "$BUILD_DIR" -G Ninja -Wno-dev \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DCMAKE_C_COMPILER="${cc}" \
        -DCMAKE_CXX_COMPILER="${cxx}" \
        -DV2_DEFAULT_LOG_LEVEL="${log_level}" 2>&1 | tail -1
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -1
    echo ""
}

# ============================================
# Install D-Bus system policy
# ============================================
install_dbus_policy() {
    local conf_file="/etc/dbus-1/system.d/com.v2.engine.conf"

    echo "==> Installing D-Bus policy..."

    sudo tee "$conf_file" > /dev/null << 'EOF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="com.v2.engine"/>
  </policy>
  <policy context="default">
    <allow send_destination="com.v2.engine"/>
    <allow receive_sender="com.v2.engine"/>
  </policy>
</busconfig>
EOF

    sudo systemctl reload dbus 2>/dev/null || echo "  (D-Bus reload skipped, will apply on next restart)"

    echo ""
}

# ============================================
# Install single app (service or binary)
# ============================================
install_app() {
    local name="$1"
    local desc="$2"
    local bin="$3"
    local type="$4"
    local deps="$5"

    echo "==> Installing $name ($type)..."

    if [[ "$type" == "service" ]]; then
        local unit_file="/etc/systemd/system/${name}.service"
        sudo tee "$unit_file" > /dev/null << EOF
[Unit]
Description=${desc}
$( [[ -n "$deps" ]] && echo "After=${deps}
Wants=${deps}" )

[Service]
Type=simple
ExecStart=${BUILD_DIR}/${bin}
WorkingDirectory=${PROJECT_DIR}
Restart=always

[Install]
WantedBy=multi-user.target
EOF
        sudo systemctl daemon-reload
        sudo systemctl enable "$name"
        sudo systemctl restart "$name" 2>/dev/null || true
        systemctl is-active --quiet "$name" && echo "  (running)" || echo "  (start failed)"
    fi

    sudo ln -sf "$BUILD_DIR/$bin" "/usr/local/bin/$name"
    if [[ "$name" == "v2_cli" ]]; then
        sudo ln -sf "/usr/local/bin/$name" "/usr/local/bin/v2"
    fi
    echo ""
}

# ============================================
# Main
# ============================================
install_deps
build
install_dbus_policy

if [[ " ${TARGETS[*]} " =~ " all " ]]; then
    for app in "${APPS[@]}"; do
        IFS=':' read -r name desc bin type deps <<< "$app"
        install_app "$name" "$desc" "$bin" "$type" "$deps"
    done
else
    for target in "${TARGETS[@]}"; do
        found=0
        for app in "${APPS[@]}"; do
            IFS=':' read -r name desc bin type deps <<< "$app"
            if [[ "$name" == "$target" ]]; then
                install_app "$name" "$desc" "$bin" "$type" "$deps"
                found=1
                break
            fi
        done
        if [[ $found -eq 0 ]]; then
            echo "Unknown app: $target"
            usage
        fi
    done
fi

echo "==> Install Complete!"
