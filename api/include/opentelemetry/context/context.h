// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/context/context_value.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace context
{

// The context class provides a context identifier. Is built as a linked list
// of DataList nodes and each context holds a shared_ptr to a place within
// the list that determines which keys and values it has access to. All that
// come before and none that come after.
class Context
{

public:
  Context() = default;
  // Creates a context object from a map of keys and identifiers, this will
  // hold a shared_ptr to the head of the DataList linked list
  template <class T>
  Context(const T &keys_and_values) noexcept
      : head_{std::make_shared<DataList>(keys_and_values)}
  {}

  // Creates a context object from a key and value, this will
  // hold a shared_ptr to the head of the DataList linked list
  Context(nostd::string_view key, ContextValue value) noexcept
      : head_{std::make_shared<DataList>(key, std::move(value))}
  {}

  // Accepts a new iterable and then returns a new context that
  // contains the new key and value data. It attaches the
  // exisiting list to the end of the new list.
  template <class T>
  Context SetValues(T &values) noexcept
  {
    Context context(values);
    nostd::shared_ptr<DataList> last = context.head_;
    while (last->next_ != nullptr)
    {
      last = last->next_;
    }
    last->next_ = head_;
    return context;
  }

  // Accepts a new iterable and then returns a new context that
  // contains the new key and value data. It attaches the
  // exisiting list to the end of the new list.
  Context SetValue(nostd::string_view key, ContextValue value) noexcept
  {
    Context context(key, std::move(value));
    context.head_->next_ = head_;
    return context;
  }

  // Returns the value associated with the passed in key.
  context::ContextValue GetValue(const nostd::string_view key) const noexcept
  {
    for (DataList *data = head_.get(); data != nullptr; data = data->next_.get())
    {
      if (key.size() == data->key_.size())
      {
        if (std::memcmp(key.data(), data->key_.data(), key.size()) == 0)
        {
          return data->value_;
        }
      }
    }
    return ContextValue{};
  }

  // Returns a pointer to the value associated with the passed in key, or nullptr if the key is not
  // found
  const context::ContextValue *GetValuePtr(const nostd::string_view key) const noexcept
  {
    for (DataList *data = head_.get(); data != nullptr; data = data->next_.get())
    {
      if (key.size() == data->key_.size())
      {
        if (std::memcmp(key.data(), data->key_.data(), key.size()) == 0)
        {
          return &data->value_;
        }
      }
    }
    return nullptr;
  }

  // Checks for key and returns true if found
  bool HasKey(const nostd::string_view key) const noexcept
  {
    return GetValuePtr(key) != nullptr;
  }

  bool operator==(const Context &other) const noexcept { return (head_ == other.head_); }

private:
  // A linked list to contain the keys and values of this context node
  struct DataList
  {
    nostd::shared_ptr<DataList> next_{nullptr};
    std::string key_{};
    ContextValue value_;

    DataList() = default;

    // Builds a data list off of a key and value iterable and returns the head
    template <class T>
    DataList(const T &keys_and_vals)
    {
      bool first = true;
      auto *node = this;
      for (const auto &iter : keys_and_vals)
      {
        if (first)
        {
          *node = DataList(iter.first, iter.second);
          first = false;
        }
        else
        {
          node->next_ = std::make_shared<DataList>(iter.first, iter.second);
          node        = node->next_.get();
        }
      }
    }

    // Builds a data list with just a key and value.
    DataList(nostd::string_view key, ContextValue value)
        : key_{key.data(), key.size()}, value_{std::move(value)}
    {}

    DataList(const DataList &)            = delete;
    DataList(DataList &&)                 = delete;
    DataList &operator=(const DataList &) = delete;

    // Move assignment is used by DataList(const T &keys_and_vals) when
    // initialising the head node in-place.
    DataList &operator=(DataList &&other) noexcept
    {
      key_   = std::move(other.key_);
      value_ = std::move(other.value_);
      next_  = std::move(other.next_);
      return *this;
    }
  };

  // Head of the list which holds the keys and values of this context
  nostd::shared_ptr<DataList> head_;
};
}  // namespace context
OPENTELEMETRY_END_NAMESPACE
