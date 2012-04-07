// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_SUPPORT_WEBKIT_SUPPORT_H_
#define WEBKIT_SUPPORT_WEBKIT_SUPPORT_H_

#include <string>

#include "base/basictypes.h"
#include "base/string16.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDevToolsAgentClient.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebFileSystem.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebGraphicsContext3D.h"
#include "ui/base/keycodes/keyboard_codes.h"

namespace WebKit {
class WebApplicationCacheHost;
class WebApplicationCacheHostClient;
class WebFileSystemCallbacks;
class WebFrame;
class WebGamepads;
class WebKitPlatformSupport;
class WebMediaPlayer;
class WebMediaPlayerClient;
class WebPlugin;
class WebString;
class WebThemeEngine;
class WebURL;
class WebURLRequest;
class WebURLResponse;
struct WebPluginParams;
struct WebURLError;
}

namespace webkit_media {
class MediaStreamClient;
}

// This package provides functions used by DumpRenderTree/chromium.
// DumpRenderTree/chromium uses some code in webkit/ of Chromium. In
// order to minimize the dependency from WebKit to Chromium, the
// following functions uses WebKit API classes as possible and hide
// implementation classes.
namespace webkit_support {

// Initializes or terminates a test environment.
// |unit_test_mode| should be set to true when running in a TestSuite, in which
// case no AtExitManager is created and ICU is not initialized (as it is already
// done by the TestSuite).
// SetUpTestEnvironment() and SetUpTestEnvironmentForUnitTests() calls
// WebKit::initialize().
// TearDownTestEnvironment() calls WebKit::shutdown().
// SetUpTestEnvironmentForUnitTests() should be used when running in a
// TestSuite, in which case no AtExitManager is created and ICU is not
// initialized (as it is already done by the TestSuite).
void SetUpTestEnvironment();
void SetUpTestEnvironmentForUnitTests();
void TearDownTestEnvironment();

// Returns a pointer to a WebKitPlatformSupport implementation for
// DumpRenderTree.  Needs to call SetUpTestEnvironment() before this.
// This returns a pointer to a static instance.  Don't delete it.
WebKit::WebKitPlatformSupport* GetWebKitPlatformSupport();

// This is used by WebFrameClient::createPlugin().
WebKit::WebPlugin* CreateWebPlugin(WebKit::WebFrame* frame,
                                   const WebKit::WebPluginParams& params);

// This is used by WebFrameClient::createMediaPlayer().
WebKit::WebMediaPlayer* CreateMediaPlayer(
    WebKit::WebFrame* frame,
    WebKit::WebMediaPlayerClient* client,
    webkit_media::MediaStreamClient* media_stream_client);

// This is used by WebFrameClient::createMediaPlayer().
WebKit::WebMediaPlayer* CreateMediaPlayer(
    WebKit::WebFrame* frame,
    WebKit::WebMediaPlayerClient* client);

// This is used by WebFrameClient::createApplicationCacheHost().
WebKit::WebApplicationCacheHost* CreateApplicationCacheHost(
    WebKit::WebFrame* frame, WebKit::WebApplicationCacheHostClient* client);

// Returns the root directory of the WebKit code.
WebKit::WebString GetWebKitRootDir();

enum GLBindingPreferences {
  GL_BINDING_DEFAULT,
  GL_BINDING_SOFTWARE_RENDERER
};
void SetUpGLBindings(GLBindingPreferences);

enum GraphicsContext3DImplementation {
  IN_PROCESS,
  IN_PROCESS_COMMAND_BUFFER
};
// Registers which GraphicsContext3D Implementation to use.
void SetGraphicsContext3DImplementation(GraphicsContext3DImplementation);
GraphicsContext3DImplementation GetGraphicsContext3DImplementation();

WebKit::WebGraphicsContext3D* CreateGraphicsContext3D(
    const WebKit::WebGraphicsContext3D::Attributes& attributes,
    WebKit::WebView* web_view,
    bool direct);

// ------- URL load mocking.
// Registers the file at |file_path| to be served when |url| is requested.
// |response| is the response provided with the contents.
void RegisterMockedURL(const WebKit::WebURL& url,
                       const WebKit::WebURLResponse& response,
                       const WebKit::WebString& file_path);

// Unregisters URLs so they are no longer mocked.
void UnregisterMockedURL(const WebKit::WebURL& url);
void UnregisterAllMockedURLs();

// Causes all pending asynchronous requests to be served.  When this method
// returns all the pending requests have been processed.
void ServeAsynchronousMockedRequests();

// Wrappers to minimize dependecy.

// -------- Debugging
bool BeingDebugged();

// -------- Message loop and task

// A wrapper for Chromium's Task class.
// The lifecycle is managed by webkit_support thus
// You shouldn't delete the object.
// Note that canceled object is just removed.
class TaskAdaptor {
 public:
  virtual ~TaskAdaptor() {}
  virtual void Run() = 0;
};

void RunMessageLoop();
void QuitMessageLoop();
void RunAllPendingMessages();
void DispatchMessageLoop();
bool MessageLoopNestableTasksAllowed();
void MessageLoopSetNestableTasksAllowed(bool allowed);
WebKit::WebDevToolsAgentClient::WebKitClientMessageLoop*
    CreateDevToolsMessageLoop();
void PostDelayedTask(void (*func)(void*), void* context, int64 delay_ms);

void PostDelayedTask(TaskAdaptor* task, int64 delay_ms);

// -------- File path and PathService
// Converts the specified path string to an absolute path in WebString.
// |utf8_path| is in UTF-8 encoding, not native multibyte string.
WebKit::WebString GetAbsoluteWebStringFromUTF8Path(
    const std::string& utf8_path);

// Create a WebURL from the specified string.
// If |path_or_url_in_nativemb| is a URL starting with scheme, this simply
// returns a WebURL for it.  Otherwise, this returns a file:// URL.
WebKit::WebURL CreateURLForPathOrURL(
    const std::string& path_or_url_in_nativemb);

// Converts file:///tmp/LayoutTests URLs to the actual location on disk.
WebKit::WebURL RewriteLayoutTestsURL(const std::string& utf8_url);

// Set the directory of specified file: URL as the current working directory.
bool SetCurrentDirectoryForFileURL(const WebKit::WebURL& fileUrl);

// Convert a file:/// URL to a base64 encoded data: URL.
WebKit::WebURL LocalFileToDataURL(const WebKit::WebURL& fileUrl);

// Scoped temporary directories for use by webkit layout tests.
class ScopedTempDirectory {
 public:
  virtual ~ScopedTempDirectory() {}
  virtual bool CreateUniqueTempDir() = 0;
  virtual std::string path() const = 0;
};

ScopedTempDirectory* CreateScopedTempDirectory();

// -------- Time
int64 GetCurrentTimeInMillisecond();

// -------- Net
// A wrapper of net::EscapePath().
std::string EscapePath(const std::string& path);
// Make an error description for layout tests.
std::string MakeURLErrorDescription(const WebKit::WebURLError& error);
// Creates WebURLError for an aborted request.
WebKit::WebURLError CreateCancelledError(const WebKit::WebURLRequest& request);

// - Database
void SetDatabaseQuota(int quota);
void ClearAllDatabases();

// - Resource loader
void SetAcceptAllCookies(bool accept);

// - Theme engine
#if defined(OS_WIN) || defined(OS_MACOSX)
void SetThemeEngine(WebKit::WebThemeEngine* engine);
WebKit::WebThemeEngine* GetThemeEngine();
#endif

// - DevTools
WebKit::WebURL GetDevToolsPathAsURL();

// - FileSystem
void OpenFileSystem(WebKit::WebFrame* frame, WebKit::WebFileSystem::Type type,
    long long size, bool create, WebKit::WebFileSystemCallbacks* callbacks);

// -------- Keyboard code
enum {
    VKEY_LEFT = ui::VKEY_LEFT,
    VKEY_RIGHT = ui::VKEY_RIGHT,
    VKEY_UP = ui::VKEY_UP,
    VKEY_DOWN = ui::VKEY_DOWN,
    VKEY_RETURN = ui::VKEY_RETURN,
    VKEY_INSERT = ui::VKEY_INSERT,
    VKEY_DELETE = ui::VKEY_DELETE,
    VKEY_PRIOR = ui::VKEY_PRIOR,
    VKEY_NEXT = ui::VKEY_NEXT,
    VKEY_HOME = ui::VKEY_HOME,
    VKEY_END = ui::VKEY_END,
    VKEY_SNAPSHOT = ui::VKEY_SNAPSHOT,
    VKEY_F1 = ui::VKEY_F1,
};

#if defined(TOOLKIT_USES_GTK)
int NativeKeyCodeForWindowsKeyCode(int keycode, bool shift);
#endif

// - Timers

double GetForegroundTabTimerInterval();

// - Logging

void EnableWebCoreLogChannels(const std::string& channels);

// - Gamepad

void SetGamepadData(const WebKit::WebGamepads& pads);

}  // namespace webkit_support

#endif  // WEBKIT_SUPPORT_WEBKIT_SUPPORT_H_
