// Copyright 2024 Google LLC
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

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/allocator.h"
#include "common/internal/reference_count.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "common/values/map_value_builder.h"
#include "eval/public/cel_value.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "internal/well_known_types.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

namespace common_internal {

namespace {

using ::cel::well_known_types::ListValueReflection;
using ::cel::well_known_types::StructReflection;
using ::cel::well_known_types::ValueReflection;
using ::google::api::expr::runtime::CelValue;

using TrivialValueVector =
    std::vector<TrivialValue, ArenaAllocator<TrivialValue>>;
using NonTrivialValueVector =
    std::vector<NonTrivialValue, NewDeleteAllocator<NonTrivialValue>>;

absl::Status CheckListElement(const Value& value) {
  if (auto error_value = value.AsError(); ABSL_PREDICT_FALSE(error_value)) {
    return error_value->NativeValue();
  }
  if (auto unknown_value = value.AsUnknown();
      ABSL_PREDICT_FALSE(unknown_value)) {
    return absl::InvalidArgumentError("cannot add unknown value to list");
  }
  return absl::OkStatus();
}

template <typename Vector>
absl::Status ListValueToJsonArray(
    const Vector& vector,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Message*> json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE);

  ListValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));

  json->Clear();

  if (vector.empty()) {
    return absl::OkStatus();
  }

  for (const auto& element : vector) {
    CEL_RETURN_IF_ERROR(element->ConvertToJson(descriptor_pool, message_factory,
                                               reflection.AddValues(json)));
  }
  return absl::OkStatus();
}

template <typename Vector>
absl::Status ListValueToJson(
    const Vector& vector,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Message*> json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  ValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));
  return ListValueToJsonArray(vector, descriptor_pool, message_factory,
                              reflection.MutableListValue(json));
}

template <typename T>
class ListValueImplIterator final : public ValueIterator {
 public:
  explicit ListValueImplIterator(absl::Span<const T> elements)
      : elements_(elements) {}

  bool HasNext() override { return index_ < elements_.size(); }

  absl::Status Next(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) override {
    if (ABSL_PREDICT_FALSE(index_ >= elements_.size())) {
      return absl::FailedPreconditionError(
          "ValueManager::Next called after ValueManager::HasNext returned "
          "false");
    }
    *result = *elements_[index_++];
    return absl::OkStatus();
  }

 private:
  const absl::Span<const T> elements_;
  size_t index_ = 0;
};

struct ValueFormatter {
  void operator()(
      std::string* out,
      const std::pair<const TrivialValue, TrivialValue>& value) const {
    (*this)(out, *value.first);
    out->append(": ");
    (*this)(out, *value.second);
  }

  void operator()(
      std::string* out,
      const std::pair<const NonTrivialValue, NonTrivialValue>& value) const {
    (*this)(out, *value.first);
    out->append(": ");
    (*this)(out, *value.second);
  }

  void operator()(std::string* out, const TrivialValue& value) const {
    (*this)(out, *value);
  }

  void operator()(std::string* out, const NonTrivialValue& value) const {
    (*this)(out, *value);
  }

  void operator()(std::string* out, const Value& value) const {
    out->append(value.DebugString());
  }
};

class TrivialListValueImpl final : public CompatListValue {
 public:
  explicit TrivialListValueImpl(TrivialValueVector&& elements)
      : elements_(std::move(elements)) {}

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJson(elements_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonArray(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomListValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueVector cloned_elements(elements_,
                                       ArenaAllocator<TrivialValue>{arena});
    return CustomListValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialListValueImpl>(
            std::move(cloned_elements)));
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, *elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<ListValueImplIterator<TrivialValue>>(
        absl::MakeConstSpan(elements_));
  }

  CelValue operator[](int index) const override {
    return Get(elements_.get_allocator().arena(), index);
  }

  // Like `operator[](int)` above, but also accepts an arena. Prefer calling
  // this variant if the arena is known.
  using CompatListValue::Get;
  CelValue Get(google::protobuf::Arena* arena, int index) const override {
    if (arena == nullptr) {
      arena = elements_.get_allocator().arena();
    }
    if (ABSL_PREDICT_FALSE(index < 0 || index >= size())) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, IndexOutOfBoundsError(index).NativeValue()));
    }
    return common_internal::LegacyTrivialValue(
        arena != nullptr ? arena : elements_.get_allocator().arena(),
        elements_[index]);
  }

  int size() const override { return static_cast<int>(Size()); }

 protected:
  absl::Status GetImpl(
      size_t index,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    *result = *elements_[index];
    return absl::OkStatus();
  }

 private:
  const TrivialValueVector elements_;
};

}  // namespace

}  // namespace common_internal

