// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/cpp/dev/widget_client_dev.h"

#include "ppapi/c/dev/ppp_scrollbar_dev.h"
#include "ppapi/c/dev/ppp_widget_dev.h"
#include "ppapi/cpp/dev/scrollbar_dev.h"
#include "ppapi/cpp/dev/widget_dev.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/module_impl.h"
#include "ppapi/cpp/rect.h"

namespace pp {

namespace {

// PPP_Widget_Dev --------------------------------------------------------------

const char kPPPWidgetInterface[] = PPP_WIDGET_DEV_INTERFACE;

void Widget_Invalidate(PP_Instance instance,
                       PP_Resource widget_id,
                       const PP_Rect* dirty_rect) {
  void* object =
      pp::Instance::GetPerInstanceObject(instance, kPPPWidgetInterface);
  if (!object)
    return;
  return static_cast<WidgetClient_Dev*>(object)->InvalidateWidget(
      Widget_Dev(widget_id), *dirty_rect);
}

static PPP_Widget_Dev widget_interface = {
  &Widget_Invalidate,
};

// PPP_Scrollbar_Dev -----------------------------------------------------------

const char kPPPScrollbarInterface[] = PPP_SCROLLBAR_DEV_INTERFACE;

void Scrollbar_ValueChanged(PP_Instance instance,
                            PP_Resource scrollbar_id,
                            uint32_t value) {
  void* object =
      pp::Instance::GetPerInstanceObject(instance, kPPPScrollbarInterface);
  if (!object)
    return;
  return static_cast<WidgetClient_Dev*>(object)->ScrollbarValueChanged(
      Scrollbar_Dev(scrollbar_id), value);
}

static PPP_Scrollbar_Dev scrollbar_interface = {
  &Scrollbar_ValueChanged,
};

}  // namespace

WidgetClient_Dev::WidgetClient_Dev(Instance* instance)
    : associated_instance_(instance) {
  pp::Module::Get()->AddPluginInterface(kPPPWidgetInterface, &widget_interface);
  associated_instance_->AddPerInstanceObject(kPPPWidgetInterface, this);
  pp::Module::Get()->AddPluginInterface(kPPPScrollbarInterface,
                                        &scrollbar_interface);
  associated_instance_->AddPerInstanceObject(kPPPScrollbarInterface, this);
}

WidgetClient_Dev::~WidgetClient_Dev() {
  associated_instance_->RemovePerInstanceObject(kPPPScrollbarInterface, this);
  associated_instance_->RemovePerInstanceObject(kPPPWidgetInterface, this);
}

}  // namespace pp
