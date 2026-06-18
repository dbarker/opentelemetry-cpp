// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/context/stack_frame_context_storage.h"

#include <gtest/gtest.h>
#include <stdint.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "opentelemetry/context/context.h"
#include "opentelemetry/context/context_value.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/nostd/variant.h"

using namespace opentelemetry::context;
using opentelemetry::nostd::shared_ptr;

class StackFrameContextStorageTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    RuntimeContext::SetRuntimeContextStorage(
        shared_ptr<RuntimeContextStorage>(new StackFrameContextStorage()));
  }
};

// Empty context returned before any attach
TEST_F(StackFrameContextStorageTest, GetCurrentEmpty)
{
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});
}

// GetCurrent returns the most recently attached context
TEST_F(StackFrameContextStorageTest, GetCurrent)
{
  Context ctx(std::string("k"), ContextValue{int64_t{42}});
  auto token = RuntimeContext::Attach(ctx);
  EXPECT_EQ(RuntimeContext::GetCurrent(), ctx);
}

// Detach restores previous context
TEST_F(StackFrameContextStorageTest, Detach)
{
  Context ctx1(std::string("k"), ContextValue{int64_t{1}});
  Context ctx2(std::string("k"), ContextValue{int64_t{2}});

  auto t1 = RuntimeContext::Attach(ctx1);
  auto t2 = RuntimeContext::Attach(ctx2);

  EXPECT_TRUE(RuntimeContext::Detach(*t2));
  EXPECT_EQ(RuntimeContext::GetCurrent(), ctx1);

  EXPECT_TRUE(RuntimeContext::Detach(*t1));
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});
}

// Detach returns false when called a second time on same token
TEST_F(StackFrameContextStorageTest, DetachTwiceReturnsFalse)
{
  Context ctx(std::string("k"), ContextValue{int64_t{1}});
  auto token = RuntimeContext::Attach(ctx);
  EXPECT_TRUE(RuntimeContext::Detach(*token));
  EXPECT_FALSE(RuntimeContext::Detach(*token));
}

// Detach returns false for a token not on the stack
TEST_F(StackFrameContextStorageTest, DetachWrongToken)
{
  Context ctx(std::string("k"), ContextValue{int64_t{1}});
  auto token = RuntimeContext::Attach(ctx);
  EXPECT_TRUE(RuntimeContext::Detach(*token));
  EXPECT_FALSE(RuntimeContext::Detach(*token));
}

// Three-level attach/detach in order
TEST_F(StackFrameContextStorageTest, ThreeLevelLifo)
{
  Context c1(std::string("n"), ContextValue{int64_t{1}});
  Context c2(std::string("n"), ContextValue{int64_t{2}});
  Context c3(std::string("n"), ContextValue{int64_t{3}});

  auto t1 = RuntimeContext::Attach(c1);
  auto t2 = RuntimeContext::Attach(c2);
  auto t3 = RuntimeContext::Attach(c3);

  EXPECT_TRUE(RuntimeContext::Detach(*t3));
  EXPECT_EQ(RuntimeContext::GetCurrent(), c2);
  EXPECT_TRUE(RuntimeContext::Detach(*t2));
  EXPECT_EQ(RuntimeContext::GetCurrent(), c1);
  EXPECT_TRUE(RuntimeContext::Detach(*t1));
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});
}

// Out-of-order detach pops everything down to that depth
TEST_F(StackFrameContextStorageTest, DetachOutOfOrder)
{
  Context c1(std::string("n"), ContextValue{int64_t{1}});
  Context c2(std::string("n"), ContextValue{int64_t{2}});
  Context c3(std::string("n"), ContextValue{int64_t{3}});

  auto t1 = RuntimeContext::Attach(c1);
  auto t2 = RuntimeContext::Attach(c2);
  auto t3 = RuntimeContext::Attach(c3);

  // Detach t1 out of order — should pop all three
  EXPECT_TRUE(RuntimeContext::Detach(*t1));
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});

  // t2 and t3 are now stale
  EXPECT_FALSE(RuntimeContext::Detach(*t2));
  EXPECT_FALSE(RuntimeContext::Detach(*t3));
}

// Token destructor auto-detaches (RAII pattern)
TEST_F(StackFrameContextStorageTest, TokenRaii)
{
  Context ctx(std::string("k"), ContextValue{int64_t{7}});
  {
    auto token = RuntimeContext::Attach(ctx);
    EXPECT_EQ(RuntimeContext::GetCurrent(), ctx);
  }  // ~unique_ptr<Token> → Token::~Token → Detach
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});
}

// Pool stress: attach more than kMaxDepth to exercise the fallback path
TEST_F(StackFrameContextStorageTest, ExceedPoolDepth)
{
  constexpr size_t kOver = StackFrameContextStorage::kMaxDepth + 4;
  std::vector<opentelemetry::nostd::unique_ptr<Token>> tokens;
  tokens.reserve(kOver);

  Context ctx(std::string("k"), ContextValue{int64_t{0}});
  for (size_t i = 0; i < kOver; ++i)
  {
    tokens.push_back(RuntimeContext::Attach(ctx));
  }
  // Detach in reverse order
  for (size_t i = kOver; i > 0; --i)
  {
    EXPECT_TRUE(RuntimeContext::Detach(*tokens[i - 1]));
  }
  EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});
}

// Each thread has its own stack (thread isolation)
TEST_F(StackFrameContextStorageTest, ThreadIsolation)
{
  Context ctx_main(std::string("owner"), ContextValue{int64_t{0}});
  auto t_main = RuntimeContext::Attach(ctx_main);

  std::thread worker([]() {
    // Worker thread starts with empty context
    EXPECT_EQ(RuntimeContext::GetCurrent(), Context{});

    Context ctx_worker(std::string("owner"), ContextValue{int64_t{1}});
    auto tw = RuntimeContext::Attach(ctx_worker);
    EXPECT_EQ(RuntimeContext::GetCurrent(), ctx_worker);
  });
  worker.join();

  // Main thread unaffected
  EXPECT_EQ(RuntimeContext::GetCurrent(), ctx_main);
}
