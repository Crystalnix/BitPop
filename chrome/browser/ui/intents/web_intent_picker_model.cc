// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/intents/web_intent_picker_model.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "chrome/browser/ui/intents/web_intent_picker_model_observer.h"
#include "grit/ui_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"

WebIntentPickerModel::WebIntentPickerModel()
    : observer_(NULL),
      inline_disposition_index_(std::string::npos) {
}

WebIntentPickerModel::~WebIntentPickerModel() {
  DestroyItems();
}

void WebIntentPickerModel::AddItem(const string16& title,
                                   const GURL& url,
                                   Disposition disposition) {
  items_.push_back(new Item(title, url, disposition));
  if (observer_)
    observer_->OnModelChanged(this);
}

void WebIntentPickerModel::RemoveItemAt(size_t index) {
  DCHECK(index < items_.size());
  std::vector<Item*>::iterator iter = items_.begin() + index;
  delete *iter;
  items_.erase(iter);
  if (observer_)
    observer_->OnModelChanged(this);
}

void WebIntentPickerModel::Clear() {
  DestroyItems();
  inline_disposition_index_ = std::string::npos;
  if (observer_)
    observer_->OnModelChanged(this);
}

const WebIntentPickerModel::Item& WebIntentPickerModel::GetItemAt(
    size_t index) const {
  DCHECK(index < items_.size());
  return *items_[index];
}

size_t WebIntentPickerModel::GetItemCount() const {
  return items_.size();
}

void WebIntentPickerModel::UpdateFaviconAt(size_t index,
                                           const gfx::Image& image) {
  DCHECK(index < items_.size());
  items_[index]->favicon = image;
  if (observer_)
    observer_->OnFaviconChanged(this, index);
}

void WebIntentPickerModel::SetInlineDisposition(size_t index) {
  DCHECK(index < items_.size());
  inline_disposition_index_ = index;
  if (observer_)
    observer_->OnInlineDisposition(this);
}

bool WebIntentPickerModel::IsInlineDisposition() const {
  return inline_disposition_index_ != std::string::npos;
}

void WebIntentPickerModel::DestroyItems() {
  STLDeleteElements(&items_);
}

WebIntentPickerModel::Item::Item(const string16& title,
                                 const GURL& url,
                                 Disposition disposition)
    : title(title),
      url(url),
      favicon(ui::ResourceBundle::GetSharedInstance().GetNativeImageNamed(
          IDR_DEFAULT_FAVICON)),
      disposition(disposition) {
}

WebIntentPickerModel::Item::~Item() {
}
