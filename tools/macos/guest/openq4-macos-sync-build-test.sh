#!/usr/bin/env bash
set -euo pipefail

action="${1:-build}"
graphics_bridge="${OPENQ4_MACOS_GRAPHICS_BRIDGE:-opengl}"
openal_provider="${OPENQ4_MACOS_OPENAL_PROVIDER:-apple_framework}"
stamp="${OPENQ4_MACOS_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"

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

workspace="$(expand_guest_path "${OPENQ4_GUEST_WORKSPACE:-${HOME}/openq4-work}")"
repo="${workspace}/openQ4"
gamelibs="${workspace}/openQ4-game"
basepath="$(expand_guest_path "${OPENQ4_BASEPATH:-${workspace}/Quake4}")"

require_result_token() {
    local label="$1"
    local value="$2"

    case "${value}" in
        ""|[!A-Za-z0-9]*|*[!A-Za-z0-9._-]*)
            echo "Invalid ${label} '${value}'. Use letters, digits, dots, underscores, or dashes, starting with a letter or digit." >&2
            exit 2
            ;;
    esac
}

require_choice() {
    local label="$1"
    local value="$2"
    shift 2

    local choice
    for choice in "$@"; do
        if [[ "${value}" == "${choice}" ]]; then
            return
        fi
    done

    echo "Invalid ${label} '${value}'. Expected one of: $*" >&2
    exit 2
}

