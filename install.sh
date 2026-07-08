#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

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
        "build-essential"
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
    local log_level="${LOG_LEVEL:-3}"
    local build_type="${BUILD_TYPE:-Release}"
    echo "==> Building... (build_type=${build_type}, log_level=${log_level})"
    cmake -B "$BUILD_DIR" -G Ninja -Wno-dev \
        -DCMAKE_BUILD_TYPE="${build_type}" \
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