template <>
struct NativeTypeTraits<common_internal::TrivialListValueImpl> {
  static bool SkipDestructor(const common_internal::TrivialListValueImpl&) {
    return true;
  }
};

namespace common_internal {

namespace {

class NonTrivialListValueImpl final : public CustomListValueInterface {
 public:
  explicit NonTrivialListValueImpl(NonTrivialValueVector&& elements)
      : elements_(std::move(elements)) {}

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJson(elements_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonArray(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    TrivialValueVector cloned_elements(ArenaAllocator<TrivialValue>{arena});
    cloned_elements.reserve(elements_.size());
    for (const auto& element : elements_) {
      cloned_elements.emplace_back(MakeTrivialValue(*element, arena));
    }
    return CustomListValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialListValueImpl>(
            std::move(cloned_elements)));
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, *elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<ListValueImplIterator<NonTrivialValue>>(
        absl::MakeConstSpan(elements_));
  }

 protected:
  absl::Status GetImpl(
      size_t index,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    *result = *elements_[index];
    return absl::OkStatus();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<NonTrivialListValueImpl>();
  }

  const NonTrivialValueVector elements_;
};

class TrivialMutableListValueImpl final : public MutableCompatListValue {
 public:
  explicit TrivialMutableListValueImpl(absl::Nonnull<google::protobuf::Arena*> arena)
      : elements_(ArenaAllocator<TrivialValue>{arena}) {}

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJson(elements_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonArray(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomListValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueVector cloned_elements(elements_,
                                       ArenaAllocator<TrivialValue>{arena});
    return CustomListValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialListValueImpl>(
            std::move(cloned_elements)));
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, *elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<ListValueImplIterator<TrivialValue>>(
        absl::MakeConstSpan(elements_));
  }

  CelValue operator[](int index) const override {
    return Get(elements_.get_allocator().arena(), index);
  }

  // Like `operator[](int)` above, but also accepts an arena. Prefer calling
  // this variant if the arena is known.
  using MutableCompatListValue::Get;
  CelValue Get(google::protobuf::Arena* arena, int index) const override {
    if (arena == nullptr) {
      arena = elements_.get_allocator().arena();
    }
    if (ABSL_PREDICT_FALSE(index < 0 || index >= size())) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, IndexOutOfBoundsError(index).NativeValue()));
    }
    return common_internal::LegacyTrivialValue(
        arena != nullptr ? arena : elements_.get_allocator().arena(),
        elements_[index]);
  }

  int size() const override { return static_cast<int>(Size()); }

  absl::Status Append(Value value) const override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    elements_.emplace_back(
        MakeTrivialValue(value, elements_.get_allocator().arena()));
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { elements_.reserve(capacity); }

 protected:
  absl::Status GetImpl(
      size_t index,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    *result = *elements_[index];
    return absl::OkStatus();
  }

 private:
  mutable TrivialValueVector elements_;
};

}  // namespace

}  // namespace common_internal

template <>
struct NativeTypeTraits<common_internal::TrivialMutableListValueImpl> {
  static bool SkipDestructor(
      const common_internal::TrivialMutableListValueImpl&) {
    return true;
  }
};

namespace common_internal {

namespace {

class NonTrivialMutableListValueImpl final : public MutableListValue {
 public:
  NonTrivialMutableListValueImpl() = default;

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJson(elements_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonArray(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    TrivialValueVector cloned_elements(ArenaAllocator<TrivialValue>{arena});
    cloned_elements.reserve(elements_.size());
    for (const auto& element : elements_) {
      cloned_elements.emplace_back(MakeTrivialValue(*element, arena));
    }
    return CustomListValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialListValueImpl>(
            std::move(cloned_elements)));
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, *elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<ListValueImplIterator<NonTrivialValue>>(
        absl::MakeConstSpan(elements_));
  }

  absl::Status Append(Value value) const override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    elements_.emplace_back(std::move(value));
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { elements_.reserve(capacity); }

 protected:
  absl::Status GetImpl(
      size_t index,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    *result = *elements_[index];
    return absl::OkStatus();
  }

 private:
  mutable NonTrivialValueVector elements_;
};

class TrivialListValueBuilderImpl final : public ListValueBuilder {
 public:
  explicit TrivialListValueBuilderImpl(absl::Nonnull<google::protobuf::Arena*> arena)
      : arena_(arena), elements_(arena_) {}

  absl::Status Add(Value value) override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    elements_.emplace_back(
        MakeTrivialValue(value, elements_.get_allocator().arena()));
    return absl::OkStatus();
  }

  size_t Size() const override { return elements_.size(); }

  void Reserve(size_t capacity) override { elements_.reserve(capacity); }

