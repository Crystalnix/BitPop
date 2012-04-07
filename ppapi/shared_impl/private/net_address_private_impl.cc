// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/private/net_address_private_impl.h"

#include <string.h>

#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/sys_byteorder.h"
#include "build/build_config.h"
#include "net/base/address_list.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_util.h"
#include "net/base/sys_addrinfo.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/private/ppb_net_address_private.h"
#include "ppapi/shared_impl/var.h"
#include "ppapi/thunk/thunk.h"

#if defined(OS_MACOSX)
// This is a bit evil, but it's standard operating procedure for |s6_addr|....
#define s6_addr16 __u6_addr.__u6_addr16
#endif

#if defined(OS_WIN)
// The type of |sockaddr::sa_family|.
typedef ADDRESS_FAMILY sa_family_t;

#define s6_addr16 u.Word
#endif

// The net address interface doesn't have a normal C -> C++ thunk since it
// doesn't actually have any proxy wrapping or associated objects; it's just a
// call into base. So we implement the entire interface here, using the thunk
// namespace so it magically gets hooked up in the proper places.

namespace ppapi {

namespace {

// This assert fails on OpenBSD for an unknown reason at the moment.
#if !defined(OS_OPENBSD)
// Make sure the storage in |PP_NetAddress_Private| is big enough. (Do it here
// since the data is opaque elsewhere.)
COMPILE_ASSERT(sizeof(reinterpret_cast<PP_NetAddress_Private*>(0)->data) >=
               sizeof(sockaddr_storage), PP_NetAddress_Private_data_too_small);
#endif

inline sa_family_t GetFamily(const PP_NetAddress_Private& addr) {
  return reinterpret_cast<const sockaddr*>(addr.data)->sa_family;
}

PP_Bool AreHostsEqual(const PP_NetAddress_Private* addr1,
                      const PP_NetAddress_Private* addr2) {
  if (!NetAddressPrivateImpl::ValidateNetAddress(*addr1) ||
      !NetAddressPrivateImpl::ValidateNetAddress(*addr2))
    return PP_FALSE;

  if (GetFamily(*addr1) != GetFamily(*addr2))
    return PP_FALSE;

  if (GetFamily(*addr1) == AF_INET) {
    const sockaddr_in* a1 = reinterpret_cast<const sockaddr_in*>(addr1->data);
    const sockaddr_in* a2 = reinterpret_cast<const sockaddr_in*>(addr2->data);
    return PP_FromBool(a1->sin_addr.s_addr == a2->sin_addr.s_addr);
  }

  if (GetFamily(*addr1) == AF_INET6) {
    const sockaddr_in6* a1 = reinterpret_cast<const sockaddr_in6*>(addr1->data);
    const sockaddr_in6* a2 = reinterpret_cast<const sockaddr_in6*>(addr2->data);
    return PP_FromBool(a1->sin6_flowinfo == a2->sin6_flowinfo &&
                       memcmp(&a1->sin6_addr, &a2->sin6_addr,
                              sizeof(a1->sin6_addr)) == 0 &&
                       a1->sin6_scope_id == a2->sin6_scope_id);
  }

  return PP_FALSE;
}

PP_Bool AreEqual(const PP_NetAddress_Private* addr1,
                 const PP_NetAddress_Private* addr2) {
  // |AreHostsEqual()| will also validate the addresses and return false if
  // either is invalid.
  if (!AreHostsEqual(addr1, addr2))
    return PP_FALSE;

  // Note: Here, we know that |addr1| and |addr2| have the same family.
  if (GetFamily(*addr1) == AF_INET) {
    const sockaddr_in* a1 = reinterpret_cast<const sockaddr_in*>(addr1->data);
    const sockaddr_in* a2 = reinterpret_cast<const sockaddr_in*>(addr2->data);
    return PP_FromBool(a1->sin_port == a2->sin_port);
  }

  if (GetFamily(*addr1) == AF_INET6) {
    const sockaddr_in6* a1 = reinterpret_cast<const sockaddr_in6*>(addr1->data);
    const sockaddr_in6* a2 = reinterpret_cast<const sockaddr_in6*>(addr2->data);
    return PP_FromBool(a1->sin6_port == a2->sin6_port);
  }

  return PP_FALSE;
}

#if defined(OS_WIN) || defined(OS_MACOSX)
std::string ConvertIPv4AddressToString(const sockaddr_in* a,
                                       bool include_port) {
  unsigned ip = ntohl(a->sin_addr.s_addr);
  unsigned port = ntohs(a->sin_port);
  std::string description = base::StringPrintf(
      "%u.%u.%u.%u",
      (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
  if (include_port)
    base::StringAppendF(&description, ":%u", port);
  return description;
}

// Format an IPv6 address for human consumption, basically according to RFC
// 5952.
//  - If the scope is nonzero, it is appended to the address as "%<scope>" (this
//    is not in RFC 5952, but consistent with |getnameinfo()| on Linux and
//    Windows).
//  - If |include_port| is true, the address (possibly including the scope) is
//    enclosed in square brackets and ":<port>" is appended, i.e., the overall
//    format is "[<address>]:<port>".
//  - If the address is an IPv4 address embedded IPv6 (per RFC 4291), then the
//    mixed format is used, e.g., "::ffff:192.168.1.2". This is optional per RFC
//    5952, but consistent with |getnameinfo()|.
std::string ConvertIPv6AddressToString(const sockaddr_in6* a,
                                       bool include_port) {
  unsigned port = ntohs(a->sin6_port);
  unsigned scope = a->sin6_scope_id;
  std::string description(include_port ? "[" : "");

  // IPv4 address embedded in IPv6.
  if (a->sin6_addr.s6_addr16[0] == 0 && a->sin6_addr.s6_addr16[1] == 0 &&
      a->sin6_addr.s6_addr16[2] == 0 && a->sin6_addr.s6_addr16[3] == 0 &&
      a->sin6_addr.s6_addr16[4] == 0 &&
      (a->sin6_addr.s6_addr16[5] == 0 || a->sin6_addr.s6_addr16[5] == 0xffff)) {
    base::StringAppendF(
        &description,
        a->sin6_addr.s6_addr16[5] == 0 ? "::%u.%u.%u.%u" : "::ffff:%u.%u.%u.%u",
        static_cast<unsigned>(a->sin6_addr.s6_addr[12]),
        static_cast<unsigned>(a->sin6_addr.s6_addr[13]),
        static_cast<unsigned>(a->sin6_addr.s6_addr[14]),
        static_cast<unsigned>(a->sin6_addr.s6_addr[15]));

  // "Real" IPv6 addresses.
  } else {
    // Find the first longest run of 0s (of length > 1), to collapse to "::".
    int longest_start = 0;
    int longest_length = 0;
    int curr_start = 0;
    int curr_length = 0;
    for (int i = 0; i < 8; i++) {
      if (ntohs(a->sin6_addr.s6_addr16[i]) != 0) {
        curr_length = 0;
      } else {
        if (!curr_length)
          curr_start = i;
        curr_length++;
        if (curr_length > longest_length) {
          longest_start = curr_start;
          longest_length = curr_length;
        }
      }
    }

    bool need_sep = false;  // Whether the next item needs a ':' to separate.
    for (int i = 0; i < 8;) {
      if (longest_length > 1 && i == longest_start) {
        description.append("::");
        need_sep = false;
        i += longest_length;
      } else {
        unsigned v = ntohs(a->sin6_addr.s6_addr16[i]);
        base::StringAppendF(&description, need_sep ? ":%x" : "%x", v);
        need_sep = true;
        i++;
      }
    }
  }

  // Nonzero scopes, e.g., 123, are indicated by appending, e.g., "%123".
  if (scope != 0)
    base::StringAppendF(&description, "%%%u", scope);

  if (include_port)
    base::StringAppendF(&description, "]:%u", port);

  return description;
}
#endif  // OS_WIN || OS_MAC

PP_Var Describe(PP_Module /*module*/,
                const struct PP_NetAddress_Private* addr,
                PP_Bool include_port) {
  if (!NetAddressPrivateImpl::ValidateNetAddress(*addr))
    return PP_MakeUndefined();

#if defined(OS_WIN) || defined(OS_MACOSX)
  // On Windows, |NetAddressToString()| doesn't work in the sandbox. On Mac,
  // the output isn't consistent with RFC 5952, at least on Mac OS 10.6:
  // |getnameinfo()| collapses length-one runs of zeros (and also doesn't
  // display the scope).
  // TODO(viettrungluu): Consider switching to this on Linux.
  switch (GetFamily(*addr)) {
    case AF_INET: {
      const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(addr->data);
      return StringVar::StringToPPVar(
          ConvertIPv4AddressToString(a, !!include_port));
    }
    case AF_INET6: {
      const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(addr->data);
      return StringVar::StringToPPVar(
          ConvertIPv6AddressToString(a, !!include_port));
    }
    default:
      NOTREACHED();
      break;
  }
  return PP_MakeUndefined();
#else
  const sockaddr* a = reinterpret_cast<const sockaddr*>(addr->data);
  socklen_t l = addr->size;
  std::string description =
      include_port ? net::NetAddressToStringWithPort(a, l) :
                     net::NetAddressToString(a, l);
  return StringVar::StringToPPVar(description);
#endif
}

PP_Bool ReplacePort(const struct PP_NetAddress_Private* src_addr,
                    uint16_t port,
                    struct PP_NetAddress_Private* dest_addr) {
  if (!NetAddressPrivateImpl::ValidateNetAddress(*src_addr))
    return PP_FALSE;

  if (GetFamily(*src_addr) == AF_INET) {
    memmove(dest_addr, src_addr, sizeof(*src_addr));
    reinterpret_cast<sockaddr_in*>(dest_addr->data)->sin_port = htons(port);
    return PP_TRUE;
  }

  if (GetFamily(*src_addr) == AF_INET6) {
    memmove(dest_addr, src_addr, sizeof(*src_addr));
    reinterpret_cast<sockaddr_in6*>(dest_addr->data)->sin6_port = htons(port);
    return PP_TRUE;
  }

  return PP_FALSE;
}

void GetAnyAddress(PP_Bool is_ipv6, PP_NetAddress_Private* addr) {
  memset(addr->data, 0, arraysize(addr->data) * sizeof(addr->data[0]));
  if (is_ipv6) {
    sockaddr_in6* a = reinterpret_cast<sockaddr_in6*>(addr->data);
    addr->size = sizeof(*a);
    a->sin6_family = AF_INET6;
    a->sin6_addr = in6addr_any;
  } else {
    sockaddr_in* a = reinterpret_cast<sockaddr_in*>(addr->data);
    addr->size = sizeof(*a);
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = INADDR_ANY;
  }
}

const PPB_NetAddress_Private net_address_private_interface = {
  &AreEqual,
  &AreHostsEqual,
  &Describe,
  &ReplacePort,
  &GetAnyAddress
};

}  // namespace

namespace thunk {

PPAPI_THUNK_EXPORT const PPB_NetAddress_Private_0_1*
GetPPB_NetAddress_Private_0_1_Thunk() {
  return &net_address_private_interface;
}

}  // namespace thunk

// static
const PP_NetAddress_Private NetAddressPrivateImpl::kInvalidNetAddress = { 0 };

// static
bool NetAddressPrivateImpl::ValidateNetAddress(
    const PP_NetAddress_Private& addr) {
  if (addr.size < sizeof(reinterpret_cast<sockaddr*>(0)->sa_family))
    return false;

  // TODO(viettrungluu): more careful validation?
  // Just do a size check for AF_INET.
  if (GetFamily(addr) == AF_INET && addr.size >= sizeof(sockaddr_in))
    return true;

  // Ditto for AF_INET6.
  if (GetFamily(addr) == AF_INET6 && addr.size >= sizeof(sockaddr_in6))
    return true;

  // Reject everything else.
  return false;
}

// static
bool NetAddressPrivateImpl::SockaddrToNetAddress(
    const sockaddr* sa,
    uint32_t sa_length,
    PP_NetAddress_Private* net_addr) {
  if (!sa || sa_length == 0 || !net_addr)
    return false;

  CHECK_LE(sa_length, sizeof(net_addr->data));
  net_addr->size = sa_length;
  memcpy(net_addr->data, sa, net_addr->size);
  return true;
}

// static
bool NetAddressPrivateImpl::IPEndPointToNetAddress(
    const net::IPEndPoint& ip,
    PP_NetAddress_Private* net_addr) {
  sockaddr_storage storage = { 0 };
  size_t length = sizeof(storage);

  return ip.ToSockAddr(reinterpret_cast<sockaddr*>(&storage), &length) &&
      SockaddrToNetAddress(reinterpret_cast<const sockaddr*>(&storage), length,
                           net_addr);
}

// static
bool NetAddressPrivateImpl::AddressListToNetAddress(
    const net::AddressList& address_list,
    PP_NetAddress_Private* net_addr) {
  const addrinfo* head = address_list.head();
  return head && SockaddrToNetAddress(head->ai_addr, head->ai_addrlen,
                                      net_addr);
}

// static
bool NetAddressPrivateImpl::NetAddressToIPEndPoint(
    const PP_NetAddress_Private& net_addr,
    net::IPEndPoint* ip_end_point) {
  if (!ip_end_point || !ValidateNetAddress(net_addr))
    return false;

  if (!ip_end_point->FromSockAddr(
      reinterpret_cast<const sockaddr*>(net_addr.data), net_addr.size)) {
    return false;
  }

  return true;
}

// static
bool NetAddressPrivateImpl::NetAddressToAddressList(
    const PP_NetAddress_Private& net_addr, net::AddressList* address_list) {
  if (!address_list)
    return false;

  net::IPEndPoint ip_end_point;
  if (!NetAddressToIPEndPoint(net_addr, &ip_end_point))
    return false;

  *address_list = net::AddressList::CreateFromIPAddress(ip_end_point.address(),
                                                        ip_end_point.port());
  return true;
}

}  // namespace ppapi
