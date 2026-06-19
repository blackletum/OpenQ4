#!/usr/bin/env bash
set -euo pipefail

basepath="${OPENQ4_BASEPATH:-${HOME}/openq4-work/Quake4}"
archive="${OPENQ4_Q4_TAR:-}"
workspace="${OPENQ4_GUEST_WORKSPACE:-${HOME}/openq4-work}"
incoming="${workspace}/incoming-quake4"

reject_unsafe_tar_entries() {
    local tar_path="$1"
    tar -tf "${tar_path}" | while IFS= read -r entry; do
        case "${entry}" in
            ""|/*|../*|*/../*|*\\*)
                echo "Unsafe asset archive path: ${entry}" >&2
                exit 1
                ;;
        esac
    done
}

reject_unsafe_tree_entries() {
    local tree="$1"
    local bad_entry
    bad_entry="$(find "${tree}" \( -type l -o \( ! -type f -a ! -type d \) \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "Asset archive contains a symlink or special file: ${bad_entry}" >&2
        exit 1
    fi
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This asset script must run inside macOS." >&2
    exit 1
fi

if [[ -z "${archive}" || ! -f "${archive}" ]]; then
    echo "OPENQ4_Q4_TAR must point to a Quake 4 asset tar copied to the macOS VM." >&2
    exit 1
fi

rm -rf "${incoming}"
mkdir -p "${incoming}" "${basepath}"

echo "Extracting Quake 4 asset archive: ${archive}"
reject_unsafe_tar_entries "${archive}"
tar -xf "${archive}" -C "${incoming}"
reject_unsafe_tree_entries "${incoming}"

q4base_dirs=()
while IFS= read -r q4base_candidate; do
    q4base_dirs+=("${q4base_candidate}")
done < <(find "${incoming}" -maxdepth 3 -type d -name q4base)

if [[ "${#q4base_dirs[@]}" -eq 0 ]]; then
    echo "Archive did not contain a q4base directory." >&2
    exit 1
fi
if [[ "${#q4base_dirs[@]}" -ne 1 ]]; then
    echo "Archive contained multiple q4base directories; refusing ambiguous asset install." >&2
    printf '  - %s\n' "${q4base_dirs[@]}" >&2
    exit 1
fi

q4base_dir="${q4base_dirs[0]}"
source_root="$(dirname "${q4base_dir}")"
echo "Installing Quake 4 assets from ${source_root} to ${basepath}"
rsync -a --delete \
    --exclude '/.tmp/' \
    --exclude '/CrashReports/' \
    --exclude '/q4base/generated/' \
    --exclude '/q4base/savegames/' \
    --exclude '/q4base/screenshots/' \
    --exclude '/q4mp/screenshots/' \
    "${source_root}/" "${basepath}/"

if [[ ! -d "${basepath}/q4base" ]]; then
    echo "Installed asset directory is missing q4base: ${basepath}" >&2
    exit 1
fi

echo "Quake 4 asset install complete: ${basepath}"
du -sh "${basepath}" || true

case "${archive}" in
    /tmp/openq4-macos-*/*)
        rm -f "${archive}"
        ;;
esac
