// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/address_list.h"

#include "base/memory/scoped_ptr.h"
#include "base/string_util.h"
#include "net/base/host_resolver_proc.h"
#include "net/base/net_util.h"
#include "net/base/sys_addrinfo.h"
#if defined(OS_WIN)
#include "net/base/winsock_init.h"
#endif
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

void MutableSetPort(uint16 port, AddressList* addrlist) {
  struct addrinfo* mutable_head =
      const_cast<struct addrinfo*>(addrlist->head());
  SetPortForAllAddrinfos(mutable_head, port);
}

// Use getaddrinfo() to allocate an addrinfo structure.
int CreateAddressList(const std::string& hostname, int port,
                      AddressList* addrlist) {
#if defined(OS_WIN)
  EnsureWinsockInit();
#endif
  int rv = SystemHostResolverProc(hostname,
                                  ADDRESS_FAMILY_UNSPECIFIED,
                                  0,
                                  addrlist, NULL);
  if (rv == 0)
    MutableSetPort(port, addrlist);
  return rv;
}

void CreateLongAddressList(AddressList* addrlist, int port) {
  EXPECT_EQ(0, CreateAddressList("192.168.1.1", port, addrlist));
  AddressList second_list;
  EXPECT_EQ(0, CreateAddressList("192.168.1.2", port, &second_list));
  addrlist->Append(second_list.head());
}

TEST(AddressListTest, GetPort) {
  AddressList addrlist;
  EXPECT_EQ(0, CreateAddressList("192.168.1.1", 81, &addrlist));
  EXPECT_EQ(81, addrlist.GetPort());

  MutableSetPort(83, &addrlist);
  EXPECT_EQ(83, addrlist.GetPort());
}

TEST(AddressListTest, SetPortMakesCopy) {
  AddressList addrlist1;
  EXPECT_EQ(0, CreateAddressList("192.168.1.1", 85, &addrlist1));
  EXPECT_EQ(85, addrlist1.GetPort());

  AddressList addrlist2 = addrlist1;
  EXPECT_EQ(85, addrlist2.GetPort());

  // addrlist1 should not be affected by the assignment to
  // addrlist2.
  addrlist1.SetPort(80);
  EXPECT_EQ(80, addrlist1.GetPort());
  EXPECT_EQ(85, addrlist2.GetPort());
}

TEST(AddressListTest, Assignment) {
  AddressList addrlist1;
  EXPECT_EQ(0, CreateAddressList("192.168.1.1", 85, &addrlist1));
  EXPECT_EQ(85, addrlist1.GetPort());

  // Should reference the same data as addrlist1 -- so when we change addrlist1
  // both are changed.
  AddressList addrlist2 = addrlist1;
  EXPECT_EQ(85, addrlist2.GetPort());

  MutableSetPort(80, &addrlist1);
  EXPECT_EQ(80, addrlist1.GetPort());
  EXPECT_EQ(80, addrlist2.GetPort());
}

TEST(AddressListTest, CopyRecursive) {
  AddressList addrlist1;
  CreateLongAddressList(&addrlist1, 85);
  EXPECT_EQ(85, addrlist1.GetPort());

  AddressList addrlist2 =
      AddressList::CreateByCopying(addrlist1.head());

  ASSERT_TRUE(addrlist2.head()->ai_next != NULL);

  // addrlist1 is the same as addrlist2 at this point.
  EXPECT_EQ(85, addrlist1.GetPort());
  EXPECT_EQ(85, addrlist2.GetPort());

  // Changes to addrlist1 are not reflected in addrlist2.
  MutableSetPort(70, &addrlist1);
  MutableSetPort(90, &addrlist2);

  EXPECT_EQ(70, addrlist1.GetPort());
  EXPECT_EQ(90, addrlist2.GetPort());
}

TEST(AddressListTest, CopyNonRecursive) {
  AddressList addrlist1;
  CreateLongAddressList(&addrlist1, 85);
  EXPECT_EQ(85, addrlist1.GetPort());

  AddressList addrlist2 =
      AddressList::CreateByCopyingFirstAddress(addrlist1.head());

  ASSERT_TRUE(addrlist2.head()->ai_next == NULL);

  // addrlist1 is the same as addrlist2 at this point.
  EXPECT_EQ(85, addrlist1.GetPort());
  EXPECT_EQ(85, addrlist2.GetPort());

  // Changes to addrlist1 are not reflected in addrlist2.
  MutableSetPort(70, &addrlist1);
  MutableSetPort(90, &addrlist2);

  EXPECT_EQ(70, addrlist1.GetPort());
  EXPECT_EQ(90, addrlist2.GetPort());
}

