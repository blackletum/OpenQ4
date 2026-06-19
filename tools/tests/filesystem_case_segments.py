#!/usr/bin/env python3
"""Regression checks for case-sensitive filesystem path segment handling."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_filesystem_case_resolution() -> None:
    source = read("src/framework/FileSystem.cpp")
    resolver = function_body(source, "bool idFileSystemLocal::ResolveCaseInsensitiveOSPath(")
    finder = function_body(source, "bool idFileSystemLocal::FindCaseInsensitiveOSPathEntry(")
    open_os_file = function_body(source, "FILE *idFileSystemLocal::OpenOSFile(")
    build_os_path = function_body(source, "const char *idFileSystemLocal::BuildOSPath(")
    list_os_files = function_body(source, "int\tidFileSystemLocal::ListOSFiles(")

    reject(source, "IMPORTANT: This only applies to files, and not to directories", "filesystem case sensitivity notes")
    reject(source, "forcing the path to lowercase", "filesystem case sensitivity notes")
    reject(build_os_path, "testPath.ToLower();", "BuildOSPath case-sensitive directory handling")
    reject(build_os_path, "Fixed up to %s", "BuildOSPath lowercase fallback")

    require(source, "FindCaseInsensitiveOSPathEntry", "filesystem case segment declarations")
    require(source, "ResolveCaseInsensitiveOSPath", "filesystem case segment declarations")

    require(finder, 'const char *extension = directoryOnly ? "/" : "";', "case-insensitive segment finder")
    require(finder, "Sys_ListFiles( listDirectory, extension, entries )", "case-insensitive segment finder")
    require(finder, "entries[i].Cmp( segment )", "case-insensitive segment finder")
    require(finder, "entries[i].Icmp( segment )", "case-insensitive segment finder")

    require(resolver, "resolvedPath.AppendPath( resolvedSegment );", "case-insensitive path resolver")
    require(resolver, "could not resolve %s segment", "case-insensitive path resolver diagnostics")
    require(resolver, "directoryOnly ? \"directory\" : \"file\"", "case-insensitive path resolver diagnostics")
    require(resolver, "ReplaceSeparators( resolvedPath );", "case-insensitive path resolver separator normalization")

    require(open_os_file, "ResolveCaseInsensitiveOSPath( fileName, resolvedFileName, true )", "OpenOSFile file case recovery")
    require(open_os_file, "fopen( resolvedFileName, mode )", "OpenOSFile resolved path open")
    require(open_os_file, "ResolveCaseInsensitiveOSPath( fpath.c_str(), resolvedPath, false )", "OpenOSFile directory case recovery")

    require(build_os_path, "hasUpperDirectory", "BuildOSPath uppercase directory tracking")
    require(build_os_path, "ResolveCaseInsensitiveOSPath( directoryPath.c_str(), resolvedDirectory, false )", "BuildOSPath directory resolver")

    require(list_os_files, "ret = Sys_ListFiles( directory, extension, list );", "ListOSFiles exact lookup")
    require(list_os_files, "ResolveCaseInsensitiveOSPath( directory, resolvedDirectory, false )", "ListOSFiles case fallback")
    require(list_os_files, "ret = Sys_ListFiles( cacheDirectory, extension, list );", "ListOSFiles resolved retry")
    require_order(list_os_files, "ret = Sys_ListFiles( directory, extension, list );", "ResolveCaseInsensitiveOSPath( directory, resolvedDirectory, false )", "ListOSFiles fallback order")


def validate_posix_directory_diagnostics() -> None:
    source = read("src/sys/posix/posix_main.cpp")
    list_files = function_body(source, "int Sys_ListFiles(")

    reject(list_files, "case sensitivity of directory path can screw us up", "POSIX directory listing")
    require(list_files, 'strerror( errno )', "POSIX directory listing diagnostics")


def validate_release_note() -> None:
    source = read("docs-dev/release-completion.md")

    require(source, "Loose-file lookups on case-sensitive Linux and macOS filesystems now resolve existing mixed-case directory segments", "release completion notes")
    require(source, "instead of assuming lowercase paths", "release completion notes")


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    for haystack, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "filesystem_case_segments.py", context)


def main() -> None:
    validate_filesystem_case_resolution()
    validate_posix_directory_diagnostics()
    validate_release_note()
    validate_validation_coverage()
    print("filesystem_case_segments: ok")


if __name__ == "__main__":
    main()
