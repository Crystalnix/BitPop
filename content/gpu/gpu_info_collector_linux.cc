// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/gpu/gpu_info_collector.h"

#include <dlfcn.h>
#include <vector>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_piece.h"
#include "base/string_split.h"
#include "base/string_tokenizer.h"
#include "base/string_util.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_implementation.h"

namespace {

// PciDevice and PciAccess are defined to access libpci functions.  Their
// members match the corresponding structures defined by libpci in size up to
// fields we may access.  For those members we don't use, their names are
// defined as "fieldX", etc., or, left out if they are declared after the
// members we care about in libpci.

struct PciDevice {
  PciDevice* next;

  uint16 field0;
  uint8 field1;
  uint8 field2;
  uint8 field3;
  int field4;

  uint16 vendor_id;
  uint16 device_id;
  uint16 device_class;
};

struct PciAccess {
  unsigned int field0;
  int field1;
  int field2;
  char* field3;
  int field4;
  int field5;
  unsigned int field6;
  int field7;

  void (*function0)();
  void (*function1)();
  void (*function2)();

  PciDevice* device_list;
};

// Define function types.
typedef PciAccess* (*FT_pci_alloc)();
typedef void (*FT_pci_init)(PciAccess*);
typedef void (*FT_pci_cleanup)(PciAccess*);
typedef void (*FT_pci_scan_bus)(PciAccess*);
typedef void (*FT_pci_scan_bus)(PciAccess*);
typedef int (*FT_pci_fill_info)(PciDevice*, int);
typedef char* (*FT_pci_lookup_name)(PciAccess*, char*, int, int, ...);

// This includes dynamically linked library handle and functions pointers from
// libpci.
struct PciInterface {
  void* lib_handle;

