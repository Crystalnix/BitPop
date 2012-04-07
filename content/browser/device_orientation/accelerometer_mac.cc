// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file is based on the SMSLib library.
//
// SMSLib Sudden Motion Sensor Access Library
// Copyright (c) 2010 Suitable Systems
// All rights reserved.
//
// Developed by: Daniel Griscom
//               Suitable Systems
//               http://www.suitable.com
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal with the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimers.
//
// - Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimers in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the names of Suitable Systems nor the names of its
// contributors may be used to endorse or promote products derived from
// this Software without specific prior written permission.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE.
//
// For more information about SMSLib, see
//    <http://www.suitable.com/tools/smslib.html>
// or contact
//    Daniel Griscom
//    Suitable Systems
//    1 Centre Street, Suite 204
//    Wakefield, MA 01880
//    (781) 665-0053

#include "content/browser/device_orientation/accelerometer_mac.h"

#include <math.h>
#include <sys/sysctl.h>

#include "base/logging.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/memory/scoped_ptr.h"
#include "content/browser/device_orientation/orientation.h"

namespace device_orientation {

struct AccelerometerMac::GenericMacbookSensor {
  // Name of device to be read.
  const char* service_name;

  // Number of bytes of the axis data.
  int axis_size;

  // Default calibration value for zero g.
  float zero_g;

  // Default calibration value for one g (negative when axis is inverted).
  float one_g;

  // Kernel function index.
  unsigned int function;

  // Size of the sensor record to be sent/received.
  unsigned int record_size;
};

struct AccelerometerMac::AxisData {
  // Location of the first byte representing the axis in the sensor data.
  int index;

  // Axis inversion flag. The value changes often between models.
  bool inverted;
};

// Sudden Motion Sensor descriptor.
struct AccelerometerMac::SensorDescriptor {
  // Prefix of model to be tested.
  const char* model_name;

  // Board id of model, or NULL if it doesn't matter.
  const char* board_id;

