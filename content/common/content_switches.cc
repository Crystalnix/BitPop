// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/content_switches.h"

namespace switches {

// By default, file:// URIs cannot read other file:// URIs. This is an
// override for developers who need the old behavior for testing.
const char kAllowFileAccessFromFiles[]      = "allow-file-access-from-files";

// Allows debugging of sandboxed processes (see zygote_main_linux.cc).
const char kAllowSandboxDebugging[]         = "allow-sandbox-debugging";

// Path to the exe to run for the renderer and plugin subprocesses.
const char kBrowserSubprocessPath[]         = "browser-subprocess-path";

// Disables client-visible 3D APIs, in particular WebGL and Pepper 3D.
// This is controlled by policy and is kept separate from the other
// enable/disable switches to avoid accidentally regressing the policy
// support for controlling access to these APIs.
const char kDisable3DAPIs[]                 = "disable-3d-apis";

// Disables accelerated compositing.
const char kDisableAcceleratedCompositing[] = "disable-accelerated-compositing";

// Disable the ApplicationCache.
const char kDisableApplicationCache[]       = "disable-application-cache";
//
// TODO(scherkus): remove --disable-audio when we have a proper fallback
// mechanism.
const char kDisableAudio[]                  = "disable-audio";

// Disable limits on the number of backing stores. Can prevent blinking for
// users with many windows/tabs and lots of memory.
const char kDisableBackingStoreLimit[]      = "disable-backing-store-limit";

// Disables HTML5 DB support.
const char kDisableDatabases[]              = "disable-databases";

// Disables data transfer items.
const char kDisableDataTransferItems[]      = "disable-data-transfer-items";

// Disables desktop notifications (default enabled on windows).
const char kDisableDesktopNotifications[]   = "disable-desktop-notifications";

// Disables device orientation events.
const char kDisableDeviceOrientation[]      = "disable-device-orientation";

// Disable experimental WebGL support.
const char kDisableExperimentalWebGL[]      = "disable-webgl";

// Disable FileSystem API.
const char kDisableFileSystem[]             = "disable-file-system";

// Suppresses support for the Geolocation javascript API.
const char kDisableGeolocation[]            = "disable-geolocation";

// Disable GL multisampling.
const char kDisableGLMultisampling[]        = "disable-gl-multisampling";

// Disable the GLSL translator.
const char kDisableGLSLTranslator[]         = "disable-glsl-translator";

// Disable the GPU process sandbox.
const char kDisableGpuSandbox[]             = "disable-gpu-sandbox";

// Disable the thread that crashes the GPU process if it stops responding to
// messages.
const char kDisableGpuWatchdog[]            = "disable-gpu-watchdog";

// Disable the Indexed Database API.
const char kDisableIndexedDatabase[]        = "disable-indexed-database";

// Prevent Java from running.
const char kDisableJava[]                   = "disable-java";

// Don't execute JavaScript (browser JS like the new tab page still runs).
const char kDisableJavaScript[]             = "disable-javascript";

// Disable JavaScript I18N API.
const char kDisableJavaScriptI18NAPI[]      = "disable-javascript-i18n-api";

// Disable LocalStorage.
const char kDisableLocalStorage[]           = "disable-local-storage";

// Force logging to be disabled.  Logging is enabled by default in debug
// builds.
const char kDisableLogging[]                = "disable-logging";

// Prevent plugins from running.
const char kDisablePlugins[]                = "disable-plugins";

// Disable pop-up blocking.
const char kDisablePopupBlocking[]          = "disable-popup-blocking";

// Disable the seccomp sandbox (Linux only)
const char kDisableSeccompSandbox[]         = "disable-seccomp-sandbox";

// Disable session storage.
const char kDisableSessionStorage[]         = "disable-session-storage";

// Enable shared workers. Functionality not yet complete.
const char kDisableSharedWorkers[]          = "disable-shared-workers";

// Disables speech input.
const char kDisableSpeechInput[]            = "disable-speech-input";

// Disable Web Sockets support.
const char kDisableWebSockets[]             = "disable-web-sockets";

// Enable hardware accelerated page drawing.
// Please note that this flag is honored only if chromium is compiled with
// SKIA_GPU flag, which can be enabled by setting use_skia_gpu variable to 1
// in build/features_override.gypi.
const char kEnableAcceleratedDrawing[]      = "enable-accelerated-drawing";

// Enables WebKit accessibility within the renderer process.
const char kEnableAccessibility[]           = "enable-accessibility";

// Enables the benchmarking extensions.
const char kEnableBenchmarking[]            = "enable-benchmarking";

// Enables device motion events.
const char kEnableDeviceMotion[]            = "enable-device-motion";

// Enable the GPU plugin and Pepper 3D rendering.
const char kEnableGPUPlugin[]               = "enable-gpu-plugin";

// Force logging to be enabled.  Logging is disabled by default in release
// builds.
const char kEnableLogging[]                 = "enable-logging";

// On Windows, converts the page to the currently-installed monitor profile.
// This does NOT enable color management for images. The source is still
// assumed to be sRGB.
const char kEnableMonitorProfile[]          = "enable-monitor-profile";

// Enable Pepper and JavaScript P2P API.
const char kEnableP2PApi[]                  = "enable-p2papi";

// Enable caching of pre-parsed JS script data.  See http://crbug.com/32407.
const char kEnablePreparsedJsCaching[]      = "enable-preparsed-js-caching";

// Cause the OS X sandbox write to syslog every time an access to a resource
// is denied by the sandbox.
const char kEnableSandboxLogging[]          = "enable-sandbox-logging";

// Enable the seccomp sandbox (Linux only)
const char kEnableSeccompSandbox[]          = "enable-seccomp-sandbox";

// Enables StatsTable, logging statistics to a global named shared memory table.
const char kEnableStatsTable[]              = "enable-stats-table";

// Enables support for fullscreen video. Current implementation is
// incomplete and this flag is used for development and testing.
const char kEnableVideoFullscreen[]         = "enable-video-fullscreen";

// Enables video logging where video elements log playback performance data to
// the debug log.
const char kEnableVideoLogging[]            = "enable-video-logging";

// Enable web audio API.
const char kEnableWebAudio[]                = "enable-webaudio";

// Enables experimental features for the geolocation API.
// Current features:
// - CoreLocation support for Mac OS X 10.6
// - Gateway location for Linux and Windows
// - Location platform support for Windows 7
const char kExperimentalLocationFeatures[]  = "experimental-location-features";

// Load NPAPI plugins from the specified directory.
const char kExtraPluginDir[]                = "extra-plugin-dir";

// Causes the process to run as an extension subprocess.
const char kExtensionProcess[]              = "extension";

// Some field tests may rendomized in the browser, and the randomly selected
// outcome needs to be propagated to the renderer.  For instance, this is used
// to modify histograms recorded in the renderer, or to get the renderer to
// also set of its state (initialize, or not initialize components) to match the
// experiment(s).
// The argument is a string-ized list of experiment names, and the associated
// value that was randomly selected.  In the recent implementetaion, the
// persistent representation generated by field_trial.cc and later decoded, is a
// list of name and value pairs, separated by slashes. See field trial.cc for
// current details.
const char kForceFieldTestNameAndValue[]    = "force-fieldtest";

// Extra command line options for launching the GPU process (normally used
// for debugging). Use like renderer-cmd-prefix.
const char kGpuLauncher[]                   = "gpu-launcher";

// Makes this process a GPU sub-process.
const char kGpuProcess[]                    = "gpu-process";

// Causes the GPU process to display a dialog on launch.
const char kGpuStartupDialog[]              = "gpu-startup-dialog";

// Run the GPU process as a thread in the browser process.
const char kInProcessGPU[]                  = "in-process-gpu";

// Runs plugins inside the renderer process
const char kInProcessPlugins[]              = "in-process-plugins";

// Runs WebGL inside the renderer process.
const char kInProcessWebGL[]                = "in-process-webgl";

// Specifies the flags passed to JS engine
const char kJavaScriptFlags[]               = "js-flags";

// Use LevelDB as back-end for Indexed Database API.
const char kLevelDBIndexedDatabase[]        = "indexeddb-use-leveldb";

// Load an NPAPI plugin from the specified path.
const char kLoadPlugin[]                    = "load-plugin";

// Sets the minimum log level. Valid values are from 0 to 3:
// INFO = 0, WARNING = 1, LOG_ERROR = 2, LOG_FATAL = 3.
const char kLoggingLevel[]                  = "log-level";

// Make plugin processes log their sent and received messages to VLOG(1).
const char kLogPluginMessages[]             = "log-plugin-messages";

// Causes the process to run as a NativeClient loader.
const char kNaClLoaderProcess[]             = "nacl-loader";

// Support a separate switch that enables the v8 playback extension.
// The extension causes javascript calls to Date.now() and Math.random()
// to return consistent values, such that subsequent loads of the same
// page will result in consistent js-generated data and XHR requests.
// Pages may still be able to generate inconsistent data from plugins.
const char kNoJsRandomness[]                = "no-js-randomness";

// Don't send HTTP-Referer headers.
const char kNoReferrers[]                   = "no-referrers";

// Disables the sandbox for all process types that are normally sandboxed.
const char kNoSandbox[]                     = "no-sandbox";

// Read previously recorded data from the cache. Only cached data is read.
// See kRecordMode.
const char kPlaybackMode[]                  = "playback-mode";

// Specifies a command that should be used to launch the plugin process.  Useful
// for running the plugin process through purify or quantify.  Ex:
//   --plugin-launcher="path\to\purify /Run=yes"
const char kPluginLauncher[]                = "plugin-launcher";

// Tells the plugin process the path of the plugin to load
const char kPluginPath[]                    = "plugin-path";

// Causes the process to run as a plugin subprocess.
const char kPluginProcess[]                 = "plugin";

// Causes the plugin process to display a dialog on launch.
const char kPluginStartupDialog[]           = "plugin-startup-dialog";

// Argument to the process type that indicates a PPAPI broker process type.
const char kPpapiBrokerProcess[]            = "ppapi-broker";

// Use the PPAPI (Pepper) Flash found at the given path.
const char kPpapiFlashPath[]                = "ppapi-flash-path";

// Report the given version for the PPAPI (Pepper) Flash. The version should be
// numbers separated by '.'s (e.g., "12.3.456.78"). If not specified, it
// defaults to "10.2.999.999".
const char kPpapiFlashVersion[]             = "ppapi-flash-version";

// Runs PPAPI (Pepper) plugins out-of-process.
const char kPpapiOutOfProcess[]             = "ppapi-out-of-process";

// Like kPluginLauncher for PPAPI plugins.
const char kPpapiPluginLauncher[]           = "ppapi-plugin-launcher";

// Argument to the process type that indicates a PPAPI plugin process type.
const char kPpapiPluginProcess[]            = "ppapi";

// Causes the PPAPI sub process to display a dialog on launch.
const char kPpapiStartupDialog[]            = "ppapi-startup-dialog";

// Runs a single process for each site (i.e., group of pages from the same
// registered domain) the user visits.  We default to using a renderer process
// for each site instance (i.e., group of pages from the same registered
// domain with script connections to each other).
const char kProcessPerSite[]                = "process-per-site";

// Runs each set of script-connected tabs (i.e., a BrowsingInstance) in its own
// renderer process.  We default to using a renderer process for each
// site instance (i.e., group of pages from the same registered domain with
// script connections to each other).
const char kProcessPerTab[]                 = "process-per-tab";

// Chrome supports a playback and record mode.  Record mode saves *everything*
// to the cache.  Playback mode reads data exclusively from the cache.  This
// allows us to record a session into the cache and then replay it at will.
// See also kPlaybackMode.
const char kRecordMode[]                    = "record-mode";

// The value of this switch determines whether the process is started as a
// renderer or plugin host.  If it's empty, it's the browser.
const char kProcessType[]                   = "type";

// Causes the process to run as a profile import subprocess.
const char kProfileImportProcess[]          = "profile-import";

// Register Pepper plugins (see pepper_plugin_registry.cc for its format).
const char kRegisterPepperPlugins[]         = "register-pepper-plugins";

// Causes the renderer process to throw an assertion on launch.
const char kRendererAssertTest[]            = "renderer-assert-test";

// On POSIX only: the contents of this flag are prepended to the renderer
// command line. Useful values might be "valgrind" or "xterm -e gdb --args".
const char kRendererCmdPrefix[]             = "renderer-cmd-prefix";

// Causes the renderer process to crash on launch.
const char kRendererCrashTest[]             = "renderer-crash-test";

// Causes the process to run as renderer instead of as browser.
const char kRendererProcess[]               = "renderer";

// Causes the renderer process to display a dialog on launch.
const char kRendererStartupDialog[]         = "renderer-startup-dialog";

// Runs the plugin processes inside the sandbox.
const char kSafePlugins[]                   = "safe-plugins";

// Causes the process to run as a service process.
const char kServiceProcess[]                = "service";

// Visibly render a border around paint rects in the web page to help debug
// and study painting behavior.
const char kShowPaintRects[]                = "show-paint-rects";
//
// TODO(scherkus): remove --simple-data-source when our media resource loading
// is cleaned up and playback testing completed.
const char kSimpleDataSource[]              = "simple-data-source";

// Runs the renderer and plugins in the same process as the browser
const char kSingleProcess[]                 = "single-process";

// Runs the security test for the renderer sandbox.
const char kTestSandbox[]                   = "test-sandbox";

// Grant unlimited quota to store files to this process.
// Used for testing Pepper's FileRef/FileIO/FileSystem implementations.
// DO NOT USE FOR OTHER PURPOSES.
// TODO(dumi): remove the switch when we have a real quota implementation.
const char kUnlimitedQuotaForFiles[]        = "unlimited-quota-for-files";

// This is for testing IndexedDB and will give any website you visit unlimited
// quota in IndexedDB. This should only be used for development and not general
// browsing. It is ignored in single process mode.
const char kUnlimitedQuotaForIndexedDB[]    = "unlimited-quota-for-indexeddb";

// A string used to override the default user agent with a custom one.
const char kUserAgent[]                     = "user-agent";

// Causes the process to run as a utility subprocess.
const char kUtilityProcess[]                = "utility";

// The utility process is sandboxed, with access to one directory. This flag
// specifies the directory that can be accessed.
const char kUtilityProcessAllowedDir[]      = "utility-allowed-dir";

// Will add kWaitForDebugger to every child processes. If a value is passed, it
// will be used as a filter to determine if the child process should have the
// kWaitForDebugger flag passed on or not.
const char kWaitForDebuggerChildren[]       = "wait-for-debugger-children";

// Choose which logging channels in WebCore to activate.  See
// Logging.cpp in WebKit's WebCore for a list of available channels.
const char kWebCoreLogChannels[]            = "webcore-log-channels";

// Causes the worker process allocation to use as many processes as cores.
const char kWebWorkerProcessPerCore[]       = "web-worker-process-per-core";

// Causes workers to run together in one process, depending on their domains.
// Note this is duplicated in webworkerclient_impl.cc
const char kWebWorkerShareProcesses[]       = "web-worker-share-processes";

// Causes the process to run as a worker subprocess.
const char kWorkerProcess[]                 = "worker";

// The prefix used when starting the zygote process. (i.e. 'gdb --args')
const char kZygoteCmdPrefix[]               = "zygote-cmd-prefix";

// Causes the process to run as a renderer zygote.
const char kZygoteProcess[]                 = "zygote";

#if !defined(OFFICIAL_BUILD)
// Causes the renderer process to throw an assertion on launch.
const char kRendererCheckFalseTest[]        = "renderer-check-false-test";
#endif

}  // namespace switches