  FT_pci_alloc pci_alloc;
  FT_pci_init pci_init;
  FT_pci_cleanup pci_cleanup;
  FT_pci_scan_bus pci_scan_bus;
  FT_pci_fill_info pci_fill_info;
  FT_pci_lookup_name pci_lookup_name;
};

// This checks if a system supports PCI bus.
// We check the existence of /sys/bus/pci or /sys/bug/pci_express.
bool IsPciSupported() {
  const FilePath pci_path("/sys/bus/pci/");
  const FilePath pcie_path("/sys/bus/pci_express/");
  return (file_util::PathExists(pci_path) ||
          file_util::PathExists(pcie_path));
}

// This dynamically opens libpci and get function pointers we need.  Return
// NULL if library fails to open or any functions can not be located.
// Returned interface (if not NULL) should be deleted in FinalizeLibPci.
PciInterface* InitializeLibPci(const char* lib_name) {
  void* handle = dlopen(lib_name, RTLD_LAZY);
  if (handle == NULL) {
    VLOG(1) << "Failed to dlopen " << lib_name;
    return NULL;
  }
  PciInterface* interface = new struct PciInterface;
  interface->lib_handle = handle;
  interface->pci_alloc = reinterpret_cast<FT_pci_alloc>(
      dlsym(handle, "pci_alloc"));
  interface->pci_init = reinterpret_cast<FT_pci_init>(
      dlsym(handle, "pci_init"));
  interface->pci_cleanup = reinterpret_cast<FT_pci_cleanup>(
      dlsym(handle, "pci_cleanup"));
  interface->pci_scan_bus = reinterpret_cast<FT_pci_scan_bus>(
      dlsym(handle, "pci_scan_bus"));
  interface->pci_fill_info = reinterpret_cast<FT_pci_fill_info>(
      dlsym(handle, "pci_fill_info"));
  interface->pci_lookup_name = reinterpret_cast<FT_pci_lookup_name>(
      dlsym(handle, "pci_lookup_name"));
  if (interface->pci_alloc == NULL ||
      interface->pci_init == NULL ||
      interface->pci_cleanup == NULL ||
      interface->pci_scan_bus == NULL ||
      interface->pci_fill_info == NULL ||
      interface->pci_lookup_name == NULL) {
    VLOG(1) << "Missing required function(s) from " << lib_name;
    dlclose(handle);
    delete interface;
    return NULL;
  }
  return interface;
}

// This close the dynamically opened libpci and delete the interface.
void FinalizeLibPci(PciInterface** interface) {
  DCHECK(interface && *interface && (*interface)->lib_handle);
  dlclose((*interface)->lib_handle);
  delete (*interface);
  *interface = NULL;
}

// Scan /etc/ati/amdpcsdb.default for "ReleaseVersion".
// Return "" on failing.
std::string CollectDriverVersionATI() {
  const FilePath::CharType kATIFileName[] =
      FILE_PATH_LITERAL("/etc/ati/amdpcsdb.default");
  FilePath ati_file_path(kATIFileName);
  if (!file_util::PathExists(ati_file_path))
    return "";
  std::string contents;
  if (!file_util::ReadFileToString(ati_file_path, &contents))
    return "";
  StringTokenizer t(contents, "\r\n");
  while (t.GetNext()) {
    std::string line = t.token();
    if (StartsWithASCII(line, "ReleaseVersion=", true)) {
      size_t begin = line.find_first_of("0123456789");
      if (begin != std::string::npos) {
        size_t end = line.find_first_not_of("0123456789.", begin);
        if (end == std::string::npos)
          return line.substr(begin);
        else
          return line.substr(begin, end - begin);
      }
    }
  }
  return "";
}

// Use glXGetClientString to get driver vendor.
// Return "" on failing.
std::string CollectDriverVendorGlx() {
  // TODO(zmo): handle the EGL/GLES2 case.
  if (gfx::GetGLImplementation() != gfx::kGLImplementationDesktopGL)
    return "";
  Display* display = XOpenDisplay(NULL);
  if (display == NULL)
    return "";
  std::string vendor = glXGetClientString(display, GLX_VENDOR);
  XCloseDisplay(display);
  return vendor;
}

// Return 0 on unrecognized vendor.
uint32 VendorStringToID(const std::string& vendor_string) {
  if (StartsWithASCII(vendor_string, "NVIDIA", true))
    return 0x10de;
  if (StartsWithASCII(vendor_string, "ATI", true))
    return 0x1002;
  // TODO(zmo): find a way to identify Intel cards.
  return 0;
}

}  // namespace anonymous

