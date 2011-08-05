// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_OVERLAY_PERSISTENT_PREF_STORE_H_
#define CHROME_BROWSER_PREFS_OVERLAY_PERSISTENT_PREF_STORE_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "chrome/browser/prefs/pref_value_map.h"
#include "chrome/common/persistent_pref_store.h"

// PersistentPrefStore that directs all write operations into a in-memory
// PrefValueMap. Read operations are first answered by the PrefValueMap.
// If the PrefValueMap does not contain a value for the requested key,
// the look-up is passed on to an underlying PersistentPrefStore |underlay_|.
class OverlayPersistentPrefStore : public PersistentPrefStore,
                                   public PrefStore::Observer {
 public:
  explicit OverlayPersistentPrefStore(PersistentPrefStore* underlay);
  virtual ~OverlayPersistentPrefStore();

  // Returns true if a value has been set for the |key| in this
  // OverlayPersistentPrefStore, i.e. if it potentially overrides a value
  // from the |underlay_|.
  virtual bool IsSetInOverlay(const std::string& key) const;

  // Methods of PrefStore.
  virtual void AddObserver(PrefStore::Observer* observer);
  virtual void RemoveObserver(PrefStore::Observer* observer);
  virtual bool IsInitializationComplete() const;
  virtual ReadResult GetValue(const std::string& key,
                              const Value** result) const;

  // Methods of PersistentPrefStore.
  virtual ReadResult GetMutableValue(const std::string& key, Value** result);
  virtual void SetValue(const std::string& key, Value* value);
  virtual void SetValueSilently(const std::string& key, Value* value);
  virtual void RemoveValue(const std::string& key);
  virtual bool ReadOnly() const;
  virtual PrefReadError ReadPrefs();
  virtual void ReadPrefsAsync(ReadErrorDelegate* delegate);
  virtual bool WritePrefs();
  virtual void ScheduleWritePrefs();
  virtual void CommitPendingWrite();
  virtual void ReportValueChanged(const std::string& key);

 private:
  // Methods of PrefStore::Observer.
  virtual void OnPrefValueChanged(const std::string& key);
  virtual void OnInitializationCompleted(bool succeeded);

  ObserverList<PrefStore::Observer, true> observers_;
  PrefValueMap overlay_;
  scoped_refptr<PersistentPrefStore> underlay_;

  DISALLOW_COPY_AND_ASSIGN(OverlayPersistentPrefStore);
};

#endif  // CHROME_BROWSER_PREFS_OVERLAY_PERSISTENT_PREF_STORE_H_
