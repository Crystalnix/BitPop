// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search_engines/template_url_table_model.h"

#include "base/callback.h"
#include "base/i18n/rtl.h"
#include "base/stl_util-inl.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/favicon_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "grit/app_resources.h"
#include "grit/generated_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/table_model_observer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/codec/png_codec.h"

// Group IDs used by TemplateURLTableModel.
static const int kMainGroupID = 0;
static const int kOtherGroupID = 1;

// ModelEntry ----------------------------------------------------

// ModelEntry wraps a TemplateURL as returned from the TemplateURL.
// ModelEntry also tracks state information about the URL.

// Icon used while loading, or if a specific favicon can't be found.
static SkBitmap* default_icon = NULL;

class ModelEntry {
 public:
  explicit ModelEntry(TemplateURLTableModel* model,
                      const TemplateURL& template_url)
      : template_url_(template_url),
        load_state_(NOT_LOADED),
        model_(model) {
    if (!default_icon) {
      default_icon = ResourceBundle::GetSharedInstance().
          GetBitmapNamed(IDR_DEFAULT_FAVICON);
    }
  }

  const TemplateURL& template_url() {
    return template_url_;
  }

  SkBitmap GetIcon() {
    if (load_state_ == NOT_LOADED)
      LoadFavicon();
    if (!favicon_.isNull())
      return favicon_;
    return *default_icon;
  }

  // Resets internal status so that the next time the icon is asked for its
  // fetched again. This should be invoked if the url is modified.
  void ResetIcon() {
    load_state_ = NOT_LOADED;
    favicon_ = SkBitmap();
  }

 private:
  // State of the favicon.
  enum LoadState {
    NOT_LOADED,
    LOADING,
    LOADED
  };

  void LoadFavicon() {
    load_state_ = LOADED;
    FaviconService* favicon_service =
        model_->template_url_model()->profile()->GetFaviconService(
            Profile::EXPLICIT_ACCESS);
    if (!favicon_service)
      return;
    GURL favicon_url = template_url().GetFaviconURL();
    if (!favicon_url.is_valid()) {
      // The favicon url isn't always set. Guess at one here.
      if (template_url_.url() && template_url_.url()->IsValid()) {
        GURL url = GURL(template_url_.url()->url());
        if (url.is_valid())
          favicon_url = TemplateURL::GenerateFaviconURL(url);
      }
      if (!favicon_url.is_valid())
        return;
    }
    load_state_ = LOADING;
    favicon_service->GetFavicon(favicon_url, history::FAVICON,
        &request_consumer_,
        NewCallback(this, &ModelEntry::OnFaviconDataAvailable));
  }

  void OnFaviconDataAvailable(
      FaviconService::Handle handle,
      history::FaviconData favicon) {
    load_state_ = LOADED;
    if (favicon.is_valid() && gfx::PNGCodec::Decode(favicon.image_data->front(),
                                                    favicon.image_data->size(),
                                                    &favicon_)) {
      model_->FaviconAvailable(this);
    }
  }

  const TemplateURL& template_url_;
  SkBitmap favicon_;
  LoadState load_state_;
  TemplateURLTableModel* model_;
  CancelableRequestConsumer request_consumer_;

  DISALLOW_COPY_AND_ASSIGN(ModelEntry);
};

// TemplateURLTableModel -----------------------------------------

TemplateURLTableModel::TemplateURLTableModel(
    TemplateURLModel* template_url_model)
    : observer_(NULL),
      template_url_model_(template_url_model) {
  DCHECK(template_url_model);
  template_url_model_->Load();
  template_url_model_->AddObserver(this);
  Reload();
}

TemplateURLTableModel::~TemplateURLTableModel() {
  template_url_model_->RemoveObserver(this);
  STLDeleteElements(&entries_);
  entries_.clear();
}