namespace gpu_info_collector {

bool CollectGraphicsInfo(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  // TODO(zmo): need to consider the case where we are running on top of
  // desktop GL and GL_ARB_robustness extension is available.
  gpu_info->can_lose_context =
      (gfx::GetGLImplementation() == gfx::kGLImplementationEGLGLES2);
  gpu_info->finalized = true;
  return CollectGraphicsInfoGL(gpu_info);
}

bool CollectPreliminaryGraphicsInfo(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  bool rt = true;
  if (!CollectVideoCardInfo(gpu_info))
    rt = false;

  if (gpu_info->vendor_id == 0x1002) {  // ATI
    std::string ati_driver_version = CollectDriverVersionATI();
    if (ati_driver_version != "") {
      gpu_info->driver_vendor = "ATI / AMD";
      gpu_info->driver_version = ati_driver_version;
    }
  }

  return rt;
}

bool CollectVideoCardInfo(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  std::string driver_vendor = CollectDriverVendorGlx();
  if (!driver_vendor.empty()) {
    gpu_info->driver_vendor = driver_vendor;
    uint32 vendor_id = VendorStringToID(driver_vendor);
    if (vendor_id != 0)
      gpu_info->vendor_id = vendor_id;
  }

  if (IsPciSupported() == false) {
    VLOG(1) << "PCI bus scanning is not supported";
    return false;
  }

  // TODO(zmo): be more flexible about library name.
  PciInterface* interface = InitializeLibPci("libpci.so.3");
  if (interface == NULL)
    interface = InitializeLibPci("libpci.so");
  if (interface == NULL) {
    VLOG(1) << "Failed to locate libpci";
    return false;
  }

  PciAccess* access = (interface->pci_alloc)();
  DCHECK(access != NULL);
  (interface->pci_init)(access);
  (interface->pci_scan_bus)(access);
  std::vector<PciDevice*> gpu_list;
  PciDevice* gpu_active = NULL;
  for (PciDevice* device = access->device_list;
       device != NULL; device = device->next) {
    (interface->pci_fill_info)(device, 33);  // Fill the IDs and class fields.
    // TODO(zmo): there might be other classes that qualify as display devices.
    if (device->device_class == 0x0300) {  // Device class is DISPLAY_VGA.
      if (gpu_info->vendor_id == 0 || gpu_info->vendor_id == device->vendor_id)
        gpu_list.push_back(device);
    }
  }
  if (gpu_list.size() == 1) {
    gpu_active = gpu_list[0];
  } else {
    // If more than one graphics card are identified, find the one that matches
    // gl VENDOR and RENDERER info.
    std::string gl_vendor_string = gpu_info->gl_vendor;
    std::string gl_renderer_string = gpu_info->gl_renderer;
    const int buffer_size = 255;
    scoped_array<char> buffer(new char[buffer_size]);
    std::vector<PciDevice*> candidates;
    for (size_t i = 0; i < gpu_list.size(); ++i) {
      PciDevice* gpu = gpu_list[i];
      // The current implementation of pci_lookup_name returns the same pointer
      // as the passed in upon success, and a different one (NULL or a pointer
      // to an error message) upon failure.
      if ((interface->pci_lookup_name)(access,
                                       buffer.get(),
                                       buffer_size,
                                       1,
                                       gpu->vendor_id) != buffer.get())
        continue;
      std::string vendor_string = buffer.get();
      const bool kCaseSensitive = false;
      if (!StartsWithASCII(gl_vendor_string, vendor_string, kCaseSensitive))
        continue;
      if ((interface->pci_lookup_name)(access,
                                       buffer.get(),
                                       buffer_size,
                                       2,
                                       gpu->vendor_id,
                                       gpu->device_id) != buffer.get())
        continue;
      std::string device_string = buffer.get();
      size_t begin = device_string.find_first_of('[');
      size_t end = device_string.find_last_of(']');
      if (begin != std::string::npos && end != std::string::npos &&
          begin < end) {
        device_string = device_string.substr(begin + 1, end - begin - 1);
      }
      if (StartsWithASCII(gl_renderer_string, device_string, kCaseSensitive)) {
        gpu_active = gpu;
        break;
      }
      // If a device's vendor matches gl VENDOR string, we want to consider the
      // possibility that libpci may not return the exact same name as gl
      // RENDERER string.
      candidates.push_back(gpu);
    }
    if (gpu_active == NULL && candidates.size() == 1)
      gpu_active = candidates[0];
  }
  if (gpu_active != NULL) {
    gpu_info->vendor_id =  gpu_active->vendor_id;
    gpu_info->device_id = gpu_active->device_id;
  }
  (interface->pci_cleanup)(access);
  FinalizeLibPci(&interface);
  return (gpu_active != NULL);
}

bool CollectDriverInfoGL(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  std::string gl_version_string = gpu_info->gl_version_string;
  std::vector<std::string> pieces;
  base::SplitStringAlongWhitespace(gl_version_string, &pieces);
  // In linux, the gl version string might be in the format of
  //   GLVersion DriverVendor DriverVersion
  if (pieces.size() < 3)
    return false;

  std::string driver_version = pieces[2];
  size_t pos = driver_version.find_first_not_of("0123456789.");
  if (pos == 0)
    return false;
  if (pos != std::string::npos)
    driver_version = driver_version.substr(0, pos);

  gpu_info->driver_vendor = pieces[1];
  gpu_info->driver_version = driver_version;
  return true;
}

}  // namespace gpu_info_collector
