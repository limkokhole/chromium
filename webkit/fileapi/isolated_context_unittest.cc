// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/isolated_context.h"

#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

#define FPL(x) FILE_PATH_LITERAL(x)

namespace fileapi {

namespace {

const FilePath kTestPaths[] = {
  FilePath(FPL("/a/b")),
  FilePath(FPL("/c/d/e/f/g")),
  FilePath(FPL("/h/")),
#if defined(FILE_PATH_USES_WIN_SEPARATORS)
  FilePath(FPL("c:/foo/bar")),
  FilePath(FPL("x:\\foo\\baz")),
  FilePath(FPL("\\foo\\boom")),
#endif
};

}  // namespace

class IsolatedContextTest : public testing::Test {
 public:
  IsolatedContextTest()
      : fileset_(kTestPaths, kTestPaths + arraysize(kTestPaths)) {
  }

  void SetUp() {
    id_ = IsolatedContext::GetInstance()->RegisterIsolatedFileSystem(fileset_);
    ASSERT_FALSE(id_.empty());
  }

  void TearDown() {
    IsolatedContext::GetInstance()->RevokeIsolatedFileSystem(id_);
  }

  IsolatedContext* isolated_context() const {
    return IsolatedContext::GetInstance();
  }

 protected:
  std::string id_;
  std::set<FilePath> fileset_;

 private:
  DISALLOW_COPY_AND_ASSIGN(IsolatedContextTest);
};

#if defined(OS_WIN)
// See http://crbug.com/112568
#define MAYBE_RegisterAndRevokeTest FAILS_RegisterAndRevokeTest
#else
#define MAYBE_RegisterAndRevokeTest RegisterAndRevokeTest
#endif
TEST_F(IsolatedContextTest, MAYBE_RegisterAndRevokeTest) {
  // See if the returned top-level entries match with what we registered.
  std::vector<FilePath> toplevels;
  ASSERT_TRUE(isolated_context()->GetTopLevelPaths(id_, &toplevels));
  ASSERT_EQ(fileset_.size(), toplevels.size());
  for (size_t i = 0; i < toplevels.size(); ++i) {
    ASSERT_TRUE(fileset_.find(toplevels[i]) != fileset_.end());
  }

  // See if the basename of each registered kTestPaths (that is what we
  // register in SetUp() by RegisterIsolatedFileSystem) is properly cracked as
  // a valid virtual path in the isolated filesystem.
  for (size_t i = 0; i < arraysize(kTestPaths); ++i) {
    FilePath virtual_path = isolated_context()->CreateVirtualPath(
        id_, kTestPaths[i].BaseName());
    std::string cracked_id;
    FilePath cracked_path;
    ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
        virtual_path, &cracked_id, &cracked_path));
    ASSERT_EQ(kTestPaths[i].value(), cracked_path.value());
    ASSERT_EQ(id_, cracked_id);
  }

  // Revoking the current one and registering a new (empty) one.
  isolated_context()->RevokeIsolatedFileSystem(id_);
  std::string id2 = isolated_context()->RegisterIsolatedFileSystem(
      std::set<FilePath>());

  // Make sure the GetTopLevelPaths returns true only for the new one.
  ASSERT_TRUE(isolated_context()->GetTopLevelPaths(id2, &toplevels));
  ASSERT_FALSE(isolated_context()->GetTopLevelPaths(id_, &toplevels));

  isolated_context()->RevokeIsolatedFileSystem(id2);
}

#if defined(OS_WIN)
#define MAYBE_CrackWithRelativePaths FAILS_CrackWithRelativePaths
#else
#define MAYBE_CrackWithRelativePaths CrackWithRelativePaths
#endif
TEST_F(IsolatedContextTest, MAYBE_CrackWithRelativePaths) {
  const struct {
    FilePath::StringType path;
    bool valid;
  } relatives[] = {
    { FPL("foo"), true },
    { FPL("foo/bar"), true },
    { FPL(".."), false },
    { FPL("foo/.."), false },
    { FPL("foo/../bar"), false },
#if defined(FILE_PATH_USES_WIN_SEPARATORS)
# define SHOULD_FAIL_WITH_WIN_SEPARATORS false
#else
# define SHOULD_FAIL_WITH_WIN_SEPARATORS true
#endif
    { FPL("foo\\..\\baz"), SHOULD_FAIL_WITH_WIN_SEPARATORS },
    { FPL("foo/..\\baz"), SHOULD_FAIL_WITH_WIN_SEPARATORS },
  };

  for (size_t i = 0; i < arraysize(kTestPaths); ++i) {
    for (size_t j = 0; j < ARRAYSIZE_UNSAFE(relatives); ++j) {
      SCOPED_TRACE(testing::Message() << "Testing "
                   << kTestPaths[i].value() << " " << relatives[j].path);
      FilePath virtual_path = isolated_context()->CreateVirtualPath(
          id_, kTestPaths[i].BaseName().Append(relatives[j].path));
      std::string cracked_id;
      FilePath cracked_path;
      if (!relatives[j].valid) {
        ASSERT_FALSE(isolated_context()->CrackIsolatedPath(
            virtual_path, &cracked_id, &cracked_path));
        continue;
      }
      ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
          virtual_path, &cracked_id, &cracked_path));
      ASSERT_EQ(kTestPaths[i].Append(relatives[j].path).value(),
                cracked_path.value());
      ASSERT_EQ(id_, cracked_id);
    }
  }
}

#if defined(OS_WIN)
#define MAYBE_TestWithVirtualRoot FAILS_TestWithVirtualRoot
#else
#define MAYBE_TestWithVirtualRoot TestWithVirtualRoot
#endif
TEST_F(IsolatedContextTest, MAYBE_TestWithVirtualRoot) {
  std::string cracked_id;
  FilePath cracked_path;
  const FilePath root(FPL("/"));

  // Trying to crack virtual root "/" returns true but with empty cracked path
  // as "/" of the isolated filesystem is a pure virtual directory
  // that has no corresponding platform directory.
  FilePath virtual_path = isolated_context()->CreateVirtualPath(id_, root);
  ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
      virtual_path, &cracked_id, &cracked_path));
  ASSERT_EQ(FPL(""), cracked_path.value());
  ASSERT_EQ(id_, cracked_id);

  // Trying to crack "/foo" should fail (because "foo" is not the one
  // included in the kTestPaths).
  virtual_path = isolated_context()->CreateVirtualPath(
      id_, FilePath(FPL("foo")));
  ASSERT_FALSE(isolated_context()->CrackIsolatedPath(
      virtual_path, &cracked_id, &cracked_path));
}

}  // namespace fileapi
