// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_flash_menu_impl.h"

#include "base/utf_string_conversions.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ui/gfx/point.h"
#include "webkit/glue/webmenuitem.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/resource_helper.h"

using ppapi::thunk::PPB_Flash_Menu_API;
using ppapi::TrackedCallback;

namespace webkit {
namespace ppapi {

namespace {

// Maximum depth of submenus allowed (e.g., 1 indicates that submenus are
// allowed, but not sub-submenus).
const size_t kMaxMenuDepth = 2;

// Maximum number of entries in any single menu (including separators).
const size_t kMaxMenuEntries = 50;

// Maximum total number of entries in the |menu_id_map| (see below).
// (Limit to 500 real entries; reserve the 0 action as an invalid entry.)
const size_t kMaxMenuIdMapEntries = 501;

// Converts menu data from one form to another.
//  - |depth| is the current nested depth (call it starting with 0).
//  - |menu_id_map| is such that |menu_id_map[output_item.action] ==
//    input_item.id| (where |action| is what a |WebMenuItem| has, |id| is what a
//    |PP_Flash_MenuItem| has).
bool ConvertMenuData(const PP_Flash_Menu* in_menu,
                     size_t depth,
                     PPB_Flash_Menu_Impl::MenuData* out_menu,
                     std::vector<int32_t>* menu_id_map) {
  if (depth > kMaxMenuDepth || !in_menu)
    return false;

  // Clear the output, just in case.
  out_menu->clear();

  if (!in_menu->count)
    return true;  // Nothing else to do.

  if (!in_menu->items || in_menu->count > kMaxMenuEntries)
    return false;
  for (uint32_t i = 0; i < in_menu->count; i++) {
    WebMenuItem item;

    PP_Flash_MenuItem_Type type = in_menu->items[i].type;
    switch (type) {
      case PP_FLASH_MENUITEM_TYPE_NORMAL:
        item.type = WebMenuItem::OPTION;
        break;
      case PP_FLASH_MENUITEM_TYPE_CHECKBOX:
        item.type = WebMenuItem::CHECKABLE_OPTION;
        break;
      case PP_FLASH_MENUITEM_TYPE_SEPARATOR:
        item.type = WebMenuItem::SEPARATOR;
        break;
      case PP_FLASH_MENUITEM_TYPE_SUBMENU:
        item.type = WebMenuItem::SUBMENU;
        break;
      default:
        return false;
    }
    if (in_menu->items[i].name)
      item.label = UTF8ToUTF16(in_menu->items[i].name);
    if (menu_id_map->size() >= kMaxMenuIdMapEntries)
      return false;
    item.action = static_cast<unsigned>(menu_id_map->size());
    // This sets |(*menu_id_map)[item.action] = in_menu->items[i].id|.
    menu_id_map->push_back(in_menu->items[i].id);
    item.enabled = PPBoolToBool(in_menu->items[i].enabled);
    item.checked = PPBoolToBool(in_menu->items[i].checked);
    if (type == PP_FLASH_MENUITEM_TYPE_SUBMENU) {
      if (!ConvertMenuData(in_menu->items[i].submenu, depth + 1, &item.submenu,
                           menu_id_map))
        return false;
    }

    out_menu->push_back(item);
  }

  return true;
}

}  // namespace

PPB_Flash_Menu_Impl::PPB_Flash_Menu_Impl(PP_Instance instance)
    : Resource(instance),
      selected_id_out_(NULL) {
}

PPB_Flash_Menu_Impl::~PPB_Flash_Menu_Impl() {
}

// static
PP_Resource PPB_Flash_Menu_Impl::Create(PP_Instance instance,
                                        const PP_Flash_Menu* menu_data) {
  scoped_refptr<PPB_Flash_Menu_Impl> menu(new PPB_Flash_Menu_Impl(instance));
  if (!menu->Init(menu_data))
    return 0;
  return menu->GetReference();
}

bool PPB_Flash_Menu_Impl::Init(const PP_Flash_Menu* menu_data) {
  menu_id_map_.clear();
  menu_id_map_.push_back(0);  // Reserve |menu_id_map_[0]|.
  if (!ConvertMenuData(menu_data, 0, &menu_data_, &menu_id_map_)) {
    menu_id_map_.clear();
    return false;
  }

  return true;
}

PPB_Flash_Menu_API* PPB_Flash_Menu_Impl::AsPPB_Flash_Menu_API() {
  return this;
}

int32_t PPB_Flash_Menu_Impl::Show(const PP_Point* location,
                                  int32_t* selected_id_out,
                                  PP_CompletionCallback callback) {
  // |location| is not (currently) optional.
  // TODO(viettrungluu): Make it optional and default to the current mouse pos?
  if (!location)
    return PP_ERROR_BADARGUMENT;

  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;

  if (TrackedCallback::IsPending(callback_))
    return PP_ERROR_INPROGRESS;

  PluginInstance* plugin_instance = ResourceHelper::GetPluginInstance(this);
  if (!plugin_instance)
    return false;

  int32_t rv = plugin_instance->delegate()->ShowContextMenu(
      plugin_instance, this, gfx::Point(location->x, location->y));
  if (rv == PP_OK_COMPLETIONPENDING) {
    // Record callback and output buffers.
    callback_ = new TrackedCallback(this, callback);
    selected_id_out_ = selected_id_out;
  } else {
    // This should never be completed synchronously successfully.
    DCHECK_NE(rv, PP_OK);
  }
  return rv;

  NOTIMPLEMENTED();
  return PP_ERROR_FAILED;
}

void PPB_Flash_Menu_Impl::CompleteShow(int32_t result,
                                       unsigned action) {
  int32_t rv = PP_ERROR_ABORTED;
  if (!callback_->aborted()) {
    CHECK(!callback_->completed());
    rv = result;

    // Write output data.
    if (selected_id_out_ && result == PP_OK) {
      // We reserved action 0 to be invalid.
      if (action == 0 || action >= menu_id_map_.size()) {
        NOTREACHED() << "Invalid action received.";
        rv = PP_ERROR_FAILED;
      } else {
        *selected_id_out_ = menu_id_map_[action];
      }
    }
  }

  selected_id_out_ = NULL;
  TrackedCallback::ClearAndRun(&callback_, rv);
}

}  // namespace ppapi
}  // namespace webkit
