// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_COMMON_NATIVE_TYPE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_NATIVE_TYPE_H_

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"  // IWYU pragma: keep
#include "absl/base/config.h"

#if ABSL_HAVE_FEATURE(cxx_rtti)
#define CEL_INTERNAL_HAVE_RTTI 1
#elif defined(__GNUC__) && defined(__GXX_RTTI)
#define CEL_INTERNAL_HAVE_RTTI 1
#elif defined(_MSC_VER) && defined(_CPPRTTI)
#define CEL_INTERNAL_HAVE_RTTI 1
#elif !defined(__GNUC__) && !defined(_MSC_VER)
#define CEL_INTERNAL_HAVE_RTTI 1
#endif

#ifdef CEL_INTERNAL_HAVE_RTTI
#include <typeinfo>
#endif

namespace cel {

class ABSL_ATTRIBUTE_TRIVIAL_ABI NativeTypeId final {
 public:
  template <typename T>
  static NativeTypeId For() {
    static_assert(!std::is_pointer_v<T>);
    static_assert(std::is_same_v<T, std::decay_t<T>>);
    static_assert(!std::is_same_v<NativeTypeId, std::decay_t<T>>);
#ifdef CEL_INTERNAL_HAVE_RTTI
    return NativeTypeId(&typeid(T));
#else
    // Adapted from Abseil and GTL. I believe this not being const is to ensure
    // the compiler does not merge multiple constants with the same value to
    // share the same address.
    static char rep;
    return NativeTypeId(&rep);
#endif
  }

  NativeTypeId() = default;
  NativeTypeId(const NativeTypeId&) = default;
  NativeTypeId& operator=(const NativeTypeId&) = default;

  std::string DebugString() const;

  friend bool operator==(NativeTypeId lhs, NativeTypeId rhs) {
#ifdef CEL_INTERNAL_HAVE_RTTI
    return lhs.rep_ == rhs.rep_ ||
           (lhs.rep_ != nullptr && rhs.rep_ != nullptr &&
            *lhs.rep_ == *rhs.rep_);
#else
    return lhs.rep_ == rhs.rep_;
#endif
  }

  friend bool operator!=(NativeTypeId lhs, NativeTypeId rhs) {
    return !operator==(lhs, rhs);
  }

  template <typename H>
  friend H AbslHashValue(H state, NativeTypeId id) {
#ifdef CEL_INTERNAL_HAVE_RTTI
    return H::combine(std::move(state),
                      id.rep_ != nullptr ? id.rep_->hash_code() : size_t{0});
#else
    return H::combine(std::move(state), absl::bit_cast<uintptr_t>(id.rep_));
#endif
  }

 private:
#ifdef CEL_INTERNAL_HAVE_RTTI
  constexpr explicit NativeTypeId(const std::type_info* rep) : rep_(rep) {}

  const std::type_info* rep_ = nullptr;
#else
  constexpr explicit NativeTypeId(const std::void* rep) : rep_(rep) {}

  const void* rep_ = nullptr;
#endif
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_NATIVE_TYPE_H_