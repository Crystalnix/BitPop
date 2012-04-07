/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From private/ppb_udp_socket_private.idl modified Wed Nov 16 15:27:20 2011. */

#ifndef PPAPI_C_PRIVATE_PPB_UDP_SOCKET_PRIVATE_H_
#define PPAPI_C_PRIVATE_PPB_UDP_SOCKET_PRIVATE_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/private/ppb_net_address_private.h"

#define PPB_UDPSOCKET_PRIVATE_INTERFACE_0_2 "PPB_UDPSocket_Private;0.2"
#define PPB_UDPSOCKET_PRIVATE_INTERFACE PPB_UDPSOCKET_PRIVATE_INTERFACE_0_2

/**
 * @file
 * This file defines the <code>PPB_UDPSocket_Private</code> interface.
 */


/**
 * @addtogroup Interfaces
 * @{
 */
struct PPB_UDPSocket_Private_0_2 {
  /**
   * Creates a UDP socket resource.
   */
  PP_Resource (*Create)(PP_Instance instance_id);
  /**
   * Determines if a given resource is a UDP socket.
   */
  PP_Bool (*IsUDPSocket)(PP_Resource resource_id);
  /* Creates a socket and binds to the address given by |addr|. */
  int32_t (*Bind)(PP_Resource udp_socket,
                  const struct PP_NetAddress_Private* addr,
                  struct PP_CompletionCallback callback);
  /* Performs a non-blocking recvfrom call on socket.
   * Bind must be called first. |callback| is invoked when recvfrom
   * reads data.  You must call GetRecvFromAddress to recover the
   * address the data was retrieved from.
   */
  int32_t (*RecvFrom)(PP_Resource udp_socket,
                      char* buffer,
                      int32_t num_bytes,
                      struct PP_CompletionCallback callback);
  /* Upon successful completion of RecvFrom, the address that the data
   * was received from is stored in |addr|.
   */
  PP_Bool (*GetRecvFromAddress)(PP_Resource udp_socket,
                                struct PP_NetAddress_Private* addr);
  /* Performs a non-blocking sendto call on the socket created and
   * bound(has already called Bind).  The callback |callback| is
   * invoked when sendto completes.
   */
  int32_t (*SendTo)(PP_Resource udp_socket,
                    const char* buffer,
                    int32_t num_bytes,
                    const struct PP_NetAddress_Private* addr,
                    struct PP_CompletionCallback callback);
  /* Cancels all pending reads and writes, and closes the socket. */
  void (*Close)(PP_Resource udp_socket);
};

typedef struct PPB_UDPSocket_Private_0_2 PPB_UDPSocket_Private;
/**
 * @}
 */

#endif  /* PPAPI_C_PRIVATE_PPB_UDP_SOCKET_PRIVATE_H_ */

