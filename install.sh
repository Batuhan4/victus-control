#!/bin/bash

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "This script requires root privileges. Re-running with sudo..."
    exec sudo "$0" "$@"
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

detect_target() {
    local os_id=""
    local os_like=""

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        os_id="${ID:-}"
        os_like="${ID_LIKE:-}"
    fi

    case " ${os_id} ${os_like} " in
        *" fedora "*|*" rhel "*)
            echo fedora
            return 0
            ;;
        *" arch "*|*" endeavouros "*|*" cachyos "*|*" manjaro "*)
            echo arch
            return 0
            ;;
    esac

    if command -v dnf >/dev/null 2>&1; then
        echo fedora
        return 0
    fi

    if command -v pacman >/dev/null 2>&1; then
        echo arch
        return 0
    fi

    return 1
}

target="$(detect_target || true)"

case "${target}" in
    fedora)
        exec "${script_dir}/fedora-install.sh" "$@"
        ;;
    arch)
        exec "${script_dir}/arch-install.sh" "$@"
        ;;
    *)
        echo "Unsupported distribution. Use arch-install.sh or fedora-install.sh directly." >&2
        exit 1
        ;;
esac
