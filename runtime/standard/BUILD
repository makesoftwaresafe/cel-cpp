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

# Provides registrars for CEL standard definitions.
package(
    # Under active development, not yet being released.
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "comparison_functions",
    srcs = [
        "comparison_functions.cc",
    ],
    hdrs = [
        "comparison_functions.h",
    ],
    deps = [
        "//base:builtins",
        "//base:function_adapter",
        "//base:handle",
        "//base:value",
        "//internal:status_macros",
        "//runtime:function_registry",
        "//runtime:runtime_options",
        "//runtime/internal:number",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "comparison_functions_test",
    size = "small",
    srcs = [
        "comparison_functions_test.cc",
    ],
    deps = [
        ":comparison_functions",
        "//base:builtins",
        "//base:kind",
        "//internal:testing",
        "@com_google_absl//absl/strings",
    ],
)