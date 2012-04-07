// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For linux_syscall_support.h. This makes it safe to call embedded system
// calls when in seccomp mode.
#define SYS_SYSCALL_ENTRYPOINT "playground$syscallEntryPoint"

#include "chrome/app/breakpad_linux.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/eintr_wrapper.h"
#include "base/file_path.h"
#include "base/global_descriptors_posix.h"
#include "base/linux_util.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "breakpad/src/client/linux/handler/exception_handler.h"
#include "breakpad/src/client/linux/minidump_writer/directory_reader.h"
#include "breakpad/src/common/linux/linux_libc_support.h"
#include "breakpad/src/common/memory.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info_posix.h"
#include "chrome/common/env_vars.h"
#include "content/common/chrome_descriptors.h"
#include "seccompsandbox/linux_syscall_support.h"

#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

// Some versions of gcc are prone to warn about unused return values. In cases
// where we either a) know the call cannot fail, or b) there is nothing we
// can do when a call fails, we mark the return code as ignored. This avoids
// spurious compiler warnings.
#define IGNORE_RET(x) do { if (x); } while (0)

static const char kUploadURL[] =
    "https://clients2.google.com/cr/report";

static bool is_crash_reporter_enabled = false;
static uint64_t process_start_time = 0;
static char* crash_log_path = NULL;

// Writes the value |v| as 16 hex characters to the memory pointed at by
// |output|.
static void write_uint64_hex(char* output, uint64_t v) {
  static const char hextable[] = "0123456789abcdef";

  for (int i = 15; i >= 0; --i) {
    output[i] = hextable[v & 15];
    v >>= 4;
  }
}

// The following helper functions are for calculating uptime.

// Converts a struct timeval to milliseconds.
static uint64_t timeval_to_ms(struct timeval *tv) {
  uint64_t ret = tv->tv_sec;  // Avoid overflow by explicitly using a uint64_t.
  ret *= 1000;
  ret += tv->tv_usec / 1000;
  return ret;
}

// Converts a struct timeval to milliseconds.
static uint64_t kernel_timeval_to_ms(struct kernel_timeval *tv) {
  uint64_t ret = tv->tv_sec;  // Avoid overflow by explicitly using a uint64_t.
  ret *= 1000;
  ret += tv->tv_usec / 1000;
  return ret;
}

// uint64_t version of my_int_len() from
// breakpad/src/common/linux/linux_libc_support.h. Return the length of the
// given, non-negative integer when expressed in base 10.
static unsigned my_uint64_len(uint64_t i) {
  if (!i)
    return 1;

  unsigned len = 0;
  while (i) {
    len++;
    i /= 10;
  }

  return len;
}

// uint64_t version of my_itos() from
// breakpad/src/common/linux/linux_libc_support.h. Convert a non-negative
// integer to a string (not null-terminated).
static void my_uint64tos(char* output, uint64_t i, unsigned i_len) {
  for (unsigned index = i_len; index; --index, i /= 10)
    output[index - 1] = '0' + (i % 10);
}

namespace {

// MIME substrings.
static const char g_rn[] = "\r\n";
static const char g_form_data_msg[] = "Content-Disposition: form-data; name=\"";
static const char g_quote_msg[] = "\"";
static const char g_dashdash_msg[] = "--";
static const char g_dump_msg[] = "upload_file_minidump\"; filename=\"dump\"";
static const char g_content_type_msg[] =
    "Content-Type: application/octet-stream";

// MimeWriter manages an iovec for writing MIMEs to a file.
class MimeWriter {
 public:
  static const int kIovCapacity = 30;
  static const size_t kMaxCrashChunkSize = 64;

  MimeWriter(int fd, const char* const mime_boundary);
  ~MimeWriter();

  // Append boundary.
  void AddBoundary();

  // Append end of file boundary.
  void AddEnd();

  // Append key/value pair with specified sizes.
  void AddPairData(const char* msg_type,
                   size_t msg_type_size,
                   const char* msg_data,
                   size_t msg_data_size);

  // Append key/value pair.
  void AddPairString(const char* msg_type,
                     const char* msg_data) {
    AddPairData(msg_type, my_strlen(msg_type), msg_data, my_strlen(msg_data));
  }

