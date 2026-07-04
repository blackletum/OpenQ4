#!/bin/sh
# Collect redacted macOS support data for experimental openQ4 crash reports.

set -eu
umask 077

case "$0" in
    */*) script_dir=${0%/*} ;;
    *) script_dir=. ;;
esac
SCRIPT_DIR=$(CDPATH= cd "${script_dir}" && pwd -P)
PACKAGE_ROOT=${OPENQ4_PACKAGE_ROOT:-$SCRIPT_DIR}
OUTPUT_DIR=${1:-$(pwd)}
STAMP=$(date -u +"%Y%m%d-%H%M%SZ")
WORK_PARENT=$(mktemp -d "${TMPDIR:-/tmp}/openq4-support.XXXXXX")
BUNDLE_NAME="openq4-macos-support-${STAMP}"
BUNDLE_DIR="${WORK_PARENT}/${BUNDLE_NAME}"
ARCHIVE_PATH="${OUTPUT_DIR%/}/${BUNDLE_NAME}.tar.gz"
ARCHIVE_TMP="${OUTPUT_DIR%/}/.${BUNDLE_NAME}.$$.tar.gz.tmp"
MAX_SUPPORT_TEXT_BYTES=2097152
MAX_CRASH_REPORT_BYTES=8388608

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [ -n "${ARCHIVE_TMP:-}" ]; then
        rm -f "${ARCHIVE_TMP}"
    fi
    rm -rf "${WORK_PARENT}"
}
trap cleanup EXIT HUP INT TERM

runtime_arch_token() {
    case "$(uname -m 2>/dev/null || printf unknown)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x64\n' ;;
        i386|i686) printf 'x86\n' ;;
        *) uname -m 2>/dev/null || printf 'unknown\n' ;;
    esac
}

RUNTIME_ARCH=$(runtime_arch_token)
SKIPPED_CRASH_REPORT_INDEX=0

prepare_package_root() {
    if [ -L "${PACKAGE_ROOT}" ]; then
        fail "Support package root must not be a symlink: ${PACKAGE_ROOT}"
    fi
    if [ ! -d "${PACKAGE_ROOT}" ]; then
        fail "Support package root must be an existing directory: ${PACKAGE_ROOT}"
    fi
}

prepare_output_target() {
    if [ -z "${OUTPUT_DIR}" ]; then
        fail "Support output directory must not be empty"
    fi
    if [ -L "${OUTPUT_DIR}" ]; then
        fail "Support output directory must not be a symlink: ${OUTPUT_DIR}"
    fi
    if [ -e "${OUTPUT_DIR}" ] && [ ! -d "${OUTPUT_DIR}" ]; then
        fail "Support output path exists but is not a directory: ${OUTPUT_DIR}"
    fi
    mkdir -p "${OUTPUT_DIR}"
    if [ -L "${OUTPUT_DIR}" ] || [ ! -d "${OUTPUT_DIR}" ]; then
        fail "Support output directory must be a real directory: ${OUTPUT_DIR}"
    fi
    if [ -L "${ARCHIVE_PATH}" ] || [ -d "${ARCHIVE_PATH}" ]; then
        fail "Support archive target must not be a symlink or directory: ${ARCHIVE_PATH}"
    fi
    if [ -e "${ARCHIVE_PATH}" ]; then
        fail "Support archive target already exists: ${ARCHIVE_PATH}"
    fi
    if [ -L "${ARCHIVE_TMP}" ] || [ -e "${ARCHIVE_TMP}" ]; then
        fail "Support archive temporary target already exists: ${ARCHIVE_TMP}"
    fi
}

prepare_package_root
prepare_output_target
mkdir -p "${BUNDLE_DIR}/system" "${BUNDLE_DIR}/package" "${BUNDLE_DIR}/logs" "${BUNDLE_DIR}/crash-reports"

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

path_exists_for_inspection() {
    inspect_path=$1
    if [ -L "${inspect_path}" ]; then
        printf '\n-- %s --\n' "${inspect_path}"
        printf '(skipped symlink; support collector does not follow symlinks)\n'
        return 1
    fi
    [ -e "${inspect_path}" ]
}

copy_text_if_present() {
    source_path=$1
    target_path=$2
    max_bytes=${3:-$MAX_SUPPORT_TEXT_BYTES}
    if [ -L "${source_path}" ]; then
        write_text "${target_path}" \
            "Skipped symlinked source: ${source_path}" \
            "The support collector does not follow symlinks when copying package, log, or crash-report text."
    elif [ -f "${source_path}" ]; then
        source_bytes=$(wc -c < "${source_path}" 2>/dev/null | tr -d '[:space:]' || printf '0')
        case "${source_bytes}" in
            ''|*[!0123456789]*) source_bytes=0 ;;
        esac
        {
            if [ "${source_bytes}" -gt "${max_bytes}" ]; then
                printf 'Source file was larger than %s bytes and was truncated to its final %s bytes for this support archive: %s\n\n' "${max_bytes}" "${max_bytes}" "${source_path}"
                tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat "${source_path}"
            else
                cat "${source_path}"
            fi
        } | redact_text > "${BUNDLE_DIR}/${target_path}"
    fi
}

copy_crash_report_if_safe() {
    crash_path=$1
    crash_name=$(basename "${crash_path}")
    case "${crash_name}" in
        *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-]*)
            SKIPPED_CRASH_REPORT_INDEX=$((SKIPPED_CRASH_REPORT_INDEX + 1))
            write_text "crash-reports/skipped-${SKIPPED_CRASH_REPORT_INDEX}.txt" \
                "Skipped crash report with unsupported filename characters: ${crash_name}" \
                "The support collector only copies DiagnosticReports files with archive-safe names."
            return
            ;;
    esac
    copy_text_if_present "${crash_path}" "crash-reports/${crash_name}" "${MAX_CRASH_REPORT_BYTES}"
}

write_command "system/sw_vers.txt" sw_vers
write_command "system/uname.txt" uname -a
write_command "system/hardware.txt" system_profiler SPHardwareDataType
write_command "system/displays.txt" system_profiler SPDisplaysDataType

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Rosetta is not a supported openQ4 release target; this report only identifies translated support sessions.\n'

    if command -v arch >/dev/null 2>&1; then
        printf '\n$ arch\n'
        arch 2>&1 || printf '\n(command failed; continuing support collection)\n'
    else
        printf '\narch not found.\n'
    fi

    printf '\n$ uname -m\n'
    uname -m 2>&1 || printf '\n(command failed; continuing support collection)\n'

    if command -v sysctl >/dev/null 2>&1; then
        printf '\n$ sysctl -n sysctl.proc_translated\n'
        sysctl -n sysctl.proc_translated 2>&1 || printf '\n(sysctl.proc_translated unavailable; this is expected on Intel Macs and some native arm64 sessions)\n'
    else
        printf '\nsysctl not found.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/system/rosetta.txt"

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
    printf 'Detected runtime architecture token: %s\n' "${RUNTIME_ARCH}"
    printf 'Current directory: %s\n' "$(pwd)"
    printf '\nExpected adjacent package-root entries:\n'
    for entry in \
        "openQ4.app" \
        "baseoq4" \
        "openQ4-client_${RUNTIME_ARCH}" \
        "openQ4-ded_${RUNTIME_ARCH}" \
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
    printf 'Detected runtime architecture token: %s\n' "${RUNTIME_ARCH}"
    printf 'App path: %s\n' "${PACKAGE_ROOT}/openQ4.app"
    printf 'App executable path: %s\n' "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4"
    printf 'Expected loose client path: %s\n' "${PACKAGE_ROOT}/openQ4-client_${RUNTIME_ARCH}"
    printf 'Expected loose dedicated-server path: %s\n' "${PACKAGE_ROOT}/openQ4-ded_${RUNTIME_ARCH}"
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
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
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
        if [ -L "${manifest_path}" ]; then
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${manifest_path}" ]; then
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
        if [ -L "${module_path}" ]; then
            found_module=1
            printf '%s (skipped symlink; support collector does not follow symlinks)\n' "${module_path}"
        elif [ -e "${module_path}" ]; then
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
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Architecture checks do not launch openQ4.\n'

    for binary_path in \
        "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4" \
        "${PACKAGE_ROOT}/openQ4-client_arm64" \
        "${PACKAGE_ROOT}/openQ4-client_x64" \
        "${PACKAGE_ROOT}/openQ4-client_x86" \
        "${PACKAGE_ROOT}/openQ4-ded_arm64" \
        "${PACKAGE_ROOT}/openQ4-ded_x64" \
        "${PACKAGE_ROOT}/openQ4-ded_x86" \
        "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dylib \
        "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dylib
    do
        if path_exists_for_inspection "${binary_path}"; then
            printf '\n-- %s --\n' "${binary_path}"
            if command -v file >/dev/null 2>&1; then
                file "${binary_path}" 2>&1 || printf '(file inspection failed; continuing support collection)\n'
            else
                printf 'file not found.\n'
            fi
            if command -v lipo >/dev/null 2>&1; then
                printf 'lipo -archs: '
                lipo -archs "${binary_path}" 2>&1 || printf '(lipo architecture inspection failed; continuing support collection)\n'
            else
                printf 'lipo not found.\n'
            fi
        fi
    done
} | redact_text > "${BUNDLE_DIR}/package/binary-architecture.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Dependency and install-name checks do not launch openQ4.\n'

    if command -v otool >/dev/null 2>&1; then
        for binary_path in \
            "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4" \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dylib \
            "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dylib
        do
            if path_exists_for_inspection "${binary_path}"; then
                printf '\n-- otool -L: %s --\n' "${binary_path}"
                otool -L "${binary_path}" 2>&1 || printf '\n(otool dependency inspection failed; continuing support collection)\n'
                case "${binary_path}" in
                    *.dylib)
                        printf '\n-- otool -D: %s --\n' "${binary_path}"
                        otool -D "${binary_path}" 2>&1 || printf '\n(otool install-name inspection failed; continuing support collection)\n'
                        ;;
                esac
            fi
        done
    else
        printf 'otool not found.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/package/dylib-dependencies.txt"

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
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
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

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Expected renderer keys: R_InitOpenGL, renderer startup phase, Renderer driver quirks, Renderer bootstrap, ARB2 interaction driver bypass, fatal signal\n'
    printf '\nCaptured renderer startup and crash lines from available logs:\n'

    found_log=0
    found_renderer_line=0
    renderer_lines="${WORK_PARENT}/renderer-lines.txt"
    for log_path in \
        "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" \
        "${HOME}/baseoq4/logs/openq4.log" \
        "${PACKAGE_ROOT}/baseoq4/logs/openq4.log"
    do
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'R_InitOpenGL|R_ReloadARBPrograms|renderer startup phase|last renderer startup phase|first ARB2 interaction handoff|ARB2 interaction driver bypass|interaction color mode|renderer startup ARB interaction selection|Renderer driver quirks|Renderer bootstrap|Renderer upload manager|SDL3: graphics bridge|SDL3: reported OpenGL context|SDL3: OpenGL context|GL_ARB_vertex_buffer_object disabled|SimpleInteraction[.]vfp|Unsupported Apple OpenGL 2[.]1 compatibility path|bypassing ARB2 light interactions|using ARB2 renderSystem|fatal signal SIGSEGV' "${log_path}" > "${renderer_lines}" 2>/dev/null; then
                found_renderer_line=1
                tail -n 200 "${renderer_lines}"
            else
                printf '(no renderer startup, driver-quirk, ARB2, or fatal-signal lines found in this log)\n'
            fi
        fi
    done

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. Renderer startup and crash lines could not be copied without launching openQ4.\n'
    elif [ "${found_renderer_line}" -eq 0 ]; then
        printf '\nNo renderer startup, driver-quirk, ARB2, or fatal-signal lines were found in the copied logs.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/logs/renderer-summary.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Signing and Gatekeeper checks do not launch openQ4.\n'

    if command -v codesign >/dev/null 2>&1; then
        for signed_path in \
            "${PACKAGE_ROOT}/openQ4.app" \
            "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4" \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dylib \
            "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dylib
        do
            if path_exists_for_inspection "${signed_path}"; then
                printf '\n-- codesign verify: %s --\n' "${signed_path}"
                codesign --verify --deep --strict --verbose=4 "${signed_path}" 2>&1 || printf '\n(codesign verify failed; continuing support collection)\n'
                printf '\n-- codesign display: %s --\n' "${signed_path}"
                codesign -dv --verbose=4 "${signed_path}" 2>&1 || printf '\n(codesign display failed; continuing support collection)\n'
            fi
        done
    else
        printf 'codesign not found.\n'
    fi

    if command -v spctl >/dev/null 2>&1; then
        for assessed_path in \
            "${PACKAGE_ROOT}/openQ4.app" \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86"
        do
            if path_exists_for_inspection "${assessed_path}"; then
                printf '\n-- spctl assess: %s --\n' "${assessed_path}"
                spctl --assess --type execute --verbose=4 "${assessed_path}" 2>&1 || printf '\n(spctl assessment failed; continuing support collection)\n'
            fi
        done
    else
        printf 'spctl not found.\n'
    fi

    if command -v xcrun >/dev/null 2>&1; then
        for stapled_path in \
            "${PACKAGE_ROOT}/openQ4.app"
        do
            if path_exists_for_inspection "${stapled_path}"; then
                printf '\n-- xcrun stapler validate: %s --\n' "${stapled_path}"
                xcrun stapler validate "${stapled_path}" 2>&1 || printf '\n(stapler validation failed; continuing support collection)\n'
            fi
        done
    else
        printf 'xcrun not found; stapler validation skipped.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/package/signing.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Only extended-attribute names are listed; values are not copied.\n'
    printf 'Quarantine state is reported by checking whether com.apple.quarantine is present.\n'

    if command -v xattr >/dev/null 2>&1; then
        xattr_names="${WORK_PARENT}/xattr-names.txt"
        for xattr_path in \
            "${PACKAGE_ROOT}" \
            "${PACKAGE_ROOT}/openQ4.app" \
            "${PACKAGE_ROOT}/openQ4.app/Contents/MacOS/openQ4" \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4" \
            "${PACKAGE_ROOT}/baseoq4"/game-sp_*.dylib \
            "${PACKAGE_ROOT}/baseoq4"/game-mp_*.dylib \
            "${PACKAGE_ROOT}/collect_macos_support_info.sh"
        do
            if path_exists_for_inspection "${xattr_path}"; then
                printf '\n-- %s --\n' "${xattr_path}"
                if xattr "${xattr_path}" > "${xattr_names}" 2>&1; then
                    if [ -s "${xattr_names}" ]; then
                        cat "${xattr_names}"
                        if grep -qx 'com.apple.quarantine' "${xattr_names}"; then
                            printf 'quarantine: present\n'
                        else
                            printf 'quarantine: absent\n'
                        fi
                    else
                        printf '(no extended attributes)\n'
                        printf 'quarantine: absent\n'
                    fi
                else
                    cat "${xattr_names}"
                    printf '\n(xattr name listing failed; continuing support collection)\n'
                fi
            fi
        done
    else
        printf 'xattr not found.\n'
    fi
} | redact_text > "${BUNDLE_DIR}/package/quarantine.txt"

copy_text_if_present "${PACKAGE_ROOT}/VERSION.txt" "package/VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Resources/VERSION.txt" "package/app-VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/SYMBOLS.txt" "package/SYMBOLS.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Info.plist" "package/Info.plist"
copy_text_if_present "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" "logs/home-openq4.log"
copy_text_if_present "${HOME}/baseoq4/logs/openq4.log" "logs/home-baseoq4-openq4.log"
copy_text_if_present "${PACKAGE_ROOT}/baseoq4/logs/openq4.log" "logs/package-baseoq4-openq4.log"

CRASH_DIR="${HOME}/Library/Logs/DiagnosticReports"
CRASH_LIST="${WORK_PARENT}/crash-list.txt"
if [ -L "${CRASH_DIR}" ]; then
    write_text "crash-reports/README.txt" \
        "The macOS DiagnosticReports directory is a symlink and was skipped." \
        "The support collector does not follow symlinks when copying crash-report text."
elif [ -d "${CRASH_DIR}" ]; then
    find "${CRASH_DIR}" -type f \( \
        -name 'openQ4*.ips' -o \
        -name 'openQ4*.crash' -o \
        -name 'openQ4-client*.ips' -o \
        -name 'openQ4-client*.crash' -o \
        -name 'openQ4-ded*.ips' -o \
        -name 'openQ4-ded*.crash' \
    \) -mtime -30 -print | sort | tail -n 10 > "${CRASH_LIST}"
    if [ -s "${CRASH_LIST}" ]; then
        while IFS= read -r crash_path; do
            copy_crash_report_if_safe "${crash_path}"
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
    "The collector does not follow symlinked package, log, or crash-report inputs; skipped symlinks are recorded in the relevant report files." \
    "system/rosetta.txt records the collector process architecture and sysctl.proc_translated value so unsupported Rosetta/translated reports are easy to spot." \
    "package/build-metadata.txt records package VERSION.txt metadata, app VERSION.txt metadata, openQ4/openQ4-game commit fields when present, and the game module filenames in baseoq4/." \
    "package/binary-architecture.txt records file/lipo architecture output for package executables and game modules without launching openQ4." \
    "package/dylib-dependencies.txt records otool dependency and game-module install-name output without launching openQ4." \
    "package/path-resolution.txt records the package root, app path, expected loose runtime paths, and any fs_basepath, fs_cdpath, or fs_savepath lines found in copied logs." \
    "package/signing.txt records codesign, spctl execute assessment, and stapler validation output for package executables and app bundles without launching openQ4." \
    "package/quarantine.txt lists extended-attribute names and com.apple.quarantine presence without copying extended-attribute values." \
    "logs/openal-summary.txt records OpenAL vendor, renderer, version, device, and EFX warning/status lines found in copied logs." \
    "logs/renderer-summary.txt records renderer startup, driver-quirk, ARB2 interaction, and fatal-signal breadcrumbs found in copied logs." \
    "crash-reports/ includes up to 10 recent matching openQ4, openQ4-client, and openQ4-ded DiagnosticReports files when macOS wrote them." \
    "If package/SYMBOLS.txt is present, include it with any .ips report so maintainers can pick the matching macOS dSYM symbol archive." \
    "For issue #73 style crashes, include full terminal output as text in the issue body too; this archive cannot recover terminal output that was not logged."

COPYFILE_DISABLE=1 tar -czf "${ARCHIVE_TMP}" -C "${WORK_PARENT}" "${BUNDLE_NAME}"
chmod 600 "${ARCHIVE_TMP}"
if [ -L "${ARCHIVE_PATH}" ] || [ -e "${ARCHIVE_PATH}" ]; then
    fail "Support archive target appeared while collecting data: ${ARCHIVE_PATH}"
fi
mv "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"
ARCHIVE_TMP=""

printf 'Created %s\n' "${ARCHIVE_PATH}"
printf 'Review the archive before attaching it to a public GitHub issue.\n'
