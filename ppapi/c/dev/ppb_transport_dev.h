/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From dev/ppb_transport_dev.idl modified Wed Oct  5 14:06:02 2011. */

#ifndef PPAPI_C_DEV_PPB_TRANSPORT_DEV_H_
#define PPAPI_C_DEV_PPB_TRANSPORT_DEV_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/pp_var.h"

#define PPB_TRANSPORT_DEV_INTERFACE_0_7 "PPB_Transport(Dev);0.7"
#define PPB_TRANSPORT_DEV_INTERFACE PPB_TRANSPORT_DEV_INTERFACE_0_7

/**
 * @file
 * This file defines the <code>PPB_Transport_Dev</code> interface.
 */


/**
 * @addtogroup Enums
 * @{
 */
typedef enum {
  PP_TRANSPORTTYPE_DATAGRAM = 0,
  PP_TRANSPORTTYPE_STREAM = 1
} PP_TransportType;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_TransportType, 4);

typedef enum {
  /**
   * STUN server address and port, e.g "stun.example.com:19302".
   */
  PP_TRANSPORTPROPERTY_STUN_SERVER = 0,
  /**
   * Relay server address and port, e.g. "relay.example.com:12344".
   */
  PP_TRANSPORTPROPERTY_RELAY_SERVER = 1,
  /**
   * Username for the relay server.
   */
  PP_TRANSPORTPROPERTY_RELAY_USERNAME = 2,
  /**
   * Password for the relay server.
   */
  PP_TRANSPORTPROPERTY_RELAY_PASSWORD = 3,
  /**
   * Type of Relay server. Must be one of the PP_TransportRelayMode values. By
   * default is set to PP_TRANSPORTRELAYMODE_TURN.
   */
  PP_TRANSPORTPROPERTY_RELAY_MODE = 4,
  /**
   * TCP receive window in bytes. Takes effect only for PseudoTCP connections.
   */
  PP_TRANSPORTPROPERTY_TCP_RECEIVE_WINDOW = 5,
  /**
   * TCP send window in bytes. Takes effect only for PseudoTCP connections.
   */
  PP_TRANSPORTPROPERTY_TCP_SEND_WINDOW = 6,
  /**
   * Boolean value that disables Neagle's algorithm when set to true. When
   * Neagle's algorithm is disabled, all outgoing packets are sent as soon as
   * possible. When set to false (by default) data may be buffered until there
   * is a sufficient amount to send.
   */
  PP_TRANSPORTPROPERTY_TCP_NO_DELAY = 7,
  /**
   * Delay for ACK packets in milliseconds. By default set to 100ms.
   */
  PP_TRANSPORTPROPERTY_TCP_ACK_DELAY = 8,
  /**
   * Boolean value that disables TCP-based transports when set to true. By
   * default set to false.
   */
  PP_TRANSPORTPROPERTY_DISABLE_TCP_TRANSPORT = 9
} PP_TransportProperty;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_TransportProperty, 4);

typedef enum {
  /**
   * RFC5766 compliant relay server.
   */
  PP_TRANSPORTRELAYMODE_TURN = 0,
  /**
   * Legacy Google relay server.
   */
  PP_TRANSPORTRELAYMODE_GOOGLE = 1
} PP_TransportRelayMode;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_TransportRelayMode, 4);
/**
 * @}
 */

/**
 * @addtogroup Interfaces
 * @{
 */
/**
 * The transport interface provides peer-to-peer communication.
 *
 * TODO(juberti): other getters/setters
 * connect state
 * connect type, protocol
 * RTT
 */
struct PPB_Transport_Dev_0_7 {
  /**
   * Creates a new transport object with the specified name using the
   * specified protocol.
   */
  PP_Resource (*CreateTransport)(PP_Instance instance,
                                 const char* name,
                                 PP_TransportType type);
  /**
   * Returns PP_TRUE if resource is a Transport, PP_FALSE otherwise.
   */
  PP_Bool (*IsTransport)(PP_Resource resource);
  /**
   * Returns PP_TRUE if the transport is currently writable (i.e. can
   * send data to the remote peer), PP_FALSE otherwise.
   */
  PP_Bool (*IsWritable)(PP_Resource transport);
  /**
   * Sets various configuration properties of the transport.
   */
  int32_t (*SetProperty)(PP_Resource transport,
                         PP_TransportProperty property,
                         struct PP_Var value);
  /**
   * Establishes a connection to the remote peer.  Returns
   * PP_OK_COMPLETIONPENDING and notifies on |cb| when connectivity is
   * established (or timeout occurs).
   */
  int32_t (*Connect)(PP_Resource transport, struct PP_CompletionCallback cb);
  /**
   * Obtains another ICE candidate address to be provided to the
   * remote peer. Returns PP_OK_COMPLETIONPENDING if there are no more
   * addresses to be sent. After the callback is called
   * GetNextAddress() must be called again to get the address.
   */
  int32_t (*GetNextAddress)(PP_Resource transport,
                            struct PP_Var* address,
                            struct PP_CompletionCallback cb);
  /**
   * Provides an ICE candidate address that was received from the remote peer.
   */
  int32_t (*ReceiveRemoteAddress)(PP_Resource transport, struct PP_Var address);
  /**
   * Like recv(), receives data. Returns PP_OK_COMPLETIONPENDING if there is
   * currently no data to receive. In that case, the |data| pointer should
   * remain valid until the callback is called.
   */
  int32_t (*Recv)(PP_Resource transport,
                  void* data,
                  uint32_t len,
                  struct PP_CompletionCallback cb);
  /**
   * Like send(), sends data. Returns PP_OK_COMPLETIONPENDING if the socket is
   * currently flow-controlled. In that case, the |data| pointer should remain
   * valid until the callback is called.
   */
  int32_t (*Send)(PP_Resource transport,
                  const void* data,
                  uint32_t len,
                  struct PP_CompletionCallback cb);
  /**
   * Disconnects from the remote peer.
   */
  int32_t (*Close)(PP_Resource transport);
};

typedef struct PPB_Transport_Dev_0_7 PPB_Transport_Dev;
/**
 * @}
 */

#endif  /* PPAPI_C_DEV_PPB_TRANSPORT_DEV_H_ */