TEST(AddressListTest, Append) {
  AddressList addrlist1;
  EXPECT_EQ(0, CreateAddressList("192.168.1.1", 11, &addrlist1));
  EXPECT_EQ(11, addrlist1.GetPort());
  AddressList addrlist2;
  EXPECT_EQ(0, CreateAddressList("192.168.1.2", 12, &addrlist2));
  EXPECT_EQ(12, addrlist2.GetPort());

  ASSERT_TRUE(addrlist1.head()->ai_next == NULL);
  addrlist1.Append(addrlist2.head());
  ASSERT_TRUE(addrlist1.head()->ai_next != NULL);

  AddressList addrlist3 =
      AddressList::CreateByCopyingFirstAddress(addrlist1.head()->ai_next);
  EXPECT_EQ(12, addrlist3.GetPort());
}

static const char* kCanonicalHostname = "canonical.bar.com";

TEST(AddressListTest, Canonical) {
  // Create an addrinfo with a canonical name.
  sockaddr_in address;
  // The contents of address do not matter for this test,
  // so just zero-ing them out for consistency.
  memset(&address, 0x0, sizeof(address));
  struct addrinfo ai;
  memset(&ai, 0x0, sizeof(ai));
  ai.ai_family = AF_INET;
  ai.ai_socktype = SOCK_STREAM;
  ai.ai_addrlen = sizeof(address);
  ai.ai_addr = reinterpret_cast<sockaddr*>(&address);
  ai.ai_canonname = const_cast<char *>(kCanonicalHostname);

  // Copy the addrinfo struct into an AddressList object and
  // make sure it seems correct.
  AddressList addrlist1 = AddressList::CreateByCopying(&ai);
  const struct addrinfo* addrinfo1 = addrlist1.head();
  EXPECT_TRUE(addrinfo1 != NULL);
  EXPECT_TRUE(addrinfo1->ai_next == NULL);
  std::string canon_name1;
  EXPECT_TRUE(addrlist1.GetCanonicalName(&canon_name1));
  EXPECT_EQ("canonical.bar.com", canon_name1);

  // Copy the AddressList to another one.
  AddressList addrlist2 = AddressList::CreateByCopying(addrinfo1);
  const struct addrinfo* addrinfo2 = addrlist2.head();
  EXPECT_TRUE(addrinfo2 != NULL);
  EXPECT_TRUE(addrinfo2->ai_next == NULL);
  EXPECT_TRUE(addrinfo2->ai_canonname != NULL);
  EXPECT_NE(addrinfo1, addrinfo2);
  EXPECT_NE(addrinfo1->ai_canonname, addrinfo2->ai_canonname);
  std::string canon_name2;
  EXPECT_TRUE(addrlist2.GetCanonicalName(&canon_name2));
  EXPECT_EQ("canonical.bar.com", canon_name2);

  // Make sure that GetCanonicalName correctly returns false
  // when ai_canonname is NULL.
  ai.ai_canonname = NULL;
  AddressList addrlist_no_canon = AddressList::CreateByCopying(&ai);
  std::string canon_name3 = "blah";
  EXPECT_FALSE(addrlist_no_canon.GetCanonicalName(&canon_name3));
  EXPECT_EQ("blah", canon_name3);
}

TEST(AddressListTest, IPLiteralConstructor) {
  struct TestData {
    std::string ip_address;
    std::string canonical_ip_address;
    bool is_ipv6;
  } tests[] = {
    { "127.0.00.1", "127.0.0.1", false },
    { "192.168.1.1", "192.168.1.1", false },
    { "::1", "::1", true },
    { "2001:db8:0::42", "2001:db8::42", true },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); i++) {
    AddressList expected_list;
    int rv = CreateAddressList(tests[i].canonical_ip_address, 80,
                               &expected_list);
    if (tests[i].is_ipv6 && rv != 0) {
      LOG(WARNING) << "Unable to resolve ip literal '" << tests[i].ip_address
                   << "' test skipped.";
      continue;
    }
    ASSERT_EQ(0, rv);
    const struct addrinfo* good_ai = expected_list.head();

    IPAddressNumber ip_number;
    ParseIPLiteralToNumber(tests[i].ip_address, &ip_number);
    AddressList test_list = AddressList::CreateFromIPAddressWithCname(
        ip_number, 80, true);
    const struct addrinfo* test_ai = test_list.head();

    EXPECT_EQ(good_ai->ai_family, test_ai->ai_family);
    EXPECT_EQ(good_ai->ai_socktype, test_ai->ai_socktype);
    EXPECT_EQ(good_ai->ai_addrlen, test_ai->ai_addrlen);
    size_t sockaddr_size =
        good_ai->ai_socktype == AF_INET ? sizeof(struct sockaddr_in) :
        good_ai->ai_socktype == AF_INET6 ? sizeof(struct sockaddr_in6) : 0;
    EXPECT_EQ(memcmp(good_ai->ai_addr, test_ai->ai_addr, sockaddr_size), 0);
    EXPECT_EQ(good_ai->ai_next, test_ai->ai_next);
    EXPECT_EQ(strcmp(tests[i].canonical_ip_address.c_str(),
                     test_ai->ai_canonname), 0);
  }
}

