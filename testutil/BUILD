# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "expr_printer",
    srcs = ["expr_printer.cc"],
    hdrs = ["expr_printer.h"],
    deps = [
        "//base/ast_internal:ast_impl",
        "//common:ast",
        "//common:constant",
        "//common:expr",
        "//extensions/protobuf:ast_converters",
        "//internal:strings",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_cc_proto",
    ],
)

cc_test(
    name = "expr_printer_test",
    srcs = ["expr_printer_test.cc"],
    deps = [
        ":expr_printer",
        "//common:expr",
        "//internal:testing",
        "//parser",
        "//parser:options",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "util",
    testonly = True,
    hdrs = [
        "util.h",
    ],
    deps = ["//internal:proto_matchers"],
)