  ListValue Build() && override {
    if (elements_.empty()) {
      return ListValue();
    }
    return CustomListValue(
        MemoryManager::Pooling(arena_).MakeShared<TrivialListValueImpl>(
            std::move(elements_)));
  }

 private:
  absl::Nonnull<google::protobuf::Arena*> const arena_;
  TrivialValueVector elements_;
};

class NonTrivialListValueBuilderImpl final : public ListValueBuilder {
 public:
  NonTrivialListValueBuilderImpl() = default;

  absl::Status Add(Value value) override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    elements_.emplace_back(std::move(value));
    return absl::OkStatus();
  }

  size_t Size() const override { return elements_.size(); }

  void Reserve(size_t capacity) override { elements_.reserve(capacity); }

  ListValue Build() && override {
    if (elements_.empty()) {
      return ListValue();
    }
    return CustomListValue(
        MemoryManager::ReferenceCounting().MakeShared<NonTrivialListValueImpl>(
            std::move(elements_)));
  }

 private:
  NonTrivialValueVector elements_;
};

}  // namespace

absl::StatusOr<absl::Nonnull<const CompatListValue*>> MakeCompatListValue(
    const CustomListValue& value,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena) {
  if (value.IsEmpty()) {
    return EmptyCompatListValue();
  }
  TrivialValueVector vector(ArenaAllocator<TrivialValue>{arena});
  vector.reserve(value.Size());
  CEL_RETURN_IF_ERROR(value.ForEach(
      [&](const Value& element) -> absl::StatusOr<bool> {
        CEL_RETURN_IF_ERROR(CheckListElement(element));
        vector.push_back(MakeTrivialValue(element, arena));
        return true;
      },
      descriptor_pool, message_factory, arena));
  return google::protobuf::Arena::Create<TrivialListValueImpl>(arena, std::move(vector));
}

Shared<MutableListValue> NewMutableListValue(Allocator<> allocator) {
  if (absl::Nullable<google::protobuf::Arena*> arena = allocator.arena();
      arena != nullptr) {
    return MemoryManager::Pooling(arena)
        .MakeShared<TrivialMutableListValueImpl>(arena);
  }
  return MemoryManager::ReferenceCounting()
      .MakeShared<NonTrivialMutableListValueImpl>();
}

bool IsMutableListValue(const Value& value) {
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_list_value);
    if (native_type_id == NativeTypeId::For<MutableListValue>() ||
        native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
      return true;
    }
  }
  return false;
}

bool IsMutableListValue(const ListValue& value) {
  if (auto custom_list_value = value.AsCustom(); custom_list_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_list_value);
    if (native_type_id == NativeTypeId::For<MutableListValue>() ||
        native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
      return true;
    }
  }
  return false;
}

absl::Nullable<const MutableListValue*> AsMutableListValue(const Value& value) {
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_list_value);
    if (native_type_id == NativeTypeId::For<MutableListValue>()) {
      return cel::internal::down_cast<const MutableListValue*>(
          (*custom_list_value).operator->());
    }
    if (native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
      return cel::internal::down_cast<const MutableCompatListValue*>(
          (*custom_list_value).operator->());
    }
  }
  return nullptr;
}

absl::Nullable<const MutableListValue*> AsMutableListValue(
    const ListValue& value) {
  if (auto custom_list_value = value.AsCustom(); custom_list_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_list_value);
    if (native_type_id == NativeTypeId::For<MutableListValue>()) {
      return cel::internal::down_cast<const MutableListValue*>(
          (*custom_list_value).operator->());
    }
    if (native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
      return cel::internal::down_cast<const MutableCompatListValue*>(
          (*custom_list_value).operator->());
    }
  }
  return nullptr;
}

const MutableListValue& GetMutableListValue(const Value& value) {
  ABSL_DCHECK(IsMutableListValue(value)) << value;
  const auto& custom_list_value = value.GetCustomList();
  NativeTypeId native_type_id = NativeTypeId::Of(*custom_list_value);
  if (native_type_id == NativeTypeId::For<MutableListValue>()) {
    return cel::internal::down_cast<const MutableListValue&>(
        *custom_list_value);
  }
  if (native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
    return cel::internal::down_cast<const MutableCompatListValue&>(
        *custom_list_value);
  }
  ABSL_UNREACHABLE();
}

const MutableListValue& GetMutableListValue(const ListValue& value) {
  ABSL_DCHECK(IsMutableListValue(value)) << value;
  const auto& custom_list_value = value.GetCustom();
  NativeTypeId native_type_id = NativeTypeId::Of(*custom_list_value);
  if (native_type_id == NativeTypeId::For<MutableListValue>()) {
    return cel::internal::down_cast<const MutableListValue&>(
        *custom_list_value);
  }
  if (native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
    return cel::internal::down_cast<const MutableCompatListValue&>(
        *custom_list_value);
  }
  ABSL_UNREACHABLE();
}

