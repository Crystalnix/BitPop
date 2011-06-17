// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines all the "content" command-line switches.

#ifndef CONTENT_COMMON_CONTENT_SWITCHES_H_
#define CONTENT_COMMON_CONTENT_SWITCHES_H_
#pragma once

namespace switches {

extern const char kAllowFileAccessFromFiles[];
extern const char kAllowSandboxDebugging[];
extern const char kBrowserSubprocessPath[];
extern const char kDisable3DAPIs[];
extern const char kDisableAcceleratedCompositing[];
extern const char kDisableApplicationCache[];
extern const char kDisableAudio[];
extern const char kDisableBackingStoreLimit[];
extern const char kDisableDatabases[];
extern const char kDisableDataTransferItems[];
extern const char kDisableDesktopNotifications[];
extern const char kDisableDeviceOrientation[];
extern const char kDisableExperimentalWebGL[];
extern const char kDisableFileSystem[];
extern const char kDisableGeolocation[];
extern const char kDisableGLMultisampling[];
extern const char kDisableGLSLTranslator[];
extern const char kDisableGpuSandbox[];
extern const char kDisableGpuWatchdog[];
extern const char kDisableIndexedDatabase[];
extern const char kDisableJava[];
extern const char kDisableJavaScript[];
extern const char kDisableJavaScriptI18NAPI[];
extern const char kDisableLocalStorage[];
extern const char kDisableLogging[];
extern const char kDisablePlugins[];
extern const char kDisablePopupBlocking[];
extern const char kDisableSeccompSandbox[];
extern const char kDisableSessionStorage[];
extern const char kDisableSharedWorkers[];
extern const char kDisableSpeechInput[];
extern const char kDisableWebSockets[];
extern const char kEnableAcceleratedDrawing[];
extern const char kEnableAccessibility[];
extern const char kEnableBenchmarking[];
extern const char kEnableDeviceMotion[];
extern const char kEnableGPUPlugin[];
extern const char kEnableLogging[];
extern const char kEnableMonitorProfile[];
extern const char kEnableP2PApi[];
extern const char kEnablePreparsedJsCaching[];
extern const char kEnableSandboxLogging[];
extern const char kEnableSeccompSandbox[];
extern const char kEnableStatsTable[];
extern const char kEnableVideoFullscreen[];
extern const char kEnableVideoLogging[];
extern const char kEnableWebAudio[];
extern const char kExperimentalLocationFeatures[];
// TODO(jam): this doesn't belong in content.
extern const char kExtensionProcess[];
extern const char kExtraPluginDir[];
extern const char kForceFieldTestNameAndValue[];
extern const char kGpuLauncher[];
extern const char kGpuProcess[];
extern const char kGpuStartupDialog[];
extern const char kInProcessGPU[];
extern const char kInProcessPlugins[];
extern const char kInProcessWebGL[];
extern const char kJavaScriptFlags[];
extern const char kLevelDBIndexedDatabase[];
extern const char kLoadPlugin[];
extern const char kLoggingLevel[];
extern const char kLogPluginMessages[];
// TODO(jam): this doesn't belong in content.
extern const char kNaClLoaderProcess[];
extern const char kNoJsRandomness[];
extern const char kNoReferrers[];
extern const char kNoSandbox[];
extern const char kPlaybackMode[];
extern const char kPluginLauncher[];
extern const char kPluginPath[];
extern const char kPluginProcess[];
extern const char kPluginStartupDialog[];
extern const char kPpapiBrokerProcess[];
extern const char kPpapiFlashPath[];
extern const char kPpapiFlashVersion[];
extern const char kPpapiOutOfProcess[];
extern const char kPpapiPluginLauncher[];
extern const char kPpapiPluginProcess[];
extern const char kPpapiStartupDialog[];
extern const char kProcessPerSite[];
extern const char kProcessPerTab[];
extern const char kProcessType[];
// TODO(jam): this doesn't belong in content.
extern const char kProfileImportProcess[];
extern const char kRecordMode[];
extern const char kRegisterPepperPlugins[];
extern const char kRendererAssertTest[];
extern const char kRendererCmdPrefix[];
extern const char kRendererCrashTest[];
extern const char kRendererProcess[];
extern const char kRendererStartupDialog[];
extern const char kSafePlugins[];
// TODO(jam): this doesn't belong in content.
extern const char kServiceProcess[];
extern const char kShowPaintRects[];
extern const char kSimpleDataSource[];
extern const char kSingleProcess[];
extern const char kTestSandbox[];
extern const char kUnlimitedQuotaForFiles[];
extern const char kUnlimitedQuotaForIndexedDB[];
extern const char kUserAgent[];
extern const char kUtilityProcess[];
extern const char kUtilityProcessAllowedDir[];
extern const char kWaitForDebuggerChildren[];
extern const char kWebCoreLogChannels[];
extern const char kWebWorkerProcessPerCore[];
extern const char kWebWorkerShareProcesses[];
extern const char kWorkerProcess[];
extern const char kZygoteCmdPrefix[];
extern const char kZygoteProcess[];

#if !defined(OFFICIAL_BUILD)
extern const char kRendererCheckFalseTest[];
#endif

}  // namespace switches

#endif  // CONTENT_COMMON_CONTENT_SWITCHES_H_