  // Append key/value pair, splitting value into chunks no larger than
  // |chunk_size|. |chunk_size| cannot be greater than |kMaxCrashChunkSize|.
  // The msg_type string will have a counter suffix to distinguish each chunk.
  void AddPairDataInChunks(const char* msg_type,
                           size_t msg_type_size,
                           const char* msg_data,
                           size_t msg_data_size,
                           size_t chunk_size,
                           bool strip_trailing_spaces);

  // Add binary file dump. Currently this is only done once, so the name is
  // fixed.
  void AddFileDump(uint8_t* file_data,
                   size_t file_size);

  // Flush any pending iovecs to the output file.
  void Flush() {
    IGNORE_RET(sys_writev(fd_, iov_, iov_index_));
    iov_index_ = 0;
  }

 private:
  void AddItem(const void* base, size_t size);
  // Minor performance trade-off for easier-to-maintain code.
  void AddString(const char* str) {
    AddItem(str, my_strlen(str));
  }
  void AddItemWithoutTrailingSpaces(const void* base, size_t size);

  struct kernel_iovec iov_[kIovCapacity];
  int iov_index_;

  // Output file descriptor.
  int fd_;

  const char* const mime_boundary_;

  DISALLOW_COPY_AND_ASSIGN(MimeWriter);
};

MimeWriter::MimeWriter(int fd, const char* const mime_boundary)
    : iov_index_(0),
      fd_(fd),
      mime_boundary_(mime_boundary) {
}

MimeWriter::~MimeWriter() {
}

void MimeWriter::AddBoundary() {
  AddString(mime_boundary_);
  AddString(g_rn);
}

void MimeWriter::AddEnd() {
  AddString(mime_boundary_);
  AddString(g_dashdash_msg);
  AddString(g_rn);
}

void MimeWriter::AddPairData(const char* msg_type,
                             size_t msg_type_size,
                             const char* msg_data,
                             size_t msg_data_size) {
  AddString(g_form_data_msg);
  AddItem(msg_type, msg_type_size);
  AddString(g_quote_msg);
  AddString(g_rn);
  AddString(g_rn);
  AddItem(msg_data, msg_data_size);
  AddString(g_rn);
}

void MimeWriter::AddPairDataInChunks(const char* msg_type,
                                     size_t msg_type_size,
                                     const char* msg_data,
                                     size_t msg_data_size,
                                     size_t chunk_size,
                                     bool strip_trailing_spaces) {
  if (chunk_size > kMaxCrashChunkSize)
    return;

  unsigned i = 0;
  size_t done = 0, msg_length = msg_data_size;

  while (msg_length) {
    char num[16];
    const unsigned num_len = my_int_len(++i);
    my_itos(num, i, num_len);

    size_t chunk_len = std::min(chunk_size, msg_length);

    AddString(g_form_data_msg);
    AddItem(msg_type, msg_type_size);
    AddItem(num, num_len);
    AddString(g_quote_msg);
    AddString(g_rn);
    AddString(g_rn);
    if (strip_trailing_spaces) {
      AddItemWithoutTrailingSpaces(msg_data + done, chunk_len);
    } else {
      AddItem(msg_data + done, chunk_len);
    }
    AddString(g_rn);
    AddBoundary();
    Flush();

    done += chunk_len;
    msg_length -= chunk_len;
  }
}

void MimeWriter::AddFileDump(uint8_t* file_data,
                             size_t file_size) {
  AddString(g_form_data_msg);
  AddString(g_dump_msg);
  AddString(g_rn);
  AddString(g_content_type_msg);
  AddString(g_rn);
  AddString(g_rn);
  AddItem(file_data, file_size);
  AddString(g_rn);
}

void MimeWriter::AddItem(const void* base, size_t size) {
  // Check if the iovec is full and needs to be flushed to output file.
  if (iov_index_ == kIovCapacity) {
    Flush();
  }
  iov_[iov_index_].iov_base = const_cast<void*>(base);
  iov_[iov_index_].iov_len = size;
  ++iov_index_;
}

void MimeWriter::AddItemWithoutTrailingSpaces(const void* base, size_t size) {
  while (size > 0) {
    const char* c = static_cast<const char*>(base) + size - 1;
    if (*c != ' ')
      break;
    size--;
  }
  AddItem(base, size);
}

}  // namespace