void TemplateURLTableModel::Reload() {
  STLDeleteElements(&entries_);
  entries_.clear();

  std::vector<const TemplateURL*> urls = template_url_model_->GetTemplateURLs();

  // Keywords that can be made the default first.
  for (std::vector<const TemplateURL*>::iterator i = urls.begin();
       i != urls.end(); ++i) {
    const TemplateURL& template_url = *(*i);
    // NOTE: we don't use ShowInDefaultList here to avoid items bouncing around
    // the lists while editing.
    if (template_url.show_in_default_list())
      entries_.push_back(new ModelEntry(this, template_url));
  }

  last_search_engine_index_ = static_cast<int>(entries_.size());

  // Then the rest.
  for (std::vector<const TemplateURL*>::iterator i = urls.begin();
       i != urls.end(); ++i) {
    const TemplateURL* template_url = *i;
    // NOTE: we don't use ShowInDefaultList here to avoid things bouncing
    // the lists while editing.
    if (!template_url->show_in_default_list() &&
        !template_url->IsExtensionKeyword()) {
      entries_.push_back(new ModelEntry(this, *template_url));
    }
  }

  if (observer_)
    observer_->OnModelChanged();
}

int TemplateURLTableModel::RowCount() {
  return static_cast<int>(entries_.size());
}

string16 TemplateURLTableModel::GetText(int row, int col_id) {
  DCHECK(row >= 0 && row < RowCount());
  const TemplateURL& url = entries_[row]->template_url();
  if (col_id == IDS_SEARCH_ENGINES_EDITOR_DESCRIPTION_COLUMN) {
    string16 url_short_name = url.short_name();
    // TODO(xji): Consider adding a special case if the short name is a URL,
    // since those should always be displayed LTR. Please refer to
    // http://crbug.com/6726 for more information.
    base::i18n::AdjustStringForLocaleDirection(&url_short_name);
    if (template_url_model_->GetDefaultSearchProvider() == &url) {
      return l10n_util::GetStringFUTF16(
          IDS_SEARCH_ENGINES_EDITOR_DEFAULT_ENGINE,
          url_short_name);
    }
    return url_short_name;
  } else if (col_id == IDS_SEARCH_ENGINES_EDITOR_KEYWORD_COLUMN) {
    // Keyword should be domain name. Force it to have LTR directionality.
    string16 keyword = url.keyword();
    keyword = base::i18n::GetDisplayStringInLTRDirectionality(keyword);
    return keyword;
  } else {
    NOTREACHED();
    return string16();
  }
}

SkBitmap TemplateURLTableModel::GetIcon(int row) {
  DCHECK(row >= 0 && row < RowCount());
  return entries_[row]->GetIcon();
}

void TemplateURLTableModel::SetObserver(ui::TableModelObserver* observer) {
  observer_ = observer;
}

bool TemplateURLTableModel::HasGroups() {
  return true;
}

TemplateURLTableModel::Groups TemplateURLTableModel::GetGroups() {
  Groups groups;

  Group search_engine_group;
  search_engine_group.title =
      l10n_util::GetStringUTF16(IDS_SEARCH_ENGINES_EDITOR_MAIN_SEPARATOR);
  search_engine_group.id = kMainGroupID;
  groups.push_back(search_engine_group);

  Group other_group;
  other_group.title =
      l10n_util::GetStringUTF16(IDS_SEARCH_ENGINES_EDITOR_OTHER_SEPARATOR);
  other_group.id = kOtherGroupID;
  groups.push_back(other_group);

  return groups;
}

int TemplateURLTableModel::GetGroupID(int row) {
  DCHECK(row >= 0 && row < RowCount());
  return row < last_search_engine_index_ ? kMainGroupID : kOtherGroupID;
}

void TemplateURLTableModel::Remove(int index) {
  // Remove the observer while we modify the model, that way we don't need to
  // worry about the model calling us back when we mutate it.
  template_url_model_->RemoveObserver(this);
  const TemplateURL* template_url = &GetTemplateURL(index);

  scoped_ptr<ModelEntry> entry(entries_[index]);
  entries_.erase(entries_.begin() + index);
  if (index < last_search_engine_index_)
    last_search_engine_index_--;
  if (observer_)
    observer_->OnItemsRemoved(index, 1);

  // Make sure to remove from the table model first, otherwise the
  // TemplateURL would be freed.
  template_url_model_->Remove(template_url);
  template_url_model_->AddObserver(this);
}