absl::Nonnull<cel::ListValueBuilderPtr> NewListValueBuilder(
    Allocator<> allocator) {
  if (absl::Nullable<google::protobuf::Arena*> arena = allocator.arena();
      arena != nullptr) {
    return std::make_unique<TrivialListValueBuilderImpl>(arena);
  }
  return std::make_unique<NonTrivialListValueBuilderImpl>();
}

}  // namespace common_internal

}  // namespace cel

namespace cel {

namespace common_internal {

namespace {

using ::google::api::expr::runtime::CelList;
using ::google::api::expr::runtime::CelValue;

absl::Status CheckMapValue(const Value& value) {
  if (auto error_value = value.AsError(); ABSL_PREDICT_FALSE(error_value)) {
    return error_value->NativeValue();
  }
  if (auto unknown_value = value.AsUnknown();
      ABSL_PREDICT_FALSE(unknown_value)) {
    return absl::InvalidArgumentError("cannot add unknown value to list");
  }
  return absl::OkStatus();
}

size_t ValueHash(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kBool:
      return absl::HashOf(value.kind(), value.GetBool());
    case ValueKind::kInt:
      return absl::HashOf(ValueKind::kInt, value.GetInt().NativeValue());
    case ValueKind::kUint:
      return absl::HashOf(ValueKind::kUint, value.GetUint().NativeValue());
    case ValueKind::kString:
      return absl::HashOf(value.kind(), value.GetString());
    default:
      ABSL_UNREACHABLE();
  }
}

size_t ValueHash(const CelValue& value) {
  switch (value.type()) {
    case CelValue::Type::kBool:
      return absl::HashOf(ValueKind::kBool, value.BoolOrDie());
    case CelValue::Type::kInt:
      return absl::HashOf(ValueKind::kInt, value.Int64OrDie());
    case CelValue::Type::kUint:
      return absl::HashOf(ValueKind::kUint, value.Uint64OrDie());
    case CelValue::Type::kString:
      return absl::HashOf(ValueKind::kString, value.StringOrDie().value());
    default:
      ABSL_UNREACHABLE();
  }
}

bool ValueEquals(const Value& lhs, const Value& rhs) {
  switch (lhs.kind()) {
    case ValueKind::kBool:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return lhs.GetBool() == rhs.GetBool();
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kInt:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return lhs.GetInt() == rhs.GetInt();
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kUint:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return lhs.GetUint() == rhs.GetUint();
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kString:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return lhs.GetString() == rhs.GetString();
        default:
          ABSL_UNREACHABLE();
      }
    default:
      ABSL_UNREACHABLE();
  }
}

bool CelValueEquals(const CelValue& lhs, const Value& rhs) {
  switch (lhs.type()) {
    case CelValue::Type::kBool:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return BoolValue(lhs.BoolOrDie()) == rhs.GetBool();
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case CelValue::Type::kInt:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return IntValue(lhs.Int64OrDie()) == rhs.GetInt();
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case CelValue::Type::kUint:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return UintValue(lhs.Uint64OrDie()) == rhs.GetUint();
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case CelValue::Type::kString:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return rhs.GetString().Equals(lhs.StringOrDie().value());
        default:
          ABSL_UNREACHABLE();
      }
    default:
      ABSL_UNREACHABLE();
  }
}

absl::StatusOr<std::string> ValueToJsonString(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kString:
      return value.GetString().NativeString();
    default:
      return TypeConversionError(value.GetRuntimeType(), StringType())
          .NativeValue();
  }
}

template <typename Map>
absl::Status MapValueToJsonObject(
    const Map& map,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Message*> json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

  StructReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));

  json->Clear();

  if (map.empty()) {
    return absl::OkStatus();
  }

  for (const auto& entry : map) {
    CEL_ASSIGN_OR_RETURN(auto key, ValueToJsonString(*entry.first));
    CEL_RETURN_IF_ERROR(entry.second->ConvertToJson(
        descriptor_pool, message_factory, reflection.InsertField(json, key)));
  }
  return absl::OkStatus();
}

template <typename Map>
absl::Status MapValueToJson(
    const Map& map,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Message*> json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  ValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));
  return MapValueToJsonObject(map, descriptor_pool, message_factory,
                              reflection.MutableStructValue(json));
}

template <typename T>
struct ValueHasher {
  using is_transparent = void;

  size_t operator()(const T& value) const { return (*this)(*value); }

