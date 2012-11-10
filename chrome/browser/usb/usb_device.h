// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_USB_USB_DEVICE_H_
#define CHROME_BROWSER_USB_USB_DEVICE_H_

#include <map>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"

struct libusb_device;
struct libusb_device_handle;
struct libusb_transfer;

typedef libusb_device* PlatformUsbDevice;
typedef libusb_device_handle* PlatformUsbDeviceHandle;
typedef libusb_transfer* PlatformUsbTransferHandle;

class UsbService;

namespace net {
class IOBuffer;
}  // namespace net

enum UsbTransferStatus {
  USB_TRANSFER_COMPLETED = 0,
  USB_TRANSFER_ERROR,
  USB_TRANSFER_TIMEOUT,
  USB_TRANSFER_CANCELLED,
  USB_TRANSFER_STALLED,
  USB_TRANSFER_DISCONNECT,
  USB_TRANSFER_OVERFLOW,
};

typedef base::Callback<void(UsbTransferStatus)> UsbTransferCallback;

// A UsbDevice wraps the platform's underlying representation of what a USB
// device actually is, and provides accessors for performing many of the
// standard USB operations.
class UsbDevice : public base::RefCounted<UsbDevice> {
 public:
  enum TransferDirection { INBOUND, OUTBOUND };
  enum TransferRequestType { STANDARD, CLASS, VENDOR, RESERVED };
  enum TransferRecipient { DEVICE, INTERFACE, ENDPOINT, OTHER };

  // Usually you will not want to directly create a UsbDevice, favoring to let
  // the UsbService take care of the logistics of getting a platform device
  // handle and handling events for it.
  UsbDevice(UsbService* service, PlatformUsbDeviceHandle handle);

  PlatformUsbDeviceHandle handle() { return handle_; }

  // Close the USB device and release the underlying platform device.
  void Close();

  void ControlTransfer(const TransferDirection direction,
                       const TransferRequestType request_type,
                       const TransferRecipient recipient,
                       const uint8 request,
                       const uint16 value,
                       const uint16 index,
                       net::IOBuffer* buffer,
                       const size_t length,
                       const unsigned int timeout,
                       const UsbTransferCallback& callback);

  void BulkTransfer(const TransferDirection direction,
                    const uint8 endpoint,
                    net::IOBuffer* buffer,
                    const size_t length,
                    const unsigned int timeout,
                    const UsbTransferCallback& callback);

  void InterruptTransfer(const TransferDirection direction,
                         const uint8 endpoint,
                         net::IOBuffer* buffer,
                         const size_t length,
                         const unsigned int timeout,
                         const UsbTransferCallback& callback);

  void IsochronousTransfer(const TransferDirection direction,
                           const uint8 endpoint,
                           net::IOBuffer* buffer,
                           const size_t length,
                           const unsigned int packets,
                           const unsigned int packet_length,
                           const unsigned int timeout,
                           const UsbTransferCallback& callback);

  // Normal code should not call this function. It is called by the platform's
  // callback mechanism in such a way that it cannot be made private. Invokes
  // the callbacks associated with a given transfer, and removes it from the
  // in-flight transfer set.
  void TransferComplete(PlatformUsbTransferHandle transfer);

 private:
  struct Transfer {
    Transfer();
    ~Transfer();

    scoped_refptr<net::IOBuffer> buffer;
    UsbTransferCallback callback;
  };

  friend class base::RefCounted<UsbDevice>;
  virtual ~UsbDevice();

  // Checks that the device has not yet been closed.
  void CheckDevice();

  // Submits a transfer and starts tracking it. Retains the buffer and copies
  // the completion callback until the transfer finishes, whereupon it invokes
  // the callback then releases the buffer.
  void SubmitTransfer(PlatformUsbTransferHandle handle, net::IOBuffer* buffer,
                      const UsbTransferCallback& callback);

  // The UsbService isn't referenced here to prevent a dependency cycle between
  // the service and the devices. Since a service owns every device, and is
  // responsible for its destruction, there is no case where a UsbDevice can
  // have outlived its originating UsbService.
  UsbService* const service_;
  PlatformUsbDeviceHandle handle_;

  // transfers_ tracks all in-flight transfers associated with this device,
  // allowing the device to retain the buffer and callback associated with a
  // transfer until such time that it completes. It is protected by lock_.
  base::Lock lock_;
  std::map<PlatformUsbTransferHandle, Transfer> transfers_;

  DISALLOW_EVIL_CONSTRUCTORS(UsbDevice);
};

#endif  // CHROME_BROWSER_USB_USB_DEVICE_H_
