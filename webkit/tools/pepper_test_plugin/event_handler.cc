/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "webkit/tools/pepper_test_plugin/event_handler.h"

#include <stdio.h>
#include <sstream>

#include "webkit/tools/pepper_test_plugin/plugin_object.h"

EventHandler* event_handler = NULL;

// EventHandler ----------------------------------------------------------------

EventHandler::EventHandler(NPP npp)
    : npp_(npp),
    text_box_(NULL) {
}

EventHandler::~EventHandler() {
}

void EventHandler::addText(const char* cstr) {
  NPVariant variant;
  // Create a a string variant to be set for the innerText.
  MakeNPVariant(cstr, &variant);
  // Report the string to the div.
  NPVariant result;
  browser->invokeDefault(npp_, text_box_, &variant, 1, &result);
  // Release the variant.
  browser->releasevariantvalue(&variant);
  browser->releasevariantvalue(&result);
}

std::string EventHandler::EventName(double timestamp, int32 type) {
#if !defined(OS_LINUX)
  std::stringstream strstr;
  strstr.setf(std::ios::fixed, std::ios::floatfield);
  strstr << timestamp << ": ";
  std::string str(strstr.str());
#else
  std::string str;
#endif
  switch (type) {
    case NPEventType_MouseDown:
      return str + "MouseDown";
    case NPEventType_MouseUp:
      return str + "MouseUp";
    case NPEventType_MouseMove:
      return str + "MouseMove";
    case NPEventType_MouseEnter:
      return str + "MouseEnter";
    case NPEventType_MouseLeave:
      return str + "MouseLeave";
    case NPEventType_MouseWheel:
      return str + "MouseWheel";
    case NPEventType_RawKeyDown:
      return str + "RawKeyDown";
    case NPEventType_KeyDown:
      return str + "KeyDown";
    case NPEventType_KeyUp:
      return str + "KeyUp";
    case NPEventType_Char:
      return str + "Char";
    case NPEventType_Minimize:
      return str + "Minimize";
    case NPEventType_Focus:
      return str + "Focus";
    case NPEventType_Device:
      return str + "Device";
    case NPEventType_Undefined:
    default:
      return str + "Undefined";
  }
}

int EventHandler::handle(void* event) {
  NPPepperEvent* npevent = reinterpret_cast<NPPepperEvent*>(event);
  std::stringstream str;
  str << EventName(npevent->timeStampSeconds, npevent->type);
  switch (npevent->type) {
    case NPEventType_MouseDown:
    case NPEventType_MouseUp:
    case NPEventType_MouseMove:
    case NPEventType_MouseEnter:
    case NPEventType_MouseLeave:
      str << ": mod "  << npevent->u.mouse.modifier
          << ", but: " << npevent->u.mouse.button
          << ", x: "   << npevent->u.mouse.x
          << ", y: "   << npevent->u.mouse.y
          << ", click: " << npevent->u.mouse.clickCount;
        break;
    case NPEventType_MouseWheel:
      str << ": mod "  << npevent->u.wheel.modifier
          << ", dx: "  << npevent->u.wheel.deltaX
          << ", dy: "  << npevent->u.wheel.deltaY
          << ", wtx: " << npevent->u.wheel.wheelTicksX
          << ", wty: " << npevent->u.wheel.wheelTicksY
          << ", sbp:"  << npevent->u.wheel.scrollByPage;
      break;
    case NPEventType_RawKeyDown:
    case NPEventType_KeyDown:
    case NPEventType_KeyUp:
      str << ": mod " << npevent->u.key.modifier
          << ", key: " << npevent->u.key.normalizedKeyCode;
      break;
    case NPEventType_Char:
      str << ": mod " << npevent->u.character.modifier << ", text: ";
      size_t i;
      for (i = 0; i < arraysize(npevent->u.character.text); ++i) {
        str << npevent->u.character.text[i] << ' ';
      }
      str << ", unmod: ";
      for (i = 0; i < arraysize(npevent->u.character.unmodifiedText); ++i) {
          str << npevent->u.character.unmodifiedText[i] << ' ';
      }
      break;
    case NPEventType_Minimize:
    case NPEventType_Focus:
    case NPEventType_Device:
      break;
    case NPEventType_Undefined:
    default:
      break;
  }
  addText(str.str().c_str());
  return 0;
}

bool EventHandler::set_text_box(NPObject* text_box_object) {
  text_box_ = text_box_object;
  // Keep a reference to the text box update object.
  browser->retainobject(text_box_object);
  // Announce that we are alive.
  addText("Set the callback for text\n");
  return true;
}

char* EventHandler::string_duplicate(const char* cstr, size_t* len) {
  *len = strlen(cstr);
  char* str = reinterpret_cast<char*>(browser->memalloc(*len + 1));
  if (NULL == str) {
    *len = 0;
    return NULL;
  }
  strcpy(str, cstr);
  return str;
}

void EventHandler::MakeNPVariant(const char* cstr, NPVariant* var) {
  if (NULL == cstr) {
    STRINGN_TO_NPVARIANT(NULL, 0, *var);
    return;
  }
  size_t len;
  char* name = string_duplicate(cstr, &len);
  if (NULL == name) {
    STRINGN_TO_NPVARIANT(NULL, 0, *var);
    return;
  }
  STRINGN_TO_NPVARIANT(name, len, *var);
}