TEST(AddressListTest, AddressFromAddrInfo) {
  struct TestData {
    std::string ip_address;
    std::string canonical_ip_address;
    bool is_ipv6;
  } tests[] = {
    { "127.0.00.1", "127.0.0.1", false },
    { "192.168.1.1", "192.168.1.1", false },
    { "::1", "::1", true },
    { "2001:db8:0::42", "2001:db8::42", true },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); i++) {
    AddressList expected_list;
    int rv = CreateAddressList(tests[i].canonical_ip_address, 80,
                               &expected_list);
    if (tests[i].is_ipv6 && rv != 0) {
      LOG(WARNING) << "Unable to resolve ip literal '" << tests[i].ip_address
                   << "' test skipped.";
      continue;
    }
    ASSERT_EQ(0, rv);
    const struct addrinfo* good_ai = expected_list.head();

    AddressList test_list =
        AddressList::CreateFromSockaddr(good_ai->ai_addr,
                                        good_ai->ai_addrlen,
                                        SOCK_STREAM,
                                        IPPROTO_TCP);
    const struct addrinfo* test_ai = test_list.head();

    EXPECT_EQ(good_ai->ai_family, test_ai->ai_family);
    EXPECT_EQ(good_ai->ai_addrlen, test_ai->ai_addrlen);
    size_t sockaddr_size =
        good_ai->ai_family == AF_INET ? sizeof(struct sockaddr_in) :
        good_ai->ai_family == AF_INET6 ? sizeof(struct sockaddr_in6) : 0;
    EXPECT_EQ(memcmp(good_ai->ai_addr, test_ai->ai_addr, sockaddr_size), 0);
    EXPECT_EQ(good_ai->ai_next, test_ai->ai_next);
  }
}

TEST(AddressListTest, CreateFromIPAddressList) {
  struct TestData {
    std::string ip_address;
    const char* in_addr;
    int ai_family;
    size_t ai_addrlen;
    size_t in_addr_offset;
    size_t in_addr_size;
  } tests[] = {
    { "127.0.0.1",
      "\x7f\x00\x00\x01",
      AF_INET,
      sizeof(struct sockaddr_in),
      offsetof(struct sockaddr_in, sin_addr),
      sizeof(struct in_addr),
    },
    { "2001:db8:0::42",
      "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x42",
      AF_INET6,
      sizeof(struct sockaddr_in6),
      offsetof(struct sockaddr_in6, sin6_addr),
      sizeof(struct in6_addr),
    },
    { "192.168.1.1",
      "\xc0\xa8\x01\x01",
      AF_INET,
      sizeof(struct sockaddr_in),
      offsetof(struct sockaddr_in, sin_addr),
      sizeof(struct in_addr),
    },
  };
  const uint16 kPort = 80;

  // Construct a list of ip addresses.
  IPAddressList ip_list;
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); ++i) {
    IPAddressNumber ip_number;
    ParseIPLiteralToNumber(tests[i].ip_address, &ip_number);
    ip_list.push_back(ip_number);
  }

  AddressList test_list = AddressList::CreateFromIPAddressList(ip_list, kPort);
  EXPECT_EQ(kPort, test_list.GetPort());

  // Make sure that CreateFromIPAddressList has created an addrinfo
  // chain of exactly the same length as the |tests| with correct content.
  const struct addrinfo* next_ai = test_list.head();
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); ++i) {
    ASSERT_TRUE(next_ai != NULL);
    EXPECT_EQ(tests[i].ai_family, next_ai->ai_family);
    EXPECT_EQ(tests[i].ai_addrlen, static_cast<size_t>(next_ai->ai_addrlen));

    char* ai_addr = reinterpret_cast<char*>(next_ai->ai_addr);
    int rv = memcmp(tests[i].in_addr,
                    ai_addr + tests[i].in_addr_offset,
                    tests[i].in_addr_size);
    EXPECT_EQ(0, rv);
    next_ai = next_ai->ai_next;
  }
  EXPECT_EQ(NULL, next_ai);
}

}  // namespace
}  // namespace net
