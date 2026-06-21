#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# ============================================
# App configurations
# Format: "service_name:description:binary:dependency"
# ============================================
APPS=(
    "v2-demo:V2 Engine Demo App:v2_demo:"
    "v2-cli:V2 Engine CLI App:v2_cli:v2-demo.service"
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

TARGETS=("$@")
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(all)

# ============================================
# Build
# ============================================
build() {
    echo "==> Building..."
    cmake -B "$BUILD_DIR" -Wno-dev 2>&1 | tail -1
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
WorkingDirectory=${BUILD_DIR}
Restart=always

[Install]
WantedBy=multi-user.target
EOF

    sudo systemctl daemon-reload
    sudo systemctl enable "$name"
    sudo systemctl start "$name" 2>/dev/null || true
    systemctl is-active --quiet "$name" && echo "  (running)" || echo "  (start failed)"

    echo ""
}

# ============================================
# Main
# ============================================
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

echo "==> Install Complete!"