  size_t operator()(const Value& value) const { return (ValueHash)(value); }

  size_t operator()(const CelValue& value) const { return (ValueHash)(value); }
};

template <typename T>
struct ValueEqualer {
  using is_transparent = void;

  bool operator()(const T& lhs, const T& rhs) const {
    return (*this)(*lhs, *rhs);
  }

  bool operator()(const T& lhs, const Value& rhs) const {
    return (*this)(*lhs, rhs);
  }

  bool operator()(const Value& lhs, const T& rhs) const {
    return (*this)(lhs, *rhs);
  }

  bool operator()(const T& lhs, const CelValue& rhs) const {
    return (*this)(rhs, lhs);
  }

  bool operator()(const CelValue& lhs, const T& rhs) const {
    return (CelValueEquals)(lhs, *rhs);
  }

  bool operator()(const Value& lhs, const Value& rhs) const {
    return (ValueEquals)(lhs, rhs);
  }
};

template <typename T>
struct SelectValueFlatHashMapAllocator;

template <>
struct SelectValueFlatHashMapAllocator<TrivialValue> {
  using type = ArenaAllocator<std::pair<const TrivialValue, TrivialValue>>;
};

template <>
struct SelectValueFlatHashMapAllocator<NonTrivialValue> {
  using type =
      NewDeleteAllocator<std::pair<const NonTrivialValue, NonTrivialValue>>;
};

template <typename T>
using ValueFlatHashMapAllocator =
    typename SelectValueFlatHashMapAllocator<T>::type;

template <typename T>
using ValueFlatHashMap =
    absl::flat_hash_map<T, T, ValueHasher<T>, ValueEqualer<T>,
                        ValueFlatHashMapAllocator<T>>;

using TrivialValueFlatHashMapAllocator =
    ValueFlatHashMapAllocator<TrivialValue>;
using NonTrivialValueFlatHashMapAllocator =
    ValueFlatHashMapAllocator<NonTrivialValue>;

using TrivialValueFlatHashMap = ValueFlatHashMap<TrivialValue>;
using NonTrivialValueFlatHashMap = ValueFlatHashMap<NonTrivialValue>;

template <typename T>
class MapValueImplIterator final : public ValueIterator {
 public:
  explicit MapValueImplIterator(absl::Nonnull<const ValueFlatHashMap<T>*> map)
      : begin_(map->begin()), end_(map->end()) {}

  bool HasNext() override { return begin_ != end_; }

  absl::Status Next(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) override {
    if (ABSL_PREDICT_FALSE(begin_ == end_)) {
      return absl::FailedPreconditionError(
          "ValueManager::Next called after ValueManager::HasNext returned "
          "false");
    }
    *result = *begin_->first;
    ++begin_;
    return absl::OkStatus();
  }

 private:
  typename ValueFlatHashMap<T>::const_iterator begin_;
  const typename ValueFlatHashMap<T>::const_iterator end_;
};

class TrivialMapValueImpl final : public CompatMapValue {
 public:
  explicit TrivialMapValueImpl(TrivialValueFlatHashMap&& map)
      : map_(std::move(map)) {}

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJson(map_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomMapValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueFlatHashMap cloned_entries(map_,
                                           ArenaAllocator<TrivialValue>{arena});
    return CustomMapValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialMapValueImpl>(
            std::move(cloned_entries)));
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<ListValue*> result) const override {
    *result = CustomListValue(MakeShared(kAdoptRef, ProjectKeys(), nullptr));
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(*entry.first, *entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<MapValueImplIterator<TrivialValue>>(&map_);
  }

  absl::optional<CelValue> operator[](CelValue key) const override {
    return Get(map_.get_allocator().arena(), key);
  }

  using CompatMapValue::Get;
  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    if (auto status = CelValue::CheckMapKeyType(key); !status.ok()) {
      status.IgnoreError();
      return absl::nullopt;
    }
    if (auto it = map_.find(key); it != map_.end()) {
      return LegacyTrivialValue(
          arena != nullptr ? arena : map_.get_allocator().arena(), it->second);
    }
    return absl::nullopt;
  }

  using CompatMapValue::Has;
  absl::StatusOr<bool> Has(const CelValue& key) const override {
    // This check safeguards against issues with invalid key types such as NaN.
    CEL_RETURN_IF_ERROR(CelValue::CheckMapKeyType(key));
    return map_.find(key) != map_.end();
  }

  int size() const override { return static_cast<int>(Size()); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return ProjectKeys();
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena* arena) const override {
    return ProjectKeys();
  }

 protected:
  absl::StatusOr<bool> FindImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = *it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  absl::Nonnull<const CompatListValue*> ProjectKeys() const {
    absl::call_once(keys_once_, [this]() {
      TrivialValueVector elements(map_.get_allocator().arena());
      elements.reserve(map_.size());
      for (const auto& entry : map_) {
        elements.push_back(entry.first);
      }
      ::new (static_cast<void*>(&keys_[0]))
          TrivialListValueImpl(std::move(elements));
    });
    return std::launder(
        reinterpret_cast<const TrivialListValueImpl*>(&keys_[0]));
  }

  const TrivialValueFlatHashMap map_;
  mutable absl::once_flag keys_once_;
  alignas(
      TrivialListValueImpl) mutable char keys_[sizeof(TrivialListValueImpl)];
};

}  // namespace

}  // namespace common_internal

