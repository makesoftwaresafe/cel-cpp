"""
Main dependencies of cel-cpp.
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_jar")

def base_deps():
    """Base evaluator and test dependencies."""

    # Abseil LTS 20240116.2
    ABSL_SHA1 = "d7aaad83b488fd62bd51c81ecf16cd938532cc0a"
    ABSL_SHA256 = "68e7d36d621769ab500b2ebeec6a7910420566874b4b33b340a04bd70e67fe43"
    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/" + ABSL_SHA1 + ".zip"],
        strip_prefix = "abseil-cpp-" + ABSL_SHA1,
        sha256 = ABSL_SHA256,
    )

    # v1.14.0
    GOOGLETEST_SHA1 = "f8d7d77c06936315286eb55f8de22cd23c188571"
    GOOGLETEST_SHA256 = "b976cf4fd57b318afdb1bdb27fc708904b3e4bed482859eb94ba2b4bdd077fe2"
    http_archive(
        name = "com_google_googletest",
        urls = ["https://github.com/google/googletest/archive/" + GOOGLETEST_SHA1 + ".zip"],
        strip_prefix = "googletest-" + GOOGLETEST_SHA1,
        sha256 = GOOGLETEST_SHA256,
    )

    # v1.6.0
    BENCHMARK_SHA1 = "f91b6b42b1b9854772a90ae9501464a161707d1e"
    BENCHMARK_SHA256 = "00bd0837db9266c758a087cdf0831a0d3e337c6bb9e3fad75d2be4f9bf480d95"
    http_archive(
        name = "com_github_google_benchmark",
        urls = ["https://github.com/google/benchmark/archive/" + BENCHMARK_SHA1 + ".zip"],
        strip_prefix = "benchmark-" + BENCHMARK_SHA1,
        sha256 = BENCHMARK_SHA256,
    )

    # 2024-02-01
    RE2_SHA1 = "9665465b69ab699279ef9fb9454559d90fed1d76"
    RE2_SHA256 = "dcd82922c7a1d3b7c2a147c045585a9f76066f9c0269a06b857eccbbf6f96dba"
    http_archive(
        name = "com_googlesource_code_re2",
        urls = ["https://github.com/google/re2/archive/" + RE2_SHA1 + ".zip"],
        strip_prefix = "re2-" + RE2_SHA1,
        sha256 = RE2_SHA256,
    )

    PROTOBUF_SHA1 = "32bedd9b8a794231ca43d6457bb6b8cd3503fca7"
    PROTOBUF_SHA256 = "77564633c7508ab2c1b8f07d02afe0ddc08c07bd2ca6a3aa3d56cde0e21143a2"
    http_archive(
        name = "com_google_protobuf",
        sha256 = PROTOBUF_SHA256,
        strip_prefix = "protobuf-" + PROTOBUF_SHA1,
        urls = ["https://github.com/protocolbuffers/protobuf/archive/" + PROTOBUF_SHA1 + ".zip"],
    )

    GOOGLEAPIS_GIT_SHA = "6eb56cdf5f54f70d0dbfce051add28a35c1203ce"  # June 26, 2024
    GOOGLEAPIS_SHA = "6321a7eac9e5280e7abca07ddf2cab9179cbd49a6828c26f4c7c73d5a45f39ad"
    http_archive(
        name = "com_google_googleapis",
        sha256 = GOOGLEAPIS_SHA,
        strip_prefix = "googleapis-" + GOOGLEAPIS_GIT_SHA,
        urls = ["https://github.com/googleapis/googleapis/archive/" + GOOGLEAPIS_GIT_SHA + ".tar.gz"],
    )

    http_archive(
        name = "rules_cc",
        urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.0.10-rc1/rules_cc-0.0.10-rc1.tar.gz"],
        sha256 = "d75a040c32954da0d308d3f2ea2ba735490f49b3a7aa3e4b40259ca4b814f825",
    )

    http_archive(
        name = "rules_proto",
        sha256 = "6fb6767d1bef535310547e03247f7518b03487740c11b6c6adb7952033fe1295",
        strip_prefix = "rules_proto-6.0.2",
        url = "https://github.com/bazelbuild/rules_proto/releases/download/6.0.2/rules_proto-6.0.2.tar.gz",
    )

def parser_deps():
    """ANTLR dependency for the parser."""

    # Sept 4, 2023
    ANTLR4_VERSION = "4.13.1"

    http_archive(
        name = "antlr4_runtimes",
        build_file_content = """
package(default_visibility = ["//visibility:public"])
cc_library(
    name = "cpp",
    srcs = glob(["runtime/Cpp/runtime/src/**/*.cpp"]),
    hdrs = glob(["runtime/Cpp/runtime/src/**/*.h"]),
    defines = ["ANTLR4CPP_USING_ABSEIL"],
    includes = ["runtime/Cpp/runtime/src"],
    deps = [
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/synchronization",
    ],
)
  """,
        sha256 = "365ff6aec0b1612fb964a763ca73748d80e0b3379cbdd9f82d86333eb8ae4638",
        strip_prefix = "antlr4-" + ANTLR4_VERSION,
        urls = ["https://github.com/antlr/antlr4/archive/refs/tags/" + ANTLR4_VERSION + ".zip"],
    )
    http_jar(
        name = "antlr4_jar",
        urls = ["https://www.antlr.org/download/antlr-" + ANTLR4_VERSION + "-complete.jar"],
        sha256 = "bc13a9c57a8dd7d5196888211e5ede657cb64a3ce968608697e4f668251a8487",
    )

def flatbuffers_deps():
    """FlatBuffers support."""
    FLAT_BUFFERS_SHA = "a83caf5910644ba1c421c002ef68e42f21c15f9f"
    http_archive(
        name = "com_github_google_flatbuffers",
        sha256 = "b8efbc25721e76780752bad775a97c3f77a0250271e2db37fc747b20e8b0f24a",
        strip_prefix = "flatbuffers-" + FLAT_BUFFERS_SHA,
        url = "https://github.com/google/flatbuffers/archive/" + FLAT_BUFFERS_SHA + ".tar.gz",
    )

def cel_spec_deps():
    """CEL Spec conformance testing."""
    http_archive(
        name = "io_bazel_rules_go",
        sha256 = "b2038e2de2cace18f032249cb4bb0048abf583a36369fa98f687af1b3f880b26",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.48.1/rules_go-v0.48.1.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.48.1/rules_go-v0.48.1.zip",
        ],
    )

    http_archive(
        name = "rules_python",
        sha256 = "e3f1cc7a04d9b09635afb3130731ed82b5f58eadc8233d4efb59944d92ffc06f",
        strip_prefix = "rules_python-0.33.2",
        url = "https://github.com/bazelbuild/rules_python/releases/download/0.33.2/rules_python-0.33.2.tar.gz",
    )

    CEL_SPEC_GIT_SHA = "5299974f1c69103e4bb4eec48f7d9b24413ca3c7"  # Jul 19, 2024
    http_archive(
        name = "com_google_cel_spec",
        sha256 = "2beb97d2d8fff4db659f0633d0e432fdb7d328fe9c39061eb142af5dbb6eaea0",
        strip_prefix = "cel-spec-" + CEL_SPEC_GIT_SHA,
        urls = ["https://github.com/google/cel-spec/archive/" + CEL_SPEC_GIT_SHA + ".zip"],
    )

def cel_cpp_deps():
    """All core dependencies of cel-cpp."""
    base_deps()
    parser_deps()
    flatbuffers_deps()
    cel_spec_deps()
