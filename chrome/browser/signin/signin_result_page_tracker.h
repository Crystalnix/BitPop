// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_SIGNIN_RESULT_PAGE_TRACKER_H_
#define CHROME_BROWSER_SIGNIN_SIGNIN_RESULT_PAGE_TRACKER_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_types.h"

namespace content {
	class WebContents;
}

class Profile;
class Browser;

class SigninResultPageTracker : public LoginUIService::LoginUI,
																public content::NotficiationObserver,
																public ProfileKeyedService {
	public:
		class Observer {
		public:
			virtual void OnSigninCredentialsReady(const std::string& username,
																			const std::string& token,
																			const std::string& type) {}
			virtual void OnSigninErrorOccurred(const std::string& error_message) {}

		};

		SigninResultPageTracker();
		virtual ~SigninResultPageTracker();

		Observer* GetCurrentObserver() const { return observer_; }
		void Initialize(Profile* profile);

		void Track(content::WebContents* contents,
							 const std::string& state,
							 Observer* observer);
		void UntrackCurrent();

		// LoginUIService::LoginUI overrides
		// Invoked when the login UI should be brought to the foreground.
    virtual void FocusUI() OVERRIDE;

    // Invoked when the login UI should be closed. This can be invoked if the
    // user takes an action that should display new login UI.
    virtual void CloseUI() OVERRIDE;

    // content::NotificationObserver overrides
    virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

    typedef std::map<std::string, std::string> Parameters;
  protected:
  	Profile* GetProfile() const;
  	LoginUIService* GetLoginUIService() const;
  	void PostCloseContents();
  private:

  	Profile* profile_;
  	Browser* browser_;
	 	content::WebContents* tracked_contents_;
	 	std::string tracked_state_;

	 	Observer* observer_;
  	content::NotificationRegistrar registrar_;

  	DISALLOW_COPY_AND_ASSIGN(SigninResultPageTracker);
};

#endif // CHROME_BROWSER_SIGNIN_SIGNIN_RESULT_PAGE_TRACKER_H_