template <>
struct NativeTypeTraits<common_internal::TrivialMapValueImpl> {
  static bool SkipDestructor(const common_internal::TrivialMapValueImpl&) {
    return true;
  }
};

namespace common_internal {

namespace {

class NonTrivialMapValueImpl final : public CustomMapValueInterface {
 public:
  explicit NonTrivialMapValueImpl(NonTrivialValueFlatHashMap&& map)
      : map_(std::move(map)) {}

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJson(map_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomMapValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueFlatHashMap cloned_entries(ArenaAllocator<TrivialValue>{arena});
    cloned_entries.reserve(map_.size());
    for (const auto& entry : map_) {
      const auto inserted =
          cloned_entries
              .insert_or_assign(MakeTrivialValue(*entry.first, arena),
                                MakeTrivialValue(*entry.second, arena))
              .second;
      ABSL_DCHECK(inserted);
    }
    return CustomMapValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialMapValueImpl>(
            std::move(cloned_entries)));
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<ListValue*> result) const override {
    auto builder = NewListValueBuilder(arena);
    builder->Reserve(Size());
    for (const auto& entry : map_) {
      CEL_RETURN_IF_ERROR(builder->Add(*entry.first));
    }
    *result = std::move(*builder).Build();
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(*entry.first, *entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<MapValueImplIterator<NonTrivialValue>>(&map_);
  }

 protected:
  absl::StatusOr<bool> FindImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = *it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<NonTrivialMapValueImpl>();
  }

  const NonTrivialValueFlatHashMap map_;
};

class TrivialMutableMapValueImpl final : public MutableCompatMapValue {
 public:
  explicit TrivialMutableMapValueImpl(absl::Nonnull<google::protobuf::Arena*> arena)
      : map_(TrivialValueFlatHashMapAllocator{arena}) {}

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJson(map_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomMapValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueFlatHashMap cloned_entries(map_,
                                           ArenaAllocator<TrivialValue>{arena});
    return CustomMapValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialMapValueImpl>(
            std::move(cloned_entries)));
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<ListValue*> result) const override {
    *result = CustomListValue(MakeShared(kAdoptRef, ProjectKeys(), nullptr));
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(*entry.first, *entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<MapValueImplIterator<TrivialValue>>(&map_);
  }

  absl::optional<CelValue> operator[](CelValue key) const override {
    return Get(map_.get_allocator().arena(), key);
  }

  using MutableCompatMapValue::Get;
  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    if (auto status = CelValue::CheckMapKeyType(key); !status.ok()) {
      status.IgnoreError();
      return absl::nullopt;
    }
    if (auto it = map_.find(key); it != map_.end()) {
      return LegacyTrivialValue(
          arena != nullptr ? arena : map_.get_allocator().arena(), it->second);
    }
    return absl::nullopt;
  }

  using MutableCompatMapValue::Has;
  absl::StatusOr<bool> Has(const CelValue& key) const override {
    // This check safeguards against issues with invalid key types such as NaN.
    CEL_RETURN_IF_ERROR(CelValue::CheckMapKeyType(key));
    return map_.find(key) != map_.end();
  }

  int size() const override { return static_cast<int>(Size()); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return ProjectKeys();
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena* arena) const override {
    return ProjectKeys();
  }

  absl::Status Put(Value key, Value value) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto it = map_.find(key); ABSL_PREDICT_FALSE(it != map_.end())) {
      return DuplicateKeyError().NativeValue();
    }
    absl::Nonnull<google::protobuf::Arena*> arena = map_.get_allocator().arena();
    auto inserted = map_.insert(std::pair{MakeTrivialValue(key, arena),
                                          MakeTrivialValue(value, arena)})
                        .second;
    ABSL_DCHECK(inserted);
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { map_.reserve(capacity); }

 protected:
  absl::StatusOr<bool> FindImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = *it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  absl::Nonnull<const CompatListValue*> ProjectKeys() const {
    absl::call_once(keys_once_, [this]() {
      TrivialValueVector elements(map_.get_allocator().arena());
      elements.reserve(map_.size());
      for (const auto& entry : map_) {
        elements.push_back(entry.first);
      }
      ::new (static_cast<void*>(&keys_[0]))
          TrivialListValueImpl(std::move(elements));
    });
    return std::launder(
        reinterpret_cast<const TrivialListValueImpl*>(&keys_[0]));
  }

  mutable TrivialValueFlatHashMap map_;
  mutable absl::once_flag keys_once_;
  alignas(
      TrivialListValueImpl) mutable char keys_[sizeof(TrivialListValueImpl)];
};

}  // namespace

}  // namespace common_internal

