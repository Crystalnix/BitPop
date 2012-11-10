/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>

#include "nacl_mounts/kernel_handle.h"
#include "nacl_mounts/kernel_intercept.h"
#include "nacl_mounts/kernel_proxy.h"
#include "nacl_mounts/mount.h"
#include "nacl_mounts/mount_mem.h"
#include "nacl_mounts/path.h"

#define __STDC__ 1
#include "gtest/gtest.h"


TEST(KernelProxy, WorkingDirectory) {
  char text[1024];

  ki_init(new KernelProxy());

  text[0] = 0;
  getcwd(text, sizeof(text));
  EXPECT_STREQ("/", text);

  char* alloc = getwd(NULL);
  EXPECT_EQ((char *) NULL, alloc);
  EXPECT_EQ(EFAULT, errno);

  text[0] = 0;
  alloc = getwd(text);
  EXPECT_STREQ("/", alloc);

  EXPECT_EQ(-1, chdir("/foo"));
  EXPECT_EQ(EEXIST, errno);

  EXPECT_EQ(0, chdir("/"));

  EXPECT_EQ(0, mkdir("/foo", S_IREAD | S_IWRITE));
  EXPECT_EQ(-1, mkdir("/foo", S_IREAD | S_IWRITE));
  EXPECT_EQ(EEXIST, errno);

  memset(text, 0, sizeof(text));
  EXPECT_EQ(0, chdir("foo"));
  EXPECT_EQ(text, getcwd(text, sizeof(text)));
  EXPECT_STREQ("/foo", text);

  memset(text, 0, sizeof(text));
  EXPECT_EQ(-1, chdir("foo"));
  EXPECT_EQ(EEXIST, errno);
  EXPECT_EQ(0, chdir(".."));
  EXPECT_EQ(0, chdir("/foo"));
  EXPECT_EQ(text, getcwd(text, sizeof(text)));
  EXPECT_STREQ("/foo", text);
}

TEST(KernelProxy, MemMountIO) {
  char text[1024];
  int fd1, fd2, fd3;
  int len;

  ki_init(new KernelProxy());

  // Create "/foo"
  EXPECT_EQ(0, mkdir("/foo", S_IREAD | S_IWRITE));

  // Fail to open "/foo/bar"
  EXPECT_EQ(-1, open("/foo/bar", O_RDONLY));
  EXPECT_EQ(ENOENT, errno);

  // Create bar "/foo/bar"
  fd1 = open("/foo/bar", O_RDONLY | O_CREAT);
  EXPECT_NE(-1, fd1);

  // Open (optionally create) bar "/foo/bar"
  fd2 = open("/foo/bar", O_RDONLY | O_CREAT);
  EXPECT_NE(-1, fd2);

  // Fail to exclusively create bar "/foo/bar"
  EXPECT_EQ(-1, open("/foo/bar", O_RDONLY | O_CREAT | O_EXCL));
  EXPECT_EQ(EEXIST, errno);

  // Write hello and world to same node with different descriptors
  // so that we overwrite each other
  EXPECT_EQ(5, write(fd2, "WORLD", 5));
  EXPECT_EQ(5, write(fd1, "HELLO", 5));

  fd3 = open("/foo/bar", O_WRONLY);
  EXPECT_NE(-1, fd3);

  len = read(fd3, text, sizeof(text));
  if (len > -0) text[len] = 0;
  EXPECT_EQ(5, len);
  EXPECT_STREQ("HELLO", text);
  EXPECT_EQ(0, close(fd1));
  EXPECT_EQ(0, close(fd2));

  fd1 = open("/foo/bar", O_WRONLY | O_APPEND);
  EXPECT_NE(-1, fd1);
  EXPECT_EQ(5, write(fd1, "WORLD", 5));

  len = read(fd3, text, sizeof(text));
  if (len >= 0) text[len] = 0;

  EXPECT_EQ(5, len);
  EXPECT_STREQ("WORLD", text);

  fd2 = open("/foo/bar", O_RDONLY);
  EXPECT_NE(-1, fd2);
  len = read(fd2, text, sizeof(text));
  if (len > 0) text[len] = 0;
  EXPECT_EQ(10, len);
  EXPECT_STREQ("HELLOWORLD", text);
}

StringMap_t g_StringMap;

class MountMockInit : public MountFactory<MountMockInit, MountMem> {
 public:
  bool Init(int dev, StringMap_t& args) {
    g_StringMap = args;
    if (args.find("false") != args.end())
      return false;
    return true;
  };
};

class KernelProxyMountMock : public KernelProxy {
  void Init() {
    KernelProxy::Init();
    factories_["initfs"] = MountMockInit::Create;
  }
};

TEST(KernelProxy, MountInit) {
  ki_init(new KernelProxyMountMock());
  int res1 = mount("/", "/mnt1", "initfs", 0, "false,foo=bar");

  EXPECT_EQ("bar", g_StringMap["foo"]);
  EXPECT_EQ(-1, res1);
  EXPECT_EQ(EINVAL, errno);

  int res2 = mount("/", "/mnt2", "initfs", 0, "true,bar=foo,x=y");
  EXPECT_NE(-1, res2);
  EXPECT_EQ("y", g_StringMap["x"]);
}