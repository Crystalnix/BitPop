/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From ppb_websocket.idl modified Mon Feb  6 14:29:34 2012. */

#ifndef PPAPI_C_PPB_WEBSOCKET_H_
#define PPAPI_C_PPB_WEBSOCKET_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/pp_var.h"

#define PPB_WEBSOCKET_INTERFACE_1_0 "PPB_WebSocket;1.0"
#define PPB_WEBSOCKET_INTERFACE PPB_WEBSOCKET_INTERFACE_1_0

/**
 * @file
 * This file defines the <code>PPB_WebSocket</code> interface.
 */


/**
 * @addtogroup Enums
 * @{
 */
/**
 * This enumeration contains the types representing the WebSocket ready state
 * and these states are based on the JavaScript WebSocket API specification.
 * GetReadyState() returns one of these states.
 */
typedef enum {
  /**
   * Ready state is queried on an invalid resource.
   */
  PP_WEBSOCKETREADYSTATE_INVALID = -1,
  /**
   * Ready state that the connection has not yet been established.
   */
  PP_WEBSOCKETREADYSTATE_CONNECTING = 0,
  /**
   * Ready state that the WebSocket connection is established and communication
   * is possible.
   */
  PP_WEBSOCKETREADYSTATE_OPEN = 1,
  /**
   * Ready state that the connection is going through the closing handshake.
   */
  PP_WEBSOCKETREADYSTATE_CLOSING = 2,
  /**
   * Ready state that the connection has been closed or could not be opened.
   */
  PP_WEBSOCKETREADYSTATE_CLOSED = 3
} PP_WebSocketReadyState;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_WebSocketReadyState, 4);

/**
 * This enumeration contains status codes. These codes are used in Close() and
 * GetCloseCode(). See also RFC 6455, The WebSocket Protocol.
 * <code>PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE</code> and codes in the range
 * <code>PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MIN</code> to
 * <code>PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MAX</code>, and
 * <code>PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MIN</code> to
 * <code>PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MAX</code> are valid for Close().
 */
typedef enum {
  /**
   * Status codes in the range 0-999 are not used.
   */
  /**
   * Indicates a normal closure.
   */
  PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE = 1000,
  /**
   * Indicates that an endpoint is "going away", such as a server going down.
   */
  PP_WEBSOCKETSTATUSCODE_GOING_AWAY = 1001,
  /**
   * Indicates that an endpoint is terminating the connection due to a protocol
   * error.
   */
  PP_WEBSOCKETSTATUSCODE_PROTOCOL_ERROR = 1002,
  /**
   * Indicates that an endpoint is terminating the connection because it has
   * received a type of data it cannot accept.
   */
  PP_WEBSOCKETSTATUSCODE_UNSUPPORTED_DATA = 1003,
  /**
   * Status code 1004 is reserved.
   */
  /**
   * Pseudo code to indicate that receiving close frame doesn't contain any
   * status code.
   */
  PP_WEBSOCKETSTATUSCODE_NO_STATUS_RECEIVED = 1005,
  /**
   * Pseudo code to indicate that connection was closed abnormally, e.g.,
   * without closing handshake.
   */
  PP_WEBSOCKETSTATUSCODE_ABNORMAL_CLOSURE = 1006,
  /**
   * Indicates that an endpoint is terminating the connection because it has
   * received data within a message that was not consistent with the type of
   * the message (e.g., non-UTF-8 data within a text message).
   */
  PP_WEBSOCKETSTATUSCODE_INVALID_FRAME_PAYLOAD_DATA = 1007,
  /**
   * Indicates that an endpoint is terminating the connection because it has
   * received a message that violates its policy.
   */
  PP_WEBSOCKETSTATUSCODE_POLICY_VIOLATION = 1008,
  /**
   * Indicates that an endpoint is terminating the connection because it has
   * received a message that is too big for it to process.
   */
  PP_WEBSOCKETSTATUSCODE_MESSAGE_TOO_BIG = 1009,
  /**
   * Indicates that an endpoint (client) is terminating the connection because
   * it has expected the server to negotiate one or more extension, but the
   * server didn't return them in the response message of the WebSocket
   * handshake.
   */
  PP_WEBSOCKETSTATUSCODE_MANDATORY_EXTENSION = 1010,
  /**
   * Indicates that a server is terminating the connection because it
   * encountered an unexpected condition.
   */
  PP_WEBSOCKETSTATUSCODE_INTERNAL_SERVER_ERROR = 1011,
  /**
   * Status codes in the range 1012-1014 are reserved.
   */
  /**
   * Pseudo code to indicate that the connection was closed due to a failure to
   * perform a TLS handshake.
   */
  PP_WEBSOCKETSTATUSCODE_TLS_HANDSHAKE = 1015,
  /**
   * Status codes in the range 1016-2999 are reserved.
   */
  /**
   * Status codes in the range 3000-3999 are reserved for use by libraries,
   * frameworks, and applications. These codes are registered directly with
   * IANA.
   */
  PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MIN = 3000,
  PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MAX = 3999,
  /**
   * Status codes in the range 4000-4999 are reserved for private use.
   * Application can use these codes for application specific purposes freely.
   */
  PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MIN = 4000,
  PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MAX = 4999
} PP_WebSocketCloseCode;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_WebSocketCloseCode, 4);
/**
 * @}
 */

/**
 * @addtogroup Interfaces
 * @{
 */
struct PPB_WebSocket_1_0 {
  /**
   * Create() creates a WebSocket instance.
   *
   * @param[in] instance A <code>PP_Instance</code> identifying the instance
   * with the WebSocket.
   *
   * @return A <code>PP_Resource</code> corresponding to a WebSocket if
   * successful.
   */
  PP_Resource (*Create)(PP_Instance instance);
  /**
   * IsWebSocket() determines if the provided <code>resource</code> is a
   * WebSocket instance.
   *
   * @param[in] resource A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns <code>PP_TRUE</code> if <code>resource</code> is a
   * <code>PPB_WebSocket</code>, <code>PP_FALSE</code> if the
   * <code>resource</code> is invalid or some type other than
   * <code>PPB_WebSocket</code>.
   */
  PP_Bool (*IsWebSocket)(PP_Resource resource);
  /**
   * Connect() connects to the specified WebSocket server. Caller can call this
   * method at most once for a <code>web_socket</code>.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @param[in] url A <code>PP_Var</code> representing a WebSocket server URL.
   * The <code>PP_VarType</code> must be <code>PP_VARTYPE_STRING</code>.
   *
   * @param[in] protocols A pointer to an array of <code>PP_Var</code>
   * specifying sub-protocols. Each <code>PP_Var</code> represents one
   * sub-protocol and its <code>PP_VarType</code> must be
   * <code>PP_VARTYPE_STRING</code>. This argument can be null only if
   * <code>protocol_count</code> is 0.
   *
   * @param[in] protocol_count The number of sub-protocols in
   * <code>protocols</code>.
   *
   * @param[in] callback A <code>PP_CompletionCallback</code> which is called
   * when a connection is established or an error occurs in establishing
   * connection.
   *
   * @return An int32_t containing an error code from <code>pp_errors.h</code>.
   * Returns <code>PP_ERROR_BADARGUMENT</code> if specified <code>url</code>,
   * or <code>protocols</code> contains invalid string as
   * <code>The WebSocket API specification</code> defines. It corresponds to
   * SyntaxError of the specification.
   * Returns <code>PP_ERROR_NOACCESS</code> if the protocol specified in the
   * <code>url</code> is not a secure protocol, but the origin of the caller
   * has a secure scheme. Also returns it if the port specified in the
   * <code>url</code> is a port to which the user agent is configured to block
   * access because the port is a well-known port like SMTP. It corresponds to
   * SecurityError of the specification.
   * Returns <code>PP_ERROR_INPROGRESS</code> if the call is not the first
   * time.
   */
  int32_t (*Connect)(PP_Resource web_socket,
                     struct PP_Var url,
                     const struct PP_Var protocols[],
                     uint32_t protocol_count,
                     struct PP_CompletionCallback callback);
  /**
   * Close() closes the specified WebSocket connection by specifying
   * <code>code</code> and <code>reason</code>.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @param[in] code The WebSocket close code. Ignored if it is 0.
   * <code>PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE</code> must be used for the
   * usual case. To indicate some specific error cases, codes in the range
   * <code>PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MIN</code> to
   * <code>PP_WEBSOCKETSTATUSCODE_USER_REGISTERED_MAX</code>, and in the range
   * <code>PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MIN</code> to
   * <code>PP_WEBSOCKETSTATUSCODE_USER_PRIVATE_MAX</code> are available.
   *
   * @param[in] reason A <code>PP_Var</code> which represents the WebSocket
   * close reason. Ignored if it is <code>PP_VARTYPE_UNDEFINED</code>.
   * Otherwise, its <code>PP_VarType</code> must be
   * <code>PP_VARTYPE_STRING</code>.
   *
   * @param[in] callback A <code>PP_CompletionCallback</code> which is called
   * when the connection is closed or an error occurs in closing the
   * connection.
   *
   * @return An int32_t containing an error code from <code>pp_errors.h</code>.
   * Returns <code>PP_ERROR_BADARGUMENT</code> if <code>reason</code> contains
   * an invalid character as a UTF-8 string, or longer than 123 bytes. It
   * corresponds to JavaScript SyntaxError of the specification.
   * Returns <code>PP_ERROR_NOACCESS</code> if the code is not an integer
   * equal to 1000 or in the range 3000 to 4999. It corresponds to
   * InvalidAccessError of the specification. Returns
   * <code>PP_ERROR_INPROGRESS</code> if the call is not the first time.
   */
  int32_t (*Close)(PP_Resource web_socket,
                   uint16_t code,
                   struct PP_Var reason,
                   struct PP_CompletionCallback callback);
  /**
   * ReceiveMessage() receives a message from the WebSocket server.
   * This interface only returns a single message. That is, this interface must
   * be called at least N times to receive N messages, no matter how small each
   * message is.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @param[out] message The received message is copied to provided
   * <code>message</code>. The <code>message</code> must remain valid until
   * the ReceiveMessage operation completes. Its <code>PP_VarType</code>
   * will be <code>PP_VARTYPE_STRING</code> or
   * <code>PP_VARTYPE_ARRAY_BUFFER</code> on receiving.
   *
   * @param[in] callback A <code>PP_CompletionCallback</code> which is called
   * when the receiving message is completed. It is ignored if ReceiveMessage
   * completes synchronously and returns <code>PP_OK</code>.
   *
   * @return An int32_t containing an error code from <code>pp_errors.h</code>.
   * If an error is detected or connection is closed, returns
   * <code>PP_ERROR_FAILED</code> after all buffered messages are received.
   * Until buffered message become empty, continues to returns
   * <code>PP_OK</code> as if connection is still established without errors.
   */
  int32_t (*ReceiveMessage)(PP_Resource web_socket,
                            struct PP_Var* message,
                            struct PP_CompletionCallback callback);
  /**
   * SendMessage() sends a message to the WebSocket server.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @param[in] message A message to send. The message is copied to internal
   * buffer. So caller can free <code>message</code> safely after returning
   * from the function. Its <code>PP_VarType</code> must be
   * <code>PP_VARTYPE_STRING</code> or <code>PP_VARTYPE_ARRAY_BUFFER</code>.
   *
   * @return An int32_t containing an error code from <code>pp_errors.h</code>.
   * Returns <code>PP_ERROR_FAILED</code> if the ReadyState is
   * <code>PP_WEBSOCKETREADYSTATE_CONNECTING</code>. It corresponds JavaScript
   * InvalidStateError of the specification.
   * Returns <code>PP_ERROR_BADARGUMENT</code> if provided <code>message</code>
   * of string type contains an invalid character as a UTF-8 string. It
   * corresponds to JavaScript SyntaxError of the specification.
   * Otherwise, returns <code>PP_OK</code>, but it doesn't necessarily mean
   * that the server received the message.
   */
  int32_t (*SendMessage)(PP_Resource web_socket, struct PP_Var message);
  /**
   * GetBufferedAmount() returns the number of bytes of text and binary
   * messages that have been queued for the WebSocket connection to send but
   * have not been transmitted to the network yet.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns the number of bytes.
   */
  uint64_t (*GetBufferedAmount)(PP_Resource web_socket);
  /**
   * GetCloseCode() returns the connection close code for the WebSocket
   * connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns 0 if called before the close code is set.
   */
  uint16_t (*GetCloseCode)(PP_Resource web_socket);
  /**
   * GetCloseReason() returns the connection close reason for the WebSocket
   * connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns a <code>PP_VARTYPE_STRING</code> var. If called before the
   * close reason is set, it contains an empty string. Returns a
   * <code>PP_VARTYPE_UNDEFINED</code> if called on an invalid resource.
   */
  struct PP_Var (*GetCloseReason)(PP_Resource web_socket);
  /**
   * GetCloseWasClean() returns if the connection was closed cleanly for the
   * specified WebSocket connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns <code>PP_FALSE</code> if called before the connection is
   * closed, or called on an invalid resource. Otherwise, returns
   * <code>PP_TRUE</code> if the connection was closed cleanly, or returns
   * <code>PP_FALSE</code> if the connection was closed for abnormal reasons.
   */
  PP_Bool (*GetCloseWasClean)(PP_Resource web_socket);
  /**
   * GetExtensions() returns the extensions selected by the server for the
   * specified WebSocket connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns a <code>PP_VARTYPE_STRING</code> var. If called before the
   * connection is established, its data is an empty string. Returns a
   * <code>PP_VARTYPE_UNDEFINED</code> if called on an invalid resource.
   * Currently its data for valid resources are always an empty string.
   */
  struct PP_Var (*GetExtensions)(PP_Resource web_socket);
  /**
   * GetProtocol() returns the sub-protocol chosen by the server for the
   * specified WebSocket connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns a <code>PP_VARTYPE_STRING</code> var. If called before the
   * connection is established, it contains the empty string. Returns a
   * <code>PP_VARTYPE_UNDEFINED</code> if called on an invalid resource.
   */
  struct PP_Var (*GetProtocol)(PP_Resource web_socket);
  /**
   * GetReadyState() returns the ready state of the specified WebSocket
   * connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns <code>PP_WEBSOCKETREADYSTATE_INVALID</code> if called
   * before connect() is called, or called on an invalid resource.
   */
  PP_WebSocketReadyState (*GetReadyState)(PP_Resource web_socket);
  /**
   * GetURL() returns the URL associated with specified WebSocket connection.
   *
   * @param[in] web_socket A <code>PP_Resource</code> corresponding to a
   * WebSocket.
   *
   * @return Returns a <code>PP_VARTYPE_STRING</code> var. If called before the
   * connection is established, it contains the empty string. Return a
   * <code>PP_VARTYPE_UNDEFINED</code> if called on an invalid resource.
   */
  struct PP_Var (*GetURL)(PP_Resource web_socket);
};

typedef struct PPB_WebSocket_1_0 PPB_WebSocket;
/**
 * @}
 */

#endif  /* PPAPI_C_PPB_WEBSOCKET_H_ */

