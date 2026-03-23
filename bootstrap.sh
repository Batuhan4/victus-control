#!/bin/bash

set -euo pipefail

repo_url="${VICTUS_CONTROL_REPO_URL:-https://github.com/Batuhan4/victus-control}"
repo_ref="${VICTUS_CONTROL_REF:-main}"
archive_url="${VICTUS_CONTROL_ARCHIVE_URL:-${repo_url}/archive/refs/heads/${repo_ref}.tar.gz}"
tmpdir=""

cleanup() {
    if [[ -n "${tmpdir}" && -d "${tmpdir}" ]]; then
        rm -rf "${tmpdir}"
    fi
}

download_archive() {
    local output_file="$1"

    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "${archive_url}" -o "${output_file}"
        return 0
    fi

    if command -v wget >/dev/null 2>&1; then
        wget -qO "${output_file}" "${archive_url}"
        return 0
    fi

    echo "Error: curl or wget is required to download victus-control." >&2
    return 1
}

main() {
    local archive_path=""
    local repo_dir=""

    tmpdir="$(mktemp -d)"
    trap cleanup EXIT

    archive_path="${tmpdir}/victus-control-main.tar.gz"

    echo "--> Downloading victus-control from ${repo_url} (${repo_ref})..."
    download_archive "${archive_path}"

    echo "--> Extracting archive..."
    tar -xzf "${archive_path}" -C "${tmpdir}"

    repo_dir="$(find "${tmpdir}" -mindepth 1 -maxdepth 1 -type d -name 'victus-control-*' | head -n 1 || true)"
    if [[ -z "${repo_dir}" ]]; then
        echo "Error: Failed to locate extracted victus-control directory." >&2
        return 1
    fi

    if [[ "${VICTUS_CONTROL_BOOTSTRAP_SKIP_INSTALL:-0}" == "1" ]]; then
        echo "--> Bootstrap download test complete: ${repo_dir}"
        return 0
    fi

    echo "--> Starting installer..."
    "${repo_dir}/install.sh" "$@"
}

main "$@"