  // Axis-specific data (x,y,z order).
  AxisData axis[3];
};

// Typical sensor parameters in MacBook models.
const AccelerometerMac::GenericMacbookSensor
  AccelerometerMac::kGenericSensor = {
  "SMCMotionSensor", 2,
  0, 251,
  5, 40
};

// Supported sensor descriptors. Add entries here to enhance compatibility.
// Tested in order; place more specific entries before more general ones. (All
// non-tested entries from SMSLib have been removed.)
const AccelerometerMac::SensorDescriptor
    AccelerometerMac::kSupportedSensors[] = {
  // Tested by tommyw on a 13" MacBook.
  { "MacBook1,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by S.Selz. (via avi) on a 13" MacBook.
  { "MacBook2,1",    NULL, { { 0, true  }, { 2, false }, { 4, true  } } },

  // Tested by verhees on a 13" MacBook.
  { "MacBook3,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by adlr on a 13" MacBook.
  { "MacBook4,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by thakis on a 13" MacBook.
  { "MacBook5,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by Adam Gerson (via avi) on a 13" MacBook.
  { "MacBook5,2",    NULL, { { 0, false }, { 2, true  }, { 4, true  } } },

  // Tested by tommyw on a 13" MacBook.
  { "MacBook6,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by avi on a 13" MacBook.
  { "MacBook7,1",    NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by crc on a 13" MacBook Air.
  { "MacBookAir1,1", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by sfiera, pjw on a 13" MacBook Air.
  { "MacBookAir2,1", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Note:
  // - MacBookAir3,1 (11" MacBook Air, late 2010)
  // - MacBookAir3,2 (13" MacBook Air, late 2010)
  // - MacBookAir4,1 (11" MacBook Air, mid 2011)
  // - MacBookAir4,2 (13" MacBook Air, mid 2011)
  // have no accelerometer sensors.

  // Tested by crc on a 15" MacBook Pro.
  { "MacBookPro1,1", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by Raul Cuza (via avi) on a 17" MacBook Pro.
  { "MacBookPro1,2", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by L.V. (via avi) on a 17" MacBook Pro.
  { "MacBookPro2,1", NULL, { { 0, true  }, { 2, false }, { 4, true  } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  { "MacBookPro2,2", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by S.Som. (via avi) on a 17" MacBook Pro.
  { "MacBookPro3,1", "Mac-F42388C8",
                           { { 0, true  }, { 2, false }, { 4, true  } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  { "MacBookPro3,1", NULL, { { 0, false }, { 2, true  }, { 4, true  } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  // Tested by Eric Shapiro (via avi) on a 17" MacBook Pro.
  { "MacBookPro4,1", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  { "MacBookPro5,1", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by S.Selz. (via avi) on a 17" MacBook Pro.
  { "MacBookPro5,2", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by dmaclach on a 15" MacBook Pro.
  { "MacBookPro5,3", NULL, { { 2, false }, { 0, false }, { 4, true  } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  { "MacBookPro5,4", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by leandrogracia on a 13" MacBook Pro.
  { "MacBookPro5,5", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by khom, leadpipe on a 17" MacBook Pro.
  { "MacBookPro6,1", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by leandrogracia on a 15" MacBook Pro.
  { "MacBookPro6,2", NULL, { { 0, true  }, { 2, false }, { 4, true  } } },

  // Tested by leandrogracia on a 13" MacBook Pro.
  { "MacBookPro7,1", NULL, { { 0, true  }, { 2, true  }, { 4, false } } },

  // Tested by avi on a 13" MacBook Pro.
  { "MacBookPro8,1", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by avi on a 15" MacBook Pro.
  { "MacBookPro8,2", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Tested by avi on a 17" MacBook Pro.
  { "MacBookPro8,3", NULL, { { 0, false }, { 2, false }, { 4, false } } },

  // Generic MacBook accelerometer sensor data, to be used for future models
  // until they can be tested and their data entered. Note that this generic
  // configuration may well have problems with inverted axes.
  { "",              NULL, { { 0, true  }, { 2, true  }, { 4, false } } }
};

// Create a AccelerometerMac object and return NULL if no valid sensor found.
DataFetcher* AccelerometerMac::Create() {
  scoped_ptr<AccelerometerMac> accelerometer(new AccelerometerMac);
  return accelerometer->Init() ? accelerometer.release() : NULL;
}

AccelerometerMac::~AccelerometerMac() {
  IOServiceClose(io_connection_);
}

AccelerometerMac::AccelerometerMac()
    : sensor_(NULL),
      io_connection_(0) {
}

//  Retrieve per-axis accelerometer values.
//
//  Axes and angles are defined according to the W3C DeviceOrientation Draft.
//  See here: http://dev.w3.org/geo/api/spec-source-orientation.html
//
//  Note: only beta and gamma angles are provided. Alpha is set to zero.
//
//  Returns false in case of error or non-properly initialized object.
//
bool AccelerometerMac::GetOrientation(Orientation* orientation) {
  DCHECK(sensor_);

  // Reset output record memory buffer.
  std::fill(output_record_.begin(), output_record_.end(), 0x00);

  // Read record data from memory.
  const size_t kInputSize = kGenericSensor.record_size;
  size_t output_size = kGenericSensor.record_size;

  if (IOConnectCallStructMethod(io_connection_, kGenericSensor.function,
      static_cast<const char *>(&input_record_[0]), kInputSize,
      &output_record_[0], &output_size) != KERN_SUCCESS) {
    return false;
  }

  // Calculate per-axis calibrated values.
  float axis_value[3];

  for (int i = 0; i < 3; ++i) {
    int sensor_value = 0;
    int size  = kGenericSensor.axis_size;
    int index = sensor_->axis[i].index;

    // Important Note: Little endian is assumed as this code is Mac-only
    //                 and PowerPC is currently not supported.
    memcpy(&sensor_value, &output_record_[index], size);

    sensor_value = ExtendSign(sensor_value, size);

    // Correct value using the current calibration.
    axis_value[i] = static_cast<float>(sensor_value - kGenericSensor.zero_g) /
                    kGenericSensor.one_g;

    // Make sure we reject any NaN or infinite values.
    if (!isfinite(axis_value[i]))
      return false;

    // Clamp value to the [-1, 1] range.
    if (axis_value[i] < -1.0)
      axis_value[i] = -1.0;
    else if (axis_value[i] > 1.0)
      axis_value[i] = 1.0;

    // Apply axis inversion.
    if (sensor_->axis[i].inverted)
      axis_value[i] = -axis_value[i];
  }

  // Transform the accelerometer values to W3C draft angles.
  //
  // Accelerometer values are just dot products of the sensor axes
  // by the gravity vector 'g' with the result for the z axis inverted.
  //
  // To understand this transformation calculate the 3rd row of the z-x-y
  // Euler angles rotation matrix (because of the 'g' vector, only 3rd row
  // affects to the result). Note that z-x-y matrix means R = Ry * Rx * Rz.
  // Then, assume alpha = 0 and you get this:
  //
  // x_acc = sin(gamma)
  // y_acc = - cos(gamma) * sin(beta)
  // z_acc = cos(beta) * cos(gamma)
  //
  // After that the rest is just a bit of trigonometry.
  //
  // Also note that alpha can't be provided but it's assumed to be always zero.
  // This is necessary in order to provide enough information to solve
  // the equations.
  //
  const double kRad2deg = 180.0 / M_PI;

  orientation->alpha_ = 0.0;
  orientation->beta_  = kRad2deg * atan2(-axis_value[1], axis_value[2]);
  orientation->gamma_ = kRad2deg * asin(axis_value[0]);

  // Make sure that the interval boundaries comply with the specification. At
  // this point, beta is [-180, 180] and gamma is [-90, 90], but the spec has
  // the upper bound open on both.
  if (orientation->beta_ == 180.0) {
    orientation->beta_ = -180.0;  // -180 == 180 (upside-down)
  }
  if (orientation->gamma_ == 90.0) {
    static double just_less_than_90 = nextafter(90, 0);
    orientation->gamma_ = just_less_than_90;
  }

  // At this point, DCHECKing is paranoia. Never hurts.
  DCHECK_GE(orientation->beta_, -180.0);
  DCHECK_LT(orientation->beta_,  180.0);
  DCHECK_GE(orientation->gamma_, -90.0);
  DCHECK_LT(orientation->gamma_,  90.0);

  orientation->can_provide_alpha_ = false;
  orientation->can_provide_beta_  = true;
  orientation->can_provide_gamma_ = true;

  return true;
}

// Probe the local hardware looking for a supported sensor device
// and initialize an I/O connection to it.
bool AccelerometerMac::Init() {
  // Request model name from the kernel.
  char local_model[32];  // size from SMSLib
  size_t local_model_size = sizeof(local_model);
  int params[2] = { CTL_HW, HW_MODEL };
  if (sysctl(params, 2, local_model, &local_model_size, NULL, 0) != 0)
    return NULL;

  const SensorDescriptor* sensor_candidate = NULL;

  // Look for the current model in the supported sensor list.
  base::mac::ScopedCFTypeRef<CFDataRef> board_id_data;
  const int kNumSensors = arraysize(kSupportedSensors);

  for (int i = 0; i < kNumSensors; ++i) {
    // Check if the supported sensor model name is a prefix
    // of the local hardware model (empty names are accepted).
    const char* p1 = kSupportedSensors[i].model_name;
    for (const char* p2 = local_model; *p1 != '\0' && *p1 == *p2; ++p1, ++p2)
      continue;
    if (*p1 != '\0')
      continue;

    // Check the board id.
    if (kSupportedSensors[i].board_id) {
      if (!board_id_data.get()) {
        CFMutableDictionaryRef dict =
            IOServiceMatching("IOPlatformExpertDevice");
        if (!dict)
          continue;

        io_service_t platform_expert =
            IOServiceGetMatchingService(kIOMasterPortDefault, dict);
        if (!platform_expert)
          continue;

        board_id_data.reset((CFDataRef)
            IORegistryEntryCreateCFProperty(platform_expert,
                                            CFSTR("board-id"),
                                            kCFAllocatorDefault,
                                            0));
        IOObjectRelease(platform_expert);
        if (!board_id_data.get())
          continue;
      }

      if (strcmp(kSupportedSensors[i].board_id,
                 (const char*)CFDataGetBytePtr(board_id_data)) != 0) {
        continue;
      }
    }

    // Local hardware found in the supported sensor list.
    sensor_candidate = &kSupportedSensors[i];

    // Get a dictionary of the services matching to the one in the sensor.
    CFMutableDictionaryRef dict =
        IOServiceMatching(kGenericSensor.service_name);
    if (!dict)
      continue;

    // Get the first matching service.
    io_service_t device = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                      dict);
    if (!device)
      continue;

    // Try to open device.
    kern_return_t result;
    result = IOServiceOpen(device, mach_task_self(), 0, &io_connection_);
    IOObjectRelease(device);
    if (result != KERN_SUCCESS || io_connection_ == 0)
      return false;

    // Local sensor service confirmed by IOKit.
    sensor_ = sensor_candidate;
    break;
  }

  if (sensor_ == NULL)
    return false;

  // Allocate and initialize input/output records.
  input_record_.resize(kGenericSensor.record_size, 0x01);
  output_record_.resize(kGenericSensor.record_size, 0x00);

  // Try to retrieve the current orientation.
  Orientation test_orientation;
  return GetOrientation(&test_orientation);
}

// Extend the sign of an integer of less than 32 bits to a 32-bit integer.
int AccelerometerMac::ExtendSign(int value, size_t size) {
  switch (size) {
  case 1:
    if (value & 0x00000080)
      return value | 0xffffff00;
    break;

  case 2:
    if (value & 0x00008000)
      return value | 0xffff0000;
    break;

  case 3:
    if (value & 0x00800000)
      return value | 0xff000000;
    break;

  default:
    LOG(FATAL) << "Invalid integer size for sign extension: " << size;
  }

  return value;
}

}  // namespace device_orientation