template <>
struct NativeTypeTraits<common_internal::TrivialMutableMapValueImpl> {
  static bool SkipDestructor(
      const common_internal::TrivialMutableMapValueImpl&) {
    return true;
  }
};

namespace common_internal {

namespace {

class NonTrivialMutableMapValueImpl final : public MutableMapValue {
 public:
  NonTrivialMutableMapValueImpl() = default;

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJson(map_, descriptor_pool, message_factory, json);
  }

  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(absl::Nonnull<google::protobuf::Arena*> arena) const override {
    // This is unreachable with the current logic in CustomMapValue, but could
    // be called once we keep track of the owning arena in CustomListValue.
    TrivialValueFlatHashMap cloned_entries(ArenaAllocator<TrivialValue>{arena});
    cloned_entries.reserve(map_.size());
    for (const auto& entry : map_) {
      const auto inserted =
          cloned_entries
              .insert_or_assign(MakeTrivialValue(*entry.first, arena),
                                MakeTrivialValue(*entry.second, arena))
              .second;
      ABSL_DCHECK(inserted);
    }
    return CustomMapValue(
        MemoryManager::Pooling(arena).MakeShared<TrivialMapValueImpl>(
            std::move(cloned_entries)));
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<ListValue*> result) const override {
    auto builder = NewListValueBuilder(arena);
    builder->Reserve(Size());
    for (const auto& entry : map_) {
      CEL_RETURN_IF_ERROR(builder->Add(*entry.first));
    }
    *result = std::move(*builder).Build();
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(*entry.first, *entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return std::make_unique<MapValueImplIterator<NonTrivialValue>>(&map_);
  }

  absl::Status Put(Value key, Value value) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto inserted =
            map_.insert(std::pair{NonTrivialValue(std::move(key)),
                                  NonTrivialValue(std::move(value))})
                .second;
        !inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { map_.reserve(capacity); }

 protected:
  absl::StatusOr<bool> FindImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = *it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  mutable NonTrivialValueFlatHashMap map_;
};

class TrivialMapValueBuilderImpl final : public MapValueBuilder {
 public:
  explicit TrivialMapValueBuilderImpl(absl::Nonnull<google::protobuf::Arena*> arena)
      : arena_(arena), map_(arena_) {}

  absl::Status Put(Value key, Value value) override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto it = map_.find(key); ABSL_PREDICT_FALSE(it != map_.end())) {
      return DuplicateKeyError().NativeValue();
    }
    absl::Nonnull<google::protobuf::Arena*> arena = map_.get_allocator().arena();
    auto inserted = map_.insert(std::pair{MakeTrivialValue(key, arena),
                                          MakeTrivialValue(value, arena)})
                        .second;
    ABSL_DCHECK(inserted);
    return absl::OkStatus();
  }

  size_t Size() const override { return map_.size(); }

  void Reserve(size_t capacity) override { map_.reserve(capacity); }

  MapValue Build() && override {
    if (map_.empty()) {
      return MapValue();
    }
    return CustomMapValue(
        MemoryManager::Pooling(arena_).MakeShared<TrivialMapValueImpl>(
            std::move(map_)));
  }

 private:
  absl::Nonnull<google::protobuf::Arena*> const arena_;
  TrivialValueFlatHashMap map_;
};

class NonTrivialMapValueBuilderImpl final : public MapValueBuilder {
 public:
  NonTrivialMapValueBuilderImpl() = default;

  absl::Status Put(Value key, Value value) override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto inserted =
            map_.insert(std::pair{NonTrivialValue(std::move(key)),
                                  NonTrivialValue(std::move(value))})
                .second;
        !inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  size_t Size() const override { return map_.size(); }

  void Reserve(size_t capacity) override { map_.reserve(capacity); }

  MapValue Build() && override {
    if (map_.empty()) {
      return MapValue();
    }
    return CustomMapValue(
        MemoryManager::ReferenceCounting().MakeShared<NonTrivialMapValueImpl>(
            std::move(map_)));
  }

