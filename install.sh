#!/bin/bash

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "This script requires root privileges. Re-running with sudo..."
    exec sudo "$0" "$@"
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

detect_desktop_user() {
    if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
        printf '%s\n' "${SUDO_USER}"
        return 0
    fi

    return 1
}

install_gnome_extension_if_available() {
    local target_user=""
    local target_home=""

    if ! command -v gnome-shell >/dev/null 2>&1; then
        echo "--> GNOME Shell not detected; skipping GNOME extension installation."
        return 0
    fi

    if [[ ! -f "${script_dir}/gnome-extension/install.sh" ]]; then
        echo "--> GNOME extension installer not found; skipping GNOME extension installation."
        return 0
    fi

    if ! target_user="$(detect_desktop_user)"; then
        echo "Warning: GNOME Shell was detected, but no desktop user could be determined." >&2
        echo "Run '${script_dir}/gnome-extension/install.sh' as your normal user later if you want the panel extension." >&2
        return 0
    fi

    target_home="$(getent passwd "${target_user}" | cut -d: -f6)"
    if [[ -z "${target_home}" || ! -d "${target_home}" ]]; then
        echo "Warning: Could not resolve a home directory for '${target_user}'. Skipping GNOME extension installation." >&2
        return 0
    fi

    echo "--> GNOME Shell detected; installing panel extension for ${target_user}..."
    if sudo -u "${target_user}" HOME="${target_home}" bash "${script_dir}/gnome-extension/install.sh"; then
        echo "--> GNOME extension installed for ${target_user}."
    else
        echo "Warning: GNOME extension installation failed for '${target_user}'. You can retry manually later." >&2
    fi
}

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
        *" ubuntu "*|*" debian "*|*" linuxmint "*|*" pop "*)
            echo ubuntu
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

    if command -v apt-get >/dev/null 2>&1; then
        echo ubuntu
        return 0
    fi

    return 1
}

target="$(detect_target || true)"

case "${target}" in
    fedora)
        "${script_dir}/fedora-install.sh" "$@"
        ;;
    arch)
        "${script_dir}/arch-install.sh" "$@"
        ;;
    ubuntu)
        "${script_dir}/ubuntu-install.sh" "$@"
        ;;
    *)
        echo "Unsupported distribution. Use arch-install.sh, fedora-install.sh, or ubuntu-install.sh directly." >&2
        exit 1
        ;;
esac

install_gnome_extension_if_available
