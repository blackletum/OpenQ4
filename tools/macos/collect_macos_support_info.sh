#!/bin/sh
# Collect redacted macOS support data for experimental openQ4 crash reports.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
PACKAGE_ROOT=${OPENQ4_PACKAGE_ROOT:-$SCRIPT_DIR}
OUTPUT_DIR=${1:-$(pwd)}
STAMP=$(date -u +"%Y%m%d-%H%M%SZ")
WORK_PARENT=$(mktemp -d "${TMPDIR:-/tmp}/openq4-support.XXXXXX")
BUNDLE_NAME="openq4-macos-support-${STAMP}"
BUNDLE_DIR="${WORK_PARENT}/${BUNDLE_NAME}"
ARCHIVE_PATH="${OUTPUT_DIR%/}/${BUNDLE_NAME}.tar.gz"

cleanup() {
    rm -rf "${WORK_PARENT}"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "${BUNDLE_DIR}/system" "${BUNDLE_DIR}/package" "${BUNDLE_DIR}/logs" "${BUNDLE_DIR}/crash-reports"
mkdir -p "${OUTPUT_DIR}"

redact_text() {
    sed -E \
        -e 's|/Users/[^/[:space:]]+|~|g' \
        -e 's|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}|<email>|g'
}

write_text() {
    target=$1
    shift
    {
        for line in "$@"; do
            printf '%s\n' "$line"
        done
    } | redact_text > "${BUNDLE_DIR}/${target}"
}

write_command() {
    target=$1
    shift
    {
        printf '$'
        for part in "$@"; do
            printf ' %s' "$part"
        done
        printf '\n\n'
        "$@" 2>&1 || printf '\n(command failed; continuing support collection)\n'
    } | redact_text > "${BUNDLE_DIR}/${target}"
}

copy_text_if_present() {
    source_path=$1
    target_path=$2
    if [ -f "${source_path}" ]; then
        redact_text < "${source_path}" > "${BUNDLE_DIR}/${target_path}"
    fi
}

write_command "system/sw_vers.txt" sw_vers
write_command "system/uname.txt" uname -a
write_command "system/hardware.txt" system_profiler SPHardwareDataType
write_command "system/displays.txt" system_profiler SPDisplaysDataType

if command -v xcodebuild >/dev/null 2>&1; then
    write_command "system/xcode.txt" xcodebuild -version
else
    write_text "system/xcode.txt" "xcodebuild not found"
fi

if command -v xcrun >/dev/null 2>&1; then
    write_command "system/macos-sdk.txt" xcrun --sdk macosx --show-sdk-version
else
    write_text "system/macos-sdk.txt" "xcrun not found"
fi

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Current directory: %s\n' "$(pwd)"
    printf '\nExpected adjacent package-root entries:\n'
    for entry in \
        "openQ4.app" \
        "baseoq4" \
        "openQ4-client_arm64" \
        "openQ4-ded_arm64" \
        "collect_macos_support_info.sh" \
        "VERSION.txt" \
        "SYMBOLS.txt"
    do
        if [ -e "${PACKAGE_ROOT}/${entry}" ]; then
            printf 'present: %s\n' "${entry}"
        else
            printf 'missing: %s\n' "${entry}"
        fi
    done
    printf '\nPackage root listing:\n'
    ls -la "${PACKAGE_ROOT}" 2>&1 || true
    printf '\nbaseoq4 listing:\n'
    ls -la "${PACKAGE_ROOT}/baseoq4" 2>&1 || true
} | redact_text > "${BUNDLE_DIR}/package/layout.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'App path: %s\n' "${PACKAGE_ROOT}/openQ4.app"
    printf 'App executable path: %s\n' "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4"
    printf 'Expected loose client path: %s\n' "${PACKAGE_ROOT}/openQ4-client_arm64"
    printf 'Expected loose dedicated-server path: %s\n' "${PACKAGE_ROOT}/openQ4-ded_arm64"
    printf 'Expected game directory path: %s\n' "${PACKAGE_ROOT}/baseoq4"
    printf 'Expected log keys: fs_basepath, fs_cdpath, fs_savepath\n'
    printf '\nCaptured filesystem path lines from available logs:\n'

    found_log=0
    path_lines="${WORK_PARENT}/path-lines.txt"
    for log_path in \
        "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" \
        "${HOME}/baseoq4/logs/openq4.log" \
        "${PACKAGE_ROOT}/baseoq4/logs/openq4.log"
    do
        if [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'fs_(basepath|cdpath|savepath)|Auto-detected fs_basepath|base path|cd path|save path' "${log_path}" > "${path_lines}" 2>/dev/null; then
                tail -n 80 "${path_lines}"
            else
                printf '(no fs_basepath, fs_cdpath, or fs_savepath lines found in this log)\n'
            fi
        fi
    done

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. fs_basepath, fs_cdpath, and fs_savepath values could not be copied without launching openQ4.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/package/path-resolution.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Expected package metadata keys: version, version_tag, platform, arch, openq4_commit, openq4_dirty, openq4_game_commit, openq4_game_dirty\n'

    for manifest_path in \
        "${PACKAGE_ROOT}/VERSION.txt" \
        "${PACKAGE_ROOT}/openQ4.app/Contents/Resources/VERSION.txt"
    do
        printf '\n-- %s --\n' "${manifest_path}"
        if [ -f "${manifest_path}" ]; then
            if ! grep -E '^(version|version_tag|platform|arch|openq4_commit|openq4_dirty|openq4_game_commit|openq4_game_dirty)=' "${manifest_path}" 2>/dev/null; then
                printf '(no recognized package build metadata keys found)\n'
            fi
        else
            printf '(metadata manifest not found)\n'
        fi
    done

    printf '\nGame module files in baseoq4:\n'
    found_module=0
    for module_path in \
        "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dylib \
        "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dylib \
        "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dll \
        "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dll \
        "${PACKAGE_ROOT}/baseoq4"/game-sp_*.so \
        "${PACKAGE_ROOT}/baseoq4"/game-mp_*.so
    do
        if [ -e "${module_path}" ]; then
            found_module=1
            printf '%s\n' "${module_path}"
        fi
    done
    if [ "${found_module}" -eq 0 ]; then
        printf '(no game module files found)\n'
    fi
} | redact_text > "${BUNDLE_DIR}/package/build-metadata.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Expected log keys: OpenAL vendor, OpenAL renderer, OpenAL version, OpenAL requested device, OpenAL default device, OpenAL active device, OpenAL EFX\n'
    printf '\nCaptured OpenAL and EFX lines from available logs:\n'

    found_log=0
    found_audio_line=0
    openal_lines="${WORK_PARENT}/openal-lines.txt"
    for log_path in \
        "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" \
        "${HOME}/baseoq4/logs/openq4.log" \
        "${PACKAGE_ROOT}/baseoq4/logs/openq4.log"
    do
        if [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'OpenAL (vendor|renderer|version|requested device|default device|active device|ALC version|EFX|HRTF|output mode)|idSoundHardware_OpenAL::Init|s_deviceName|s_useEAXReverb' "${log_path}" > "${openal_lines}" 2>/dev/null; then
                found_audio_line=1
                tail -n 120 "${openal_lines}"
            else
                printf '(no OpenAL vendor, renderer, device, or EFX lines found in this log)\n'
            fi
        fi
    done

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. OpenAL vendor, renderer, device name, and EFX warning lines could not be copied without launching openQ4.\n'
    elif [ "${found_audio_line}" -eq 0 ]; then
        printf '\nNo OpenAL vendor, renderer, device name, or EFX warning lines were found in the copied logs.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/logs/openal-summary.txt"

copy_text_if_present "${PACKAGE_ROOT}/VERSION.txt" "package/VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Resources/VERSION.txt" "package/app-VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/SYMBOLS.txt" "package/SYMBOLS.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Info.plist" "package/Info.plist"
copy_text_if_present "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" "logs/home-openq4.log"
copy_text_if_present "${HOME}/baseoq4/logs/openq4.log" "logs/home-baseoq4-openq4.log"
copy_text_if_present "${PACKAGE_ROOT}/baseoq4/logs/openq4.log" "logs/package-baseoq4-openq4.log"

CRASH_DIR="${HOME}/Library/Logs/DiagnosticReports"
CRASH_LIST="${WORK_PARENT}/crash-list.txt"
if [ -d "${CRASH_DIR}" ]; then
    find "${CRASH_DIR}" -type f \( \
        -name 'openQ4*.ips' -o \
        -name 'openQ4*.crash' -o \
        -name 'openQ4-client*.ips' -o \
        -name 'openQ4-client*.crash' \
    \) -mtime -30 -print | sort | tail -n 5 > "${CRASH_LIST}"
    if [ -s "${CRASH_LIST}" ]; then
        while IFS= read -r crash_path; do
            crash_name=$(basename "${crash_path}")
            copy_text_if_present "${crash_path}" "crash-reports/${crash_name}"
        done < "${CRASH_LIST}"
    else
        write_text "crash-reports/README.txt" "No matching openQ4 crash reports were found in ~/Library/Logs/DiagnosticReports from the last 30 days."
    fi
else
    write_text "crash-reports/README.txt" "The macOS DiagnosticReports directory was not found."
fi

write_text "README.txt" \
    "Review this archive before attaching it to a public issue." \
    "The collector redacts /Users/<name> paths and email-like strings, does not dump the environment, does not launch openQ4, and does not copy retail q4base PK4 assets." \
    "package/build-metadata.txt records package VERSION.txt metadata, app VERSION.txt metadata, openQ4/openQ4-game commit fields when present, and the game module filenames in baseoq4/." \
    "package/path-resolution.txt records the package root, app path, expected loose runtime paths, and any fs_basepath, fs_cdpath, or fs_savepath lines found in copied logs." \
    "logs/openal-summary.txt records OpenAL vendor, renderer, version, device, and EFX warning/status lines found in copied logs." \
    "If package/SYMBOLS.txt is present, include it with any .ips report so maintainers can pick the matching macOS dSYM symbol archive." \
    "For issue #73 style crashes, include full terminal output as text in the issue body too; this archive cannot recover terminal output that was not logged."

tar -czf "${ARCHIVE_PATH}" -C "${WORK_PARENT}" "${BUNDLE_NAME}"

printf 'Created %s\n' "${ARCHIVE_PATH}"
printf 'Review the archive before attaching it to a public GitHub issue.\n'