void TemplateURLTableModel::Add(int index, TemplateURL* template_url) {
  DCHECK(index >= 0 && index <= RowCount());
  ModelEntry* entry = new ModelEntry(this, *template_url);
  entries_.insert(entries_.begin() + index, entry);
  if (observer_)
    observer_->OnItemsAdded(index, 1);
  template_url_model_->RemoveObserver(this);
  template_url_model_->Add(template_url);
  template_url_model_->AddObserver(this);
}

void TemplateURLTableModel::ModifyTemplateURL(int index,
                                              const string16& title,
                                              const string16& keyword,
                                              const std::string& url) {
  DCHECK(index >= 0 && index <= RowCount());
  const TemplateURL* template_url = &GetTemplateURL(index);
  template_url_model_->RemoveObserver(this);
  template_url_model_->ResetTemplateURL(template_url, title, keyword, url);
  if (template_url_model_->GetDefaultSearchProvider() == template_url &&
      !TemplateURL::SupportsReplacement(template_url)) {
    // The entry was the default search provider, but the url has been modified
    // so that it no longer supports replacement. Reset the default search
    // provider so that it doesn't point to a bogus entry.
    template_url_model_->SetDefaultSearchProvider(NULL);
  }
  template_url_model_->AddObserver(this);
  ReloadIcon(index);  // Also calls NotifyChanged().
}

void TemplateURLTableModel::ReloadIcon(int index) {
  DCHECK(index >= 0 && index < RowCount());

  entries_[index]->ResetIcon();

  NotifyChanged(index);
}

const TemplateURL& TemplateURLTableModel::GetTemplateURL(int index) {
  return entries_[index]->template_url();
}

int TemplateURLTableModel::IndexOfTemplateURL(
    const TemplateURL* template_url) {
  for (std::vector<ModelEntry*>::iterator i = entries_.begin();
       i != entries_.end(); ++i) {
    ModelEntry* entry = *i;
    if (&(entry->template_url()) == template_url)
      return static_cast<int>(i - entries_.begin());
  }
  return -1;
}

int TemplateURLTableModel::MoveToMainGroup(int index) {
  if (index < last_search_engine_index_)
    return index;  // Already in the main group.

  ModelEntry* current_entry = entries_[index];
  entries_.erase(index + entries_.begin());
  if (observer_)
    observer_->OnItemsRemoved(index, 1);

  const int new_index = last_search_engine_index_++;
  entries_.insert(entries_.begin() + new_index, current_entry);
  if (observer_)
    observer_->OnItemsAdded(new_index, 1);
  return new_index;
}

int TemplateURLTableModel::MakeDefaultTemplateURL(int index) {
  if (index < 0 || index >= RowCount()) {
    NOTREACHED();
    return -1;
  }

  const TemplateURL* keyword = &GetTemplateURL(index);
  const TemplateURL* current_default =
      template_url_model_->GetDefaultSearchProvider();
  if (current_default == keyword)
    return -1;

  template_url_model_->RemoveObserver(this);
  template_url_model_->SetDefaultSearchProvider(keyword);
  template_url_model_->AddObserver(this);

  // The formatting of the default engine is different; notify the table that
  // both old and new entries have changed.
  if (current_default != NULL) {
    int old_index = IndexOfTemplateURL(current_default);
    // current_default may not be in the list of TemplateURLs if the database is
    // corrupt and the default TemplateURL is used from preferences
    if (old_index >= 0)
      NotifyChanged(old_index);
  }
  const int new_index = IndexOfTemplateURL(keyword);
  NotifyChanged(new_index);

  // Make sure the new default is in the main group.
  return MoveToMainGroup(index);
}

void TemplateURLTableModel::NotifyChanged(int index) {
  if (observer_) {
    DCHECK_GE(index, 0);
    observer_->OnItemsChanged(index, 1);
  }
}

void TemplateURLTableModel::FaviconAvailable(ModelEntry* entry) {
  std::vector<ModelEntry*>::iterator i =
      find(entries_.begin(), entries_.end(), entry);
  DCHECK(i != entries_.end());
  NotifyChanged(static_cast<int>(i - entries_.begin()));
}

void TemplateURLTableModel::OnTemplateURLModelChanged() {
  Reload();
}
