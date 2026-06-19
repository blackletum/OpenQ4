#!/usr/bin/env bash
set -euo pipefail

action="${1:-build}"
workspace="${OPENQ4_GUEST_WORKSPACE:-${HOME}/openq4-work}"
repo="${workspace}/openQ4"
gamelibs="${workspace}/openQ4-GameLibs"
basepath="${OPENQ4_BASEPATH:-${workspace}/Quake4}"
stamp="$(date +%Y%m%d-%H%M%S)"
results_root="${workspace}/results"
run_dir="${results_root}/${stamp}-${action}"

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
    local builddir="${OPENQ4_BUILDDIR:-builddir}"
    local platform_backend="${OPENQ4_PLATFORM_BACKEND:-sdl3}"
    local bridge="${OPENQ4_MACOS_GRAPHICS_BRIDGE:-opengl}"
    builddir="$(resolve_under_repo "${builddir}")"

    echo "Configuring openQ4 (${buildtype}, backend=${platform_backend}, macos_graphics_bridge=${bridge})"
    bash tools/build/meson_setup.sh setup --wipe "${builddir}" . \
        --backend ninja \
        --buildtype="${buildtype}" \
        --wrap-mode=forcefallback \
        "-Dplatform_backend=${platform_backend}" \
        "-Dmacos_graphics_bridge=${bridge}"

    echo "Compiling openQ4"
    bash tools/build/meson_setup.sh compile -C "${builddir}"

    echo "Staging openQ4 into .install"
    bash tools/build/meson_setup.sh install -C "${builddir}" --no-rebuild --skip-subprojects
}

run_smoke() {
    require_repo
    cd "${repo}"

    if [[ ! -d "${basepath}/q4base" ]]; then
        echo "Missing Quake 4 asset basepath: ${basepath}" >&2
        echo "Expected ${basepath}/q4base. Run host action Assets or set OPENQ4_BASEPATH." >&2
        exit 1
    fi

    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi

    echo "Running openQ4 macOS renderer smoke with ${client}"
    python3 tools/tests/renderer_gameplay_benchmark.py \
        --profile smoke \
        --limit "${OPENQ4_SMOKE_LIMIT:-1}" \
        --settle-frames "${OPENQ4_SMOKE_SETTLE_FRAMES:-10}" \
        --sample-frames "${OPENQ4_SMOKE_SAMPLE_FRAMES:-10}" \
        --timeout "${OPENQ4_SMOKE_TIMEOUT:-300}" \
        --output-dir "${run_dir}/renderer-smoke" \
        --basepath "${basepath}"
}

run_renderer_matrix() {
    require_repo
    cd "${repo}"

    echo "Running macOS-facing renderer validation matrix"
    python3 tools/tests/renderer_validation_matrix.py \
        --tiers auto,gl41 \
        --cases renderer-gbuffer-selftest,renderer-cluster-grid-selftest,renderer-shadow-planner-selftest,renderer-shadow-projected-diagnostic,renderer-modern-visible-selftest,shader-lensflare-gl41,tier-auto,tier-gl41 \
        --timeout "${OPENQ4_RENDERER_TIMEOUT:-180}" \
        --output-dir "${run_dir}/renderer-matrix"
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
    all)
        build_openq4
        run_smoke
        run_renderer_matrix
        install_launcher
        ;;
    *)
        echo "Unknown action '${action}'. Expected build, smoke, renderer, launcher, or all." >&2
        exit 2
        ;;
esac

echo "openQ4 macOS workflow '${action}' complete. Guest results: ${run_dir}"