require_positive_integer() {
    local label="$1"
    local value="$2"
    local max="$3"

    case "${value}" in
        ""|*[!0-9]*)
            echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
            exit 2
            ;;
    esac
    if (( ${#value} > ${#max} )); then
        echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
        exit 2
    fi

    local number=$((10#${value}))
    if (( number < 1 || number > max )); then
        echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
        exit 2
    fi
}

require_result_token "OPENQ4_MACOS_RUN_ID" "${stamp}"
require_choice "macOS workflow action" "${action}" build smoke renderer launcher signoff all
require_choice "OPENQ4_MACOS_GRAPHICS_BRIDGE" "${graphics_bridge}" opengl metal
require_choice "OPENQ4_MACOS_OPENAL_PROVIDER" "${openal_provider}" apple_framework system

results_root="${workspace}/results"
run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"

shell_quote() {
    python3 - "$1" <<'PY'
import shlex
import sys

print(shlex.quote(sys.argv[1]))
PY
}

resolve_under_repo() {
    local candidate="$1"
    python3 - "${repo}" "${candidate}" <<'PY'
import pathlib
import sys

repo = pathlib.Path(sys.argv[1]).resolve()
candidate = pathlib.Path(sys.argv[2])
if not candidate.is_absolute():
    candidate = repo / candidate
candidate = candidate.resolve()

try:
    candidate.relative_to(repo)
except ValueError:
    raise SystemExit(f"Path escapes the openQ4 repository: {candidate}")

print(candidate)
PY
}

require_safe_builddir() {
    local raw_builddir="$1"
    local resolved_builddir="$2"

    if [[ -e "${raw_builddir}" && -L "${raw_builddir}" ]]; then
        echo "OPENQ4_BUILDDIR must not be a symlink: ${raw_builddir}" >&2
        exit 2
    fi
    if [[ -e "${resolved_builddir}" && ! -d "${resolved_builddir}" ]]; then
        echo "OPENQ4_BUILDDIR must resolve to a directory: ${resolved_builddir}" >&2
        exit 2
    fi
    if [[ "${resolved_builddir}" == "${repo}" ]]; then
        echo "OPENQ4_BUILDDIR must not resolve to the openQ4 repository root." >&2
        exit 2
    fi

    case "${resolved_builddir}" in
        "${repo}/.git"|\
        "${repo}/.git/"*|\
        "${repo}/.install"|\
        "${repo}/.install/"*|\
        "${repo}/baseoq4"|\
        "${repo}/baseoq4/"*|\
        "${repo}/content"|\
        "${repo}/content/"*|\
        "${repo}/src"|\
        "${repo}/src/"*|\
        "${repo}/tools"|\
        "${repo}/tools/"*)
            echo "OPENQ4_BUILDDIR must not target source, content, tool, git, or staged runtime directories: ${resolved_builddir}" >&2
            exit 2
            ;;
    esac
}

if [[ -x "${HOME}/.local/bin/meson" ]]; then
    export PATH="${HOME}/.local/bin:${PATH}"
    export OPENQ4_MESON="${OPENQ4_MESON:-${HOME}/.local/bin/meson}"
fi

mkdir -p "${run_dir}"
exec > >(tee -a "${run_dir}/openq4-macos-workflow.log") 2>&1

host_arch() {
    case "$(uname -m)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x64\n' ;;
        *) uname -m ;;
    esac
}

host_lipo_arch() {
    case "$(uname -m)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x86_64\n' ;;
        *) uname -m ;;
    esac
}

client_binary() {
    local arch
    arch="$(host_arch)"
    if [[ -x "${repo}/.install/openQ4-client_${arch}" ]]; then
        printf '%s\n' "${repo}/.install/openQ4-client_${arch}"
        return
    fi

    if [[ "${OPENQ4_ALLOW_CROSS_ARCH_CLIENT:-0}" != "1" ]]; then
        echo "Missing staged macOS client for host architecture ${arch}: ${repo}/.install/openQ4-client_${arch}" >&2
        echo "Set OPENQ4_ALLOW_CROSS_ARCH_CLIENT=1 only when intentionally validating Rosetta/cross-arch behavior." >&2
        return 1
    fi

    local candidate
    for candidate in "${repo}"/.install/openQ4-client_*; do
        if [[ -f "${candidate}" && -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return
        fi
    done
}

count_q4base_pk4s() {
    local root="$1"
    find "${root}/q4base" -maxdepth 1 -type f \( -iname '*.pk4' \) | wc -l | tr -d '[:space:]'
}

validate_asset_basepath() {
    if [[ ! -d "${basepath}/q4base" ]]; then
        echo "Missing Quake 4 asset basepath: ${basepath}" >&2
        echo "Expected ${basepath}/q4base. Run host action Assets or set OPENQ4_BASEPATH." >&2
        exit 1
    fi

    local pk4_count
    pk4_count="$(count_q4base_pk4s "${basepath}")"
    if [[ "${pk4_count}" == "0" ]]; then
        echo "Quake 4 asset basepath has no q4base PK4 files: ${basepath}/q4base" >&2
        exit 1
    fi

    echo "Validated Quake 4 asset basepath: ${basepath} (${pk4_count} q4base PK4 files)."
}

validate_staged_macos_payload() {
    require_repo

    local arch
    arch="$(host_arch)"
    local lipo_arch
    lipo_arch="$(host_lipo_arch)"
    local install_root="${repo}/.install"
    local game_dir="${install_root}/baseoq4"

    if [[ ! -d "${install_root}" ]]; then
        echo "Missing staged macOS install root: ${install_root}" >&2
        exit 1
    fi
    if [[ ! -d "${game_dir}" ]]; then
        echo "Missing staged macOS game directory: ${game_dir}" >&2
        exit 1
    fi

    local required=(
        "${install_root}/openQ4-client_${arch}"
        "${install_root}/openQ4-ded_${arch}"
        "${game_dir}/game-sp_${arch}.dylib"
        "${game_dir}/game-mp_${arch}.dylib"
    )

    local path
    for path in "${required[@]}"; do
        if [[ ! -f "${path}" || ! -x "${path}" ]]; then
            echo "Missing or non-executable staged macOS payload: ${path}" >&2
            exit 1
        fi
    done

    local bad_entry
    bad_entry="$(find "${install_root}" \( -iname '.DS_Store' -o -name '._*' -o -iname '__MACOSX' -o -iname '.fseventsd' -o -iname '.Spotlight-V100' -o -iname '.Trashes' -o -name $'Icon\r' -o -name '*.dSYM' \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "macOS staged install contains non-runtime metadata/debug entry: ${bad_entry}" >&2
        exit 1
    fi

    if compgen -G "${game_dir}/game-*.dll" >/dev/null || compgen -G "${game_dir}/game-*.so" >/dev/null; then
        echo "macOS staged install contains non-dylib game modules." >&2
        exit 1
    fi

    if command -v lipo >/dev/null 2>&1; then
        for path in "${required[@]}"; do
            if ! lipo -archs "${path}" | tr ' ' '\n' | grep -qx "${lipo_arch}"; then
                echo "Staged macOS binary architecture mismatch: ${path}; expected ${lipo_arch}" >&2
                lipo -archs "${path}" >&2 || true
                exit 1
            fi
        done
    fi

    echo "Validated staged macOS payload for ${arch}."
}

require_repo() {
    if [[ ! -d "${repo}" ]]; then
        echo "Missing guest source workspace: ${repo}" >&2
        echo "Run host action Sync first." >&2
        exit 1
    fi
}

build_openq4() {
    require_repo
    cd "${repo}"
    export OPENQ4_GAMELIBS_REPO="${gamelibs}"
    export OPENQ4_BUILD_GAMELIBS="${OPENQ4_BUILD_GAMELIBS:-1}"

    local buildtype="${OPENQ4_BUILDTYPE:-debug}"
    local raw_builddir="${OPENQ4_BUILDDIR:-builddir}"
    local builddir="${raw_builddir}"
    local platform_backend="${OPENQ4_PLATFORM_BACKEND:-sdl3}"
    require_choice "OPENQ4_BUILDTYPE" "${buildtype}" plain debug debugoptimized release minsize custom
    require_choice "OPENQ4_PLATFORM_BACKEND" "${platform_backend}" sdl3 native
    builddir="$(resolve_under_repo "${builddir}")"
    require_safe_builddir "${raw_builddir}" "${builddir}"

    echo "Configuring openQ4 (${buildtype}, backend=${platform_backend}, macos_graphics_bridge=${graphics_bridge}, macos_openal_provider=${openal_provider})"
    bash tools/build/meson_setup.sh setup --wipe "${builddir}" . \
        --backend ninja \
        --buildtype="${buildtype}" \
        --wrap-mode=forcefallback \
        "-Dplatform_backend=${platform_backend}" \
        "-Dmacos_graphics_bridge=${graphics_bridge}" \
        "-Dmacos_openal_provider=${openal_provider}"

    echo "Compiling openQ4"
    bash tools/build/meson_setup.sh compile -C "${builddir}"

    echo "Staging openQ4 into .install"
    bash tools/build/meson_setup.sh install -C "${builddir}" --no-rebuild --skip-subprojects
    validate_staged_macos_payload
}

run_smoke() {
    require_repo
    cd "${repo}"

    validate_asset_basepath

    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi

    local smoke_limit="${OPENQ4_SMOKE_LIMIT:-1}"
    local smoke_settle_frames="${OPENQ4_SMOKE_SETTLE_FRAMES:-10}"
    local smoke_sample_frames="${OPENQ4_SMOKE_SAMPLE_FRAMES:-10}"
    local smoke_timeout="${OPENQ4_SMOKE_TIMEOUT:-300}"
    require_positive_integer "OPENQ4_SMOKE_LIMIT" "${smoke_limit}" 100000
    require_positive_integer "OPENQ4_SMOKE_SETTLE_FRAMES" "${smoke_settle_frames}" 100000
    require_positive_integer "OPENQ4_SMOKE_SAMPLE_FRAMES" "${smoke_sample_frames}" 100000
    require_positive_integer "OPENQ4_SMOKE_TIMEOUT" "${smoke_timeout}" 86400

    echo "Running openQ4 macOS renderer smoke with ${client}"
    python3 tools/tests/renderer_gameplay_benchmark.py \
        --profile smoke \
        --limit "${smoke_limit}" \
        --settle-frames "${smoke_settle_frames}" \
        --sample-frames "${smoke_sample_frames}" \
        --timeout "${smoke_timeout}" \
        --output-dir "${run_dir}/renderer-smoke" \
        --basepath "${basepath}"
}

run_renderer_matrix() {
    require_repo
    cd "${repo}"

    local renderer_timeout="${OPENQ4_RENDERER_TIMEOUT:-180}"
    require_positive_integer "OPENQ4_RENDERER_TIMEOUT" "${renderer_timeout}" 86400

    echo "Running macOS-facing renderer validation matrix"
    python3 tools/tests/renderer_validation_matrix.py \
        --tiers auto,gl41 \
        --cases renderer-gbuffer-selftest,renderer-cluster-grid-selftest,renderer-shadow-planner-selftest,renderer-shadow-projected-diagnostic,renderer-modern-visible-selftest,shader-library-gl41,tier-auto,tier-gl41 \
        --timeout "${renderer_timeout}" \
        --output-dir "${run_dir}/renderer-matrix"
}

append_command_report() {
    local report="$1"
    local title="$2"
    shift 2

    {
        echo
        echo "## ${title}"
        echo '```text'
        "$@" 2>&1 || true
        echo '```'
    } >> "${report}"
}

append_staged_binary_arch_report() {
    local report="$1"
    local arch
    arch="$(host_arch)"
    local install_root="${repo}/.install"
    local game_dir="${install_root}/baseoq4"
    local required=(
        "${install_root}/openQ4-client_${arch}"
        "${install_root}/openQ4-ded_${arch}"
        "${game_dir}/game-sp_${arch}.dylib"
        "${game_dir}/game-mp_${arch}.dylib"
    )
    local path

    {
        echo
        echo "## Staged Binary Architectures"
        echo '```text'
        if command -v lipo >/dev/null 2>&1; then
            for path in "${required[@]}"; do
                printf '%s: ' "${path}"
                lipo -archs "${path}" 2>&1 || true
            done
        else
            echo "lipo was not found."
        fi
        echo '```'
    } >> "${report}"
}

install_launcher() {
    require_repo
    mkdir -p "${HOME}/Desktop"
    local launcher="${HOME}/Desktop/openQ4.command"
    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi
    local install_dir_q client_q basepath_q
    install_dir_q="$(shell_quote "${repo}/.install")"
    client_q="$(shell_quote "${client}")"
    basepath_q="$(shell_quote "${basepath}")"

    cat > "${launcher}" <<EOF
#!/usr/bin/env bash
cd ${install_dir_q}
exec ${client_q} +set fs_basepath ${basepath_q} "\$@"
EOF
    chmod +x "${launcher}"
    echo "Installed macOS launcher: ${launcher}"
}

write_signoff_report() {
    require_repo
    local report="${run_dir}/macos-runtime-signoff.md"
    local client
    client="$(client_binary 2>/dev/null || true)"

    {
        echo "# macOS Runtime Signoff"
        echo
        echo "- Date (UTC): $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "- Host: $(hostname)"
        echo "- Architecture: $(uname -m)"
        echo "- Graphics bridge: ${graphics_bridge}"
        echo "- OpenAL provider: ${openal_provider}"
        echo "- Build directory: ${OPENQ4_BUILDDIR:-builddir}"
        echo "- Asset basepath: ${basepath}"
        echo "- Client: ${client:-not found}"
        echo "- Results: ${run_dir}"
        echo
        echo "## Automated Evidence"
        echo "- [x] Bridge-specific build and staged install completed."
        echo "- [x] Staged macOS payload integrity checks completed."
        echo "- [x] Quake 4 asset basepath validation completed."
        echo "- [x] Renderer smoke profile completed with retail Quake 4 assets."
        echo "- [x] macOS-facing renderer validation matrix completed."
        echo "- [x] Desktop launcher was written for Finder/Terminal launch checks."
        echo "- Renderer smoke output: ${run_dir}/renderer-smoke"
        echo "- Renderer matrix output: ${run_dir}/renderer-matrix"
        echo "- Workflow log: ${run_dir}/openq4-macos-workflow.log"
        echo
        echo "## Manual Hardware Checklist"
        echo "- [ ] Launch openQ4 from Finder or the Desktop launcher and enter a single-player map."
        echo "- [ ] Verify keyboard text entry, console toggle, mouse-look, clicks, and wheel input."
        echo "- [ ] Verify at least one SDL game controller, including hotplug and rumble when hardware supports it."
        echo "- [ ] Verify audio output, volume changes, and at least one device switch or reconnect."
        echo "- [ ] Verify windowed, fullscreen, selected-display, and HiDPI/Retina behavior on attached displays."
        echo "- [ ] Verify the matching OpenGL or Metal bridge package path in actual gameplay, not only at the main menu."
        echo "- [ ] Record any input, audio, display, renderer, package, Gatekeeper, or crash issues with logs from this results directory."
    } > "${report}"

    append_command_report "${report}" "macOS Version" sw_vers
    append_command_report "${report}" "Kernel" uname -a
    append_command_report "${report}" "Displays" system_profiler SPDisplaysDataType
    append_command_report "${report}" "Audio Devices" system_profiler SPAudioDataType
    append_command_report "${report}" "USB Devices" system_profiler SPUSBDataType
    append_command_report "${report}" "Bluetooth Devices" system_profiler SPBluetoothDataType
    append_command_report "${report}" "Staged Payload" find "${repo}/.install" -maxdepth 2 -print
    append_staged_binary_arch_report "${report}"

    echo "macOS runtime signoff report: ${report}"
}

run_signoff() {
    build_openq4
    run_smoke
    run_renderer_matrix
    install_launcher
    write_signoff_report
}

case "${action}" in
    build)
        build_openq4
        ;;
    smoke)
        run_smoke
        ;;
    renderer)
        run_renderer_matrix
        ;;
    launcher)
        install_launcher
        ;;
    signoff)
        run_signoff
        ;;
    all)
        run_signoff
        ;;
    *)
        echo "Unknown action '${action}'. Expected build, smoke, renderer, launcher, signoff, or all." >&2
        exit 2
        ;;
esac

echo "openQ4 macOS workflow '${action}' complete. Guest results: ${run_dir}"