pid_t HandleCrashDump(const BreakpadInfo& info) {
  // WARNING: this code runs in a compromised context. It may not call into
  // libc nor allocate memory normally.

  const int dumpfd = sys_open(info.filename, O_RDONLY, 0);
  if (dumpfd < 0) {
    static const char msg[] = "Cannot upload crash dump: failed to open\n";
    sys_write(2, msg, sizeof(msg));
    return -1;
  }
  struct kernel_stat st;
  if (sys_fstat(dumpfd, &st) != 0) {
    static const char msg[] = "Cannot upload crash dump: stat failed\n";
    sys_write(2, msg, sizeof(msg));
    IGNORE_RET(sys_close(dumpfd));
    return -1;
  }

  google_breakpad::PageAllocator allocator;

  uint8_t* dump_data = reinterpret_cast<uint8_t*>(allocator.Alloc(st.st_size));
  if (!dump_data) {
    static const char msg[] = "Cannot upload crash dump: cannot alloc\n";
    sys_write(2, msg, sizeof(msg));
    IGNORE_RET(sys_close(dumpfd));
    return -1;
  }

  sys_read(dumpfd, dump_data, st.st_size);
  IGNORE_RET(sys_close(dumpfd));

  // We need to build a MIME block for uploading to the server. Since we are
  // going to fork and run wget, it needs to be written to a temp file.

  const int ufd = sys_open("/dev/urandom", O_RDONLY, 0);
  if (ufd < 0) {
    static const char msg[] = "Cannot upload crash dump because /dev/urandom"
                              " is missing\n";
    sys_write(2, msg, sizeof(msg) - 1);
    return -1;
  }

  static const char temp_file_template[] =
      "/tmp/chromium-upload-XXXXXXXXXXXXXXXX";
  char temp_file[sizeof(temp_file_template)];
  int fd = -1;
  if (info.upload) {
    memcpy(temp_file, temp_file_template, sizeof(temp_file_template));

    for (unsigned i = 0; i < 10; ++i) {
      uint64_t t;
      sys_read(ufd, &t, sizeof(t));
      write_uint64_hex(temp_file + sizeof(temp_file) - (16 + 1), t);

      fd = sys_open(temp_file, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (fd >= 0)
        break;
    }

    if (fd < 0) {
      static const char msg[] = "Failed to create temporary file in /tmp: "
          "cannot upload crash dump\n";
      sys_write(2, msg, sizeof(msg) - 1);
      IGNORE_RET(sys_close(ufd));
      return -1;
    }
  } else {
    fd = sys_open(info.filename, O_WRONLY, 0600);
    if (fd < 0) {
      static const char msg[] = "Failed to save crash dump: failed to open\n";
      sys_write(2, msg, sizeof(msg) - 1);
      IGNORE_RET(sys_close(ufd));
      return -1;
    }
  }

  // The MIME boundary is 28 hyphens, followed by a 64-bit nonce and a NUL.
  char mime_boundary[28 + 16 + 1];
  my_memset(mime_boundary, '-', 28);
  uint64_t boundary_rand;
  sys_read(ufd, &boundary_rand, sizeof(boundary_rand));
  write_uint64_hex(mime_boundary + 28, boundary_rand);
  mime_boundary[28 + 16] = 0;
  IGNORE_RET(sys_close(ufd));

  // The MIME block looks like this:
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="prod" \r\n \r\n
  //   Chrome_Linux \r\n
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="ver" \r\n \r\n
  //   1.2.3.4 \r\n
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="guid" \r\n \r\n
  //   1.2.3.4 \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="ptime" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="ptype" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or more gpu entries:
  //   Content-Disposition: form-data; name="gpu-xxxxx" \r\n \r\n
  //   <gpu-xxxxx> \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="lsb-release" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or more:
  //   Content-Disposition: form-data; name="url-chunk-1" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="channel" \r\n \r\n
  //   beta \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-views" \r\n \r\n
  //   3 \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-extensions" \r\n \r\n
  //   5 \r\n
  //   BOUNDARY \r\n
  //
  //   zero to 10:
  //   Content-Disposition: form-data; name="extension-1" \r\n \r\n
  //   abcdefghijklmnopqrstuvwxyzabcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-switches" \r\n \r\n
  //   5 \r\n
  //   BOUNDARY \r\n
  //
  //   zero to 15:
  //   Content-Disposition: form-data; name="switch-1" \r\n \r\n
  //   --foo \r\n
  //   BOUNDARY \r\n
  //
  //   Content-Disposition: form-data; name="dump"; filename="dump" \r\n
  //   Content-Type: application/octet-stream \r\n \r\n
  //   <dump contents>
  //   \r\n BOUNDARY -- \r\n

  MimeWriter writer(fd, mime_boundary);
  {
#if defined(OS_CHROMEOS)
    static const char chrome_product_msg[] = "Chrome_ChromeOS";
#else  // OS_LINUX
    static const char chrome_product_msg[] = "Chrome_Linux";
#endif
    static const char version_msg[] = PRODUCT_VERSION;

    writer.AddBoundary();
    writer.AddPairString("prod", chrome_product_msg);
    writer.AddBoundary();
    writer.AddPairString("ver", version_msg);
    writer.AddBoundary();
    writer.AddPairString("guid", info.guid);
    writer.AddBoundary();
    writer.Flush();
  }

  if (info.process_start_time > 0) {
    struct kernel_timeval tv;
    if (!sys_gettimeofday(&tv, NULL)) {
      uint64_t time = kernel_timeval_to_ms(&tv);
      if (time > info.process_start_time) {
        time -= info.process_start_time;
        char time_str[21];
        const unsigned time_len = my_uint64_len(time);
        my_uint64tos(time_str, time, time_len);

        static const char process_time_msg[] = "ptime";
        writer.AddPairData(process_time_msg, sizeof(process_time_msg) - 1,
                           time_str, time_len);
        writer.AddBoundary();
        writer.Flush();
      }
    }
  }

  if (info.process_type_length) {
    writer.AddPairString("ptype", info.process_type);
    writer.AddBoundary();
    writer.Flush();
  }

  // If GPU info is known, send it.
  unsigned gpu_vendor_len = my_strlen(child_process_logging::g_gpu_vendor_id);
  if (gpu_vendor_len) {
    static const char vendor_msg[] = "gpu-venid";
    static const char device_msg[] = "gpu-devid";
    static const char driver_msg[] = "gpu-driver";
    static const char psver_msg[] = "gpu-psver";
    static const char vsver_msg[] = "gpu-vsver";

    writer.AddPairString(vendor_msg, child_process_logging::g_gpu_vendor_id);
    writer.AddBoundary();
    writer.AddPairString(device_msg, child_process_logging::g_gpu_device_id);
    writer.AddBoundary();
    writer.AddPairString(driver_msg, child_process_logging::g_gpu_driver_ver);
    writer.AddBoundary();
    writer.AddPairString(psver_msg, child_process_logging::g_gpu_ps_ver);
    writer.AddBoundary();
    writer.AddPairString(vsver_msg, child_process_logging::g_gpu_vs_ver);
    writer.AddBoundary();
    writer.Flush();
  }

  if (info.distro_length) {
    static const char distro_msg[] = "lsb-release";
    writer.AddPairString(distro_msg, info.distro);
    writer.AddBoundary();
    writer.Flush();
  }

  // For renderers and plugins.
  if (info.crash_url_length) {
    static const char url_chunk_msg[] = "url-chunk-";
    static const unsigned kMaxUrlLength = 8 * MimeWriter::kMaxCrashChunkSize;
    writer.AddPairDataInChunks(url_chunk_msg, sizeof(url_chunk_msg) - 1,
        info.crash_url, std::min(info.crash_url_length, kMaxUrlLength),
        MimeWriter::kMaxCrashChunkSize, false /* Don't strip whitespaces. */);
  }

  if (my_strlen(child_process_logging::g_channel)) {
    writer.AddPairString("channel", child_process_logging::g_channel);
    writer.AddBoundary();
    writer.Flush();
  }

  if (my_strlen(child_process_logging::g_num_views)) {
    writer.AddPairString("num-views", child_process_logging::g_num_views);
    writer.AddBoundary();
    writer.Flush();
  }

  if (my_strlen(child_process_logging::g_num_extensions)) {
    writer.AddPairString("num-extensions",
                         child_process_logging::g_num_extensions);
    writer.AddBoundary();
    writer.Flush();
  }

  unsigned extension_ids_len =
      my_strlen(child_process_logging::g_extension_ids);
  if (extension_ids_len) {
    static const char extension_msg[] = "extension-";
    static const unsigned kMaxExtensionsLen =
        kMaxReportedActiveExtensions * child_process_logging::kExtensionLen;
    writer.AddPairDataInChunks(extension_msg, sizeof(extension_msg) - 1,
        child_process_logging::g_extension_ids,
        std::min(extension_ids_len, kMaxExtensionsLen),
        child_process_logging::kExtensionLen,
        false /* Don't strip whitespace. */);
  }

  if (my_strlen(child_process_logging::g_num_switches)) {
    writer.AddPairString("num-switches",
                         child_process_logging::g_num_switches);
    writer.AddBoundary();
    writer.Flush();
  }

  unsigned switches_len =
      my_strlen(child_process_logging::g_switches);
  if (switches_len) {
    static const char switch_msg[] = "switch-";
    static const unsigned kMaxSwitchLen =
        kMaxSwitches * child_process_logging::kSwitchLen;
    writer.AddPairDataInChunks(switch_msg, sizeof(switch_msg) - 1,
        child_process_logging::g_switches,
        std::min(switches_len, kMaxSwitchLen),
        child_process_logging::kSwitchLen,
        true /* Strip whitespace since switches are padded to kSwitchLen. */);
  }

  writer.AddFileDump(dump_data, st.st_size);
  writer.AddEnd();
  writer.Flush();

  IGNORE_RET(sys_close(fd));

  if (!info.upload)
    return 0;

  // The --header argument to wget looks like:
  //   --header=Content-Type: multipart/form-data; boundary=XYZ
  // where the boundary has two fewer leading '-' chars
  static const char header_msg[] =
      "--header=Content-Type: multipart/form-data; boundary=";
  char* const header = reinterpret_cast<char*>(allocator.Alloc(
      sizeof(header_msg) - 1 + sizeof(mime_boundary) - 2));
  memcpy(header, header_msg, sizeof(header_msg) - 1);
  memcpy(header + sizeof(header_msg) - 1, mime_boundary + 2,
         sizeof(mime_boundary) - 2);
  // We grab the NUL byte from the end of |mime_boundary|.

  // The --post-file argument to wget looks like:
  //   --post-file=/tmp/...
  static const char post_file_msg[] = "--post-file=";
  char* const post_file = reinterpret_cast<char*>(allocator.Alloc(
       sizeof(post_file_msg) - 1 + sizeof(temp_file)));
  memcpy(post_file, post_file_msg, sizeof(post_file_msg) - 1);
  memcpy(post_file + sizeof(post_file_msg) - 1, temp_file, sizeof(temp_file));

  const pid_t child = sys_fork();
  if (!child) {
    // This code is called both when a browser is crashing (in which case,
    // nothing really matters any more) and when a renderer/plugin crashes, in
    // which case we need to continue.
    //
    // Since we are a multithreaded app, if we were just to fork(), we might
    // grab file descriptors which have just been created in another thread and
    // hold them open for too long.
    //
    // Thus, we have to loop and try and close everything.
    const int fd = sys_open("/proc/self/fd", O_DIRECTORY | O_RDONLY, 0);
    if (fd < 0) {
      for (unsigned i = 3; i < 8192; ++i)
        IGNORE_RET(sys_close(i));
    } else {
      google_breakpad::DirectoryReader reader(fd);
      const char* name;
      while (reader.GetNextEntry(&name)) {
        int i;
        if (my_strtoui(&i, name) && i > 2 && i != fd)
          IGNORE_RET(sys_close(i));
        reader.PopEntry();
      }

      IGNORE_RET(sys_close(fd));
    }

    IGNORE_RET(sys_setsid());

    // Leave one end of a pipe in the wget process and watch for it getting
    // closed by the wget process exiting.
    int fds[2];
    IGNORE_RET(sys_pipe(fds));

    const pid_t child = sys_fork();
    if (child) {
      IGNORE_RET(sys_close(fds[1]));
      char id_buf[17];
      const int len = HANDLE_EINTR(sys_read(fds[0], id_buf,
                                   sizeof(id_buf) - 1));
      if (len > 0) {
        // Write crash dump id to stderr.
        id_buf[len] = 0;
        static const char msg[] = "\nCrash dump id: ";
        sys_write(2, msg, sizeof(msg) - 1);
        sys_write(2, id_buf, my_strlen(id_buf));
        sys_write(2, "\n", 1);

        // Write crash dump id to crash log as: seconds_since_epoch,crash_id
        struct kernel_timeval tv;
        if (crash_log_path && !sys_gettimeofday(&tv, NULL)) {
          uint64_t time = kernel_timeval_to_ms(&tv) / 1000;
          char time_str[21];
          const unsigned time_len = my_uint64_len(time);
          my_uint64tos(time_str, time, time_len);

          int log_fd = sys_open(crash_log_path, O_CREAT | O_WRONLY | O_APPEND,
                                0600);
          if (log_fd > 0) {
            sys_write(log_fd, time_str, time_len);
            sys_write(log_fd, ",", 1);
            sys_write(log_fd, id_buf, my_strlen(id_buf));
            sys_write(log_fd, "\n", 1);
            IGNORE_RET(sys_close(log_fd));
          }
        }
      }
      IGNORE_RET(sys_unlink(info.filename));
      IGNORE_RET(sys_unlink(temp_file));
      sys__exit(0);
    }

    IGNORE_RET(sys_close(fds[0]));
    IGNORE_RET(sys_dup2(fds[1], 3));
    static const char* const kWgetBinary = "/usr/bin/wget";
    const char* args[] = {
      kWgetBinary,
      header,
      post_file,
      kUploadURL,
      "--timeout=10",  // Set a timeout so we don't hang forever.
      "--tries=1",     // Don't retry if the upload fails.
      "-O",  // output reply to fd 3
      "/dev/fd/3",
      NULL,
    };

    execve(kWgetBinary, const_cast<char**>(args), environ);
    static const char msg[] = "Cannot upload crash dump: cannot exec "
                              "/usr/bin/wget\n";
    sys_write(2, msg, sizeof(msg) - 1);
    sys__exit(1);
  }

  HANDLE_EINTR(sys_waitpid(child, NULL, 0));
  return child;
}

static bool CrashDone(const char* dump_path,
                      const char* minidump_id,
                      const bool upload,
                      const bool succeeded) {
  // WARNING: this code runs in a compromised context. It may not call into
  // libc nor allocate memory normally.
  if (!succeeded)
    return false;

  google_breakpad::PageAllocator allocator;
  const unsigned dump_path_len = my_strlen(dump_path);
  const unsigned minidump_id_len = my_strlen(minidump_id);
  char* const path = reinterpret_cast<char*>(allocator.Alloc(
      dump_path_len + 1 /* '/' */ + minidump_id_len +
      4 /* ".dmp" */ + 1 /* NUL */));
  memcpy(path, dump_path, dump_path_len);
  path[dump_path_len] = '/';
  memcpy(path + dump_path_len + 1, minidump_id, minidump_id_len);
  memcpy(path + dump_path_len + 1 + minidump_id_len, ".dmp", 4);
  path[dump_path_len + 1 + minidump_id_len + 4] = 0;

  BreakpadInfo info;
  info.filename = path;
  info.process_type = "browser";
  info.process_type_length = 7;
  info.crash_url = NULL;
  info.crash_url_length = 0;
  info.guid = child_process_logging::g_client_id;
  info.guid_length = my_strlen(child_process_logging::g_client_id);
  info.distro = base::g_linux_distro;
  info.distro_length = my_strlen(base::g_linux_distro);
  info.upload = upload;
  info.process_start_time = process_start_time;
  HandleCrashDump(info);

  return true;
}

// Wrapper script, do not add more code here.
static bool CrashDoneNoUpload(const char* dump_path,
                      const char* minidump_id,
                      void* context,
                      bool succeeded) {
  return CrashDone(dump_path, minidump_id, false, succeeded);
}

// Wrapper script, do not add more code here.
static bool CrashDoneUpload(const char* dump_path,
                      const char* minidump_id,
                      void* context,
                      bool succeeded) {
  return CrashDone(dump_path, minidump_id, true, succeeded);
}

void EnableCrashDumping(const bool unattended) {
  is_crash_reporter_enabled = true;

  FilePath tmp_path("/tmp");
  PathService::Get(base::DIR_TEMP, &tmp_path);

  FilePath dumps_path(tmp_path);
  if (PathService::Get(chrome::DIR_CRASH_DUMPS, &dumps_path)) {
    FilePath logfile = dumps_path.AppendASCII("uploads.log");
    std::string logfile_str = logfile.value();
    const size_t crash_log_path_len = logfile_str.size() + 1;
    crash_log_path = new char[crash_log_path_len];
    strncpy(crash_log_path, logfile_str.c_str(), crash_log_path_len);
  }

  if (unattended) {
    new google_breakpad::ExceptionHandler(dumps_path.value().c_str(), NULL,
                                          CrashDoneNoUpload, NULL,
                                          true /* install handlers */);
  } else {
    new google_breakpad::ExceptionHandler(tmp_path.value().c_str(), NULL,
                                          CrashDoneUpload, NULL,
                                          true /* install handlers */);
  }
}

// Non-Browser = Extension, Gpu, Plugins, Ppapi and Renderer
static bool NonBrowserCrashHandler(const void* crash_context,
                                   size_t crash_context_size,
                                   void* context) {
  const int fd = reinterpret_cast<intptr_t>(context);
  int fds[2] = { -1, -1 };
  if (sys_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
    static const char msg[] = "Failed to create socket for crash dumping.\n";
    sys_write(2, msg, sizeof(msg)-1);
    return false;
  }

  // On kernels with ptrace protection, e.g. Ubuntu 10.10+, the browser cannot
  // ptrace this crashing process and crash dumping will fail. When using the
  // SUID sandbox, this crashing process is likely to be in its own PID
  // namespace, and thus there is no way to permit only the browser process to
  // ptrace it.
  // The workaround is to allow all processes to ptrace this process if we
  // reach this point, by passing -1 as the allowed PID. However, support for
  // passing -1 as the PID won't reach kernels until around the Ubuntu 12.04
  // timeframe.
  sys_prctl(PR_SET_PTRACER, -1);

  // Start constructing the message to send to the browser.
  char guid[kGuidSize + 1] = {0};
  char crash_url[kMaxActiveURLSize + 1] = {0};
  char distro[kDistroSize + 1] = {0};
  const size_t guid_len =
      std::min(my_strlen(child_process_logging::g_client_id), kGuidSize);
  const size_t crash_url_len =
      std::min(my_strlen(child_process_logging::g_active_url),
               kMaxActiveURLSize);
  const size_t distro_len =
      std::min(my_strlen(base::g_linux_distro), kDistroSize);
  memcpy(guid, child_process_logging::g_client_id, guid_len);
  memcpy(crash_url, child_process_logging::g_active_url, crash_url_len);
  memcpy(distro, base::g_linux_distro, distro_len);

  char b;  // Dummy variable for sys_read below.
  const char* b_addr = &b;  // Get the address of |b| so we can create the
                            // expected /proc/[pid]/syscall content in the
                            // browser to convert namespace tids.

  // The length of the control message:
  static const unsigned kControlMsgSize = sizeof(fds);
  static const unsigned kControlMsgSpaceSize = CMSG_SPACE(kControlMsgSize);
  static const unsigned kControlMsgLenSize = CMSG_LEN(kControlMsgSize);

  const size_t kIovSize = 7;
  struct kernel_msghdr msg;
  my_memset(&msg, 0, sizeof(struct kernel_msghdr));
  struct kernel_iovec iov[kIovSize];
  iov[0].iov_base = const_cast<void*>(crash_context);
  iov[0].iov_len = crash_context_size;
  iov[1].iov_base = guid;
  iov[1].iov_len = kGuidSize + 1;
  iov[2].iov_base = crash_url;
  iov[2].iov_len = kMaxActiveURLSize + 1;
  iov[3].iov_base = distro;
  iov[3].iov_len = kDistroSize + 1;
  iov[4].iov_base = &b_addr;
  iov[4].iov_len = sizeof(b_addr);
  iov[5].iov_base = &fds[0];
  iov[5].iov_len = sizeof(fds[0]);
  iov[6].iov_base = &process_start_time;
  iov[6].iov_len = sizeof(process_start_time);

  msg.msg_iov = iov;
  msg.msg_iovlen = kIovSize;
  char cmsg[kControlMsgSpaceSize];
  my_memset(cmsg, 0, kControlMsgSpaceSize);
  msg.msg_control = cmsg;
  msg.msg_controllen = sizeof(cmsg);

  struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
  hdr->cmsg_level = SOL_SOCKET;
  hdr->cmsg_type = SCM_RIGHTS;
  hdr->cmsg_len = kControlMsgLenSize;
  ((int*) CMSG_DATA(hdr))[0] = fds[0];
  ((int*) CMSG_DATA(hdr))[1] = fds[1];

  if (HANDLE_EINTR(sys_sendmsg(fd, &msg, 0)) < 0) {
    static const char msg[] = "Failed to tell parent about crash.\n";
    sys_write(2, msg, sizeof(msg)-1);
    IGNORE_RET(sys_close(fds[1]));
    return false;
  }
  IGNORE_RET(sys_close(fds[1]));

  if (HANDLE_EINTR(sys_read(fds[0], &b, 1)) != 1) {
    static const char msg[] = "Parent failed to complete crash dump.\n";
    sys_write(2, msg, sizeof(msg)-1);
  }

  return true;
}

void EnableNonBrowserCrashDumping() {
  const int fd = base::GlobalDescriptors::GetInstance()->Get(kCrashDumpSignal);
  is_crash_reporter_enabled = true;
  // We deliberately leak this object.
  google_breakpad::ExceptionHandler* handler =
      new google_breakpad::ExceptionHandler("" /* unused */, NULL, NULL,
                                            reinterpret_cast<void*>(fd), true);
  handler->set_crash_handler(NonBrowserCrashHandler);
}

void InitCrashReporter() {
  // Determine the process type and take appropriate action.
  const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();
  if (parsed_command_line.HasSwitch(switches::kDisableBreakpad))
    return;

  const std::string process_type =
      parsed_command_line.GetSwitchValueASCII(switches::kProcessType);
  if (process_type.empty()) {
    EnableCrashDumping(getenv(env_vars::kHeadless) != NULL);
  } else if (process_type == switches::kRendererProcess ||
             process_type == switches::kPluginProcess ||
             process_type == switches::kPpapiPluginProcess ||
             process_type == switches::kZygoteProcess ||
             process_type == switches::kGpuProcess) {
    // We might be chrooted in a zygote or renderer process so we cannot call
    // GetCollectStatsConsent because that needs access the the user's home
    // dir. Instead, we set a command line flag for these processes.
    // Even though plugins are not chrooted, we share the same code path for
    // simplicity.
    if (!parsed_command_line.HasSwitch(switches::kEnableCrashReporter))
      return;
    // Get the guid and linux distro from the command line switch.
    std::string switch_value =
        parsed_command_line.GetSwitchValueASCII(switches::kEnableCrashReporter);
    size_t separator = switch_value.find(",");
    if (separator != std::string::npos) {
      child_process_logging::SetClientId(switch_value.substr(0, separator));
      base::SetLinuxDistro(switch_value.substr(separator + 1));
    } else {
      child_process_logging::SetClientId(switch_value);
    }
    EnableNonBrowserCrashDumping();
  }

  // Set the base process start time value.
  struct timeval tv;
  if (!gettimeofday(&tv, NULL))
    process_start_time = timeval_to_ms(&tv);
  else
    process_start_time = 0;
}

bool IsCrashReporterEnabled() {
  return is_crash_reporter_enabled;
}