 private:
  NonTrivialValueFlatHashMap map_;
};

}  // namespace

absl::StatusOr<absl::Nonnull<const CompatMapValue*>> MakeCompatMapValue(
    const CustomMapValue& value,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena) {
  if (value.IsEmpty()) {
    return EmptyCompatMapValue();
  }
  TrivialValueFlatHashMap map(TrivialValueFlatHashMapAllocator{arena});
  map.reserve(value.Size());
  CEL_RETURN_IF_ERROR(value.ForEach(
      [&](const Value& key, const Value& value) -> absl::StatusOr<bool> {
        CEL_RETURN_IF_ERROR(CheckMapKey(key));
        CEL_RETURN_IF_ERROR(CheckMapValue(value));
        const auto inserted =
            map.insert_or_assign(MakeTrivialValue(key, arena),
                                 MakeTrivialValue(value, arena))
                .second;
        ABSL_DCHECK(inserted);
        return true;
      },
      descriptor_pool, message_factory, arena));
  return google::protobuf::Arena::Create<TrivialMapValueImpl>(arena, std::move(map));
}

Shared<MutableMapValue> NewMutableMapValue(Allocator<> allocator) {
  if (absl::Nullable<google::protobuf::Arena*> arena = allocator.arena();
      arena != nullptr) {
    return MemoryManager::Pooling(arena).MakeShared<TrivialMutableMapValueImpl>(
        arena);
  }
  return MemoryManager::ReferenceCounting()
      .MakeShared<NonTrivialMutableMapValueImpl>();
}

bool IsMutableMapValue(const Value& value) {
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_map_value);
    if (native_type_id == NativeTypeId::For<MutableMapValue>() ||
        native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
      return true;
    }
  }
  return false;
}

bool IsMutableMapValue(const MapValue& value) {
  if (auto custom_map_value = value.AsCustom(); custom_map_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_map_value);
    if (native_type_id == NativeTypeId::For<MutableMapValue>() ||
        native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
      return true;
    }
  }
  return false;
}

absl::Nullable<const MutableMapValue*> AsMutableMapValue(const Value& value) {
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_map_value);
    if (native_type_id == NativeTypeId::For<MutableMapValue>()) {
      return cel::internal::down_cast<const MutableMapValue*>(
          (*custom_map_value).operator->());
    }
    if (native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
      return cel::internal::down_cast<const MutableCompatMapValue*>(
          (*custom_map_value).operator->());
    }
  }
  return nullptr;
}

absl::Nullable<const MutableMapValue*> AsMutableMapValue(
    const MapValue& value) {
  if (auto custom_map_value = value.AsCustom(); custom_map_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(**custom_map_value);
    if (native_type_id == NativeTypeId::For<MutableMapValue>()) {
      return cel::internal::down_cast<const MutableMapValue*>(
          (*custom_map_value).operator->());
    }
    if (native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
      return cel::internal::down_cast<const MutableCompatMapValue*>(
          (*custom_map_value).operator->());
    }
  }
  return nullptr;
}

const MutableMapValue& GetMutableMapValue(const Value& value) {
  ABSL_DCHECK(IsMutableMapValue(value)) << value;
  const auto& custom_map_value = value.GetCustomMap();
  NativeTypeId native_type_id = NativeTypeId::Of(*custom_map_value);
  if (native_type_id == NativeTypeId::For<MutableMapValue>()) {
    return cel::internal::down_cast<const MutableMapValue&>(*custom_map_value);
  }
  if (native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
    return cel::internal::down_cast<const MutableCompatMapValue&>(
        *custom_map_value);
  }
  ABSL_UNREACHABLE();
}

const MutableMapValue& GetMutableMapValue(const MapValue& value) {
  ABSL_DCHECK(IsMutableMapValue(value)) << value;
  const auto& custom_map_value = value.GetCustom();
  NativeTypeId native_type_id = NativeTypeId::Of(*custom_map_value);
  if (native_type_id == NativeTypeId::For<MutableMapValue>()) {
    return cel::internal::down_cast<const MutableMapValue&>(*custom_map_value);
  }
  if (native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
    return cel::internal::down_cast<const MutableCompatMapValue&>(
        *custom_map_value);
  }
  ABSL_UNREACHABLE();
}

absl::Nonnull<cel::MapValueBuilderPtr> NewMapValueBuilder(
    Allocator<> allocator) {
  if (absl::Nullable<google::protobuf::Arena*> arena = allocator.arena();
      arena != nullptr) {
    return std::make_unique<TrivialMapValueBuilderImpl>(arena);
  }
  return std::make_unique<NonTrivialMapValueBuilderImpl>();
}

}  // namespace common_internal

}  // namespace cel
