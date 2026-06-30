#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# ============================================
# App configurations
# Format: "service_name:description:binary:dependency"
# ============================================
APPS=(
    "v2_main:V2 Engine Main App:v2_main:"
)

# ============================================
# Usage
# ============================================
usage() {
    echo "Usage: $0 [app...]"
    echo ""
    echo "Apps:"
    for app in "${APPS[@]}"; do
        IFS=':' read -r name desc bin _ <<< "$app"
        printf "  %-12s %s\n" "$name" "$desc"
    done
    echo ""
    echo "Examples:"
    echo "  $0            # Install all apps"
    echo "  $0 v2-demo    # Install only demo"
    exit 1
}

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    usage
fi

# ============================================
# Install dependencies
# ============================================
install_deps() {
    if [[ -n "$SKIP_DEPS" ]]; then
        echo "==> Skipping dependency installation (SKIP_DEPS is set)"
        return
    fi

    local packages=(
        "libsdbus-c++-dev"
    )

    echo "============================================"
    echo "  V2-Engine Dependency Installation"
    echo "============================================"
    echo "  The following packages will be installed:"
    for pkg in "${packages[@]}"; do
        echo "    - $pkg"
    done
    echo ""
    echo "  (set SKIP_DEPS=1 to skip this step)"
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
    echo "==> Building... (log_level=${log_level})"
    cmake -B "$BUILD_DIR" -Wno-dev -DV2_DEFAULT_LOG_LEVEL="${log_level}" 2>&1 | tail -1
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -1
    echo ""
}

# ============================================
# Install single service
# ============================================
install_service() {
    local name="$1"
    local desc="$2"
    local bin="$3"
    local deps="$4"

    echo "==> Installing $name..."

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

    echo ""
}

# ============================================
# Main
# ============================================
install_deps
build

if [[ " ${TARGETS[*]} " =~ " all " ]]; then
    for app in "${APPS[@]}"; do
        IFS=':' read -r name desc bin deps <<< "$app"
        install_service "$name" "$desc" "$bin" "$deps"
    done
else
    for target in "${TARGETS[@]}"; do
        found=0
        for app in "${APPS[@]}"; do
            IFS=':' read -r name desc bin deps <<< "$app"
            if [[ "$name" == "$target" ]]; then
                install_service "$name" "$desc" "$bin" "$deps"
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

echo "==> Creating symlink..."
sudo ln -sf "$BUILD_DIR/v2_cli" /usr/local/bin/v2
sudo ln -sf "$BUILD_DIR/v2_tui" /usr/local/bin/v2_tui

echo "==> Install Complete!"
