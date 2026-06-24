#!/usr/bin/env bash
set -euo pipefail

expand_guest_path() {
    case "$1" in
        "~")
            printf '%s\n' "${HOME}"
            ;;
        "~/"*)
            printf '%s/%s\n' "${HOME}" "${1#~/}"
            ;;
        *)
            printf '%s\n' "$1"
            ;;
    esac
}

basepath="$(expand_guest_path "${OPENQ4_BASEPATH:-${HOME}/openq4-work/Quake4}")"
archive="${OPENQ4_Q4_TAR:-}"
workspace="$(expand_guest_path "${OPENQ4_GUEST_WORKSPACE:-${HOME}/openq4-work}")"
incoming="${workspace}/incoming-quake4"

reject_unsafe_tar_entries() {
    local tar_path="$1"
    tar -tf "${tar_path}" | while IFS= read -r entry; do
        case "${entry}" in
            ""|"."|".."|./*|*/./*|*//*|/*|../*|*/../*|*/..|*\\*)
                echo "Unsafe asset archive path: ${entry}" >&2
                exit 1
                ;;
        esac
    done

    tar -tvf "${tar_path}" | while IFS= read -r listing; do
        entry_type="${listing:0:1}"
        case "${entry_type}" in
            -|d)
                ;;
            *)
                echo "Asset archive contains a symlink or special file (including hardlinks): ${listing}" >&2
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
    --exclude '/q4base/*.cfg' \
    --exclude '/q4base/*.log' \
    --exclude '/q4base/q4key' \
    --exclude '/q4base/quake4key' \
    --exclude '/q4base/savegames/' \
    --exclude '/q4base/screenshots/' \
    --exclude '/q4mp/*.cfg' \
    --exclude '/q4mp/*.log' \
    --exclude '/q4mp/screenshots/' \
    "${source_root}/" "${basepath}/"

if [[ ! -d "${basepath}/q4base" ]]; then
    echo "Installed asset directory is missing q4base: ${basepath}" >&2
    exit 1
fi

pk4_count="$(find "${basepath}/q4base" -maxdepth 1 -type f \( -name '*.pk4' -o -name '*.PK4' \) | wc -l | tr -d '[:space:]')"
if [[ "${pk4_count}" == "0" ]]; then
    echo "Installed asset directory has no q4base PK4 files: ${basepath}/q4base" >&2
    exit 1
fi
echo "Installed Quake 4 asset PK4 count: ${pk4_count}"

echo "Quake 4 asset install complete: ${basepath}"
du -sh "${basepath}" || true

case "${archive}" in
    /tmp/openq4-macos-*/*)
        rm -f "${archive}"
        ;;
esac
