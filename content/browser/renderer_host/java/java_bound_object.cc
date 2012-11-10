// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/java/java_bound_object.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/memory/singleton.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "content/browser/renderer_host/java/java_type.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebBindings.h"

using base::StringPrintf;
using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::GetClass;
using base::android::GetMethodIDFromClassName;
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using WebKit::WebBindings;

// The conversion between JavaScript and Java types is based on the Live
// Connect 2 spec. See
// http://jdk6.java.net/plugin2/liveconnect/#JS_JAVA_CONVERSIONS.

// Note that in some cases, we differ from from the spec in order to maintain
// existing behavior. These areas are marked LIVECONNECT_COMPLIANCE. We may
// revisit this decision in the future.

namespace {

const char kJavaLangClass[] = "java/lang/Class";
const char kJavaLangObject[] = "java/lang/Object";
const char kJavaLangReflectMethod[] = "java/lang/reflect/Method";
const char kGetClass[] = "getClass";
const char kGetDeclaredMethods[] = "getDeclaredMethods";
const char kGetMethods[] = "getMethods";
const char kGetModifiers[] = "getModifiers";
const char kReturningInteger[] = "()I";
const char kReturningJavaLangClass[] = "()Ljava/lang/Class;";
const char kReturningJavaLangReflectMethodArray[] =
    "()[Ljava/lang/reflect/Method;";

// This constant represents the value at java.lang.reflect.Modifier.PUBLIC.
const int kJavaPublicModifier = 1;

// Our special NPObject type.  We extend an NPObject with a pointer to a
// JavaBoundObject.  We also add static methods for each of the NPObject
// callbacks, which are registered by our NPClass. These methods simply
// delegate to the private implementation methods of JavaBoundObject.
struct JavaNPObject : public NPObject {
  JavaBoundObject* bound_object;

  static const NPClass kNPClass;

  static NPObject* Allocate(NPP npp, NPClass* np_class);
  static void Deallocate(NPObject* np_object);
  static bool HasMethod(NPObject* np_object, NPIdentifier np_identifier);
  static bool Invoke(NPObject* np_object, NPIdentifier np_identifier,
                     const NPVariant *args, uint32_t arg_count,
                     NPVariant *result);
  static bool HasProperty(NPObject* np_object, NPIdentifier np_identifier);
  static bool GetProperty(NPObject* np_object, NPIdentifier np_identifier,
                          NPVariant *result);
};

const NPClass JavaNPObject::kNPClass = {
  NP_CLASS_STRUCT_VERSION,
  JavaNPObject::Allocate,
  JavaNPObject::Deallocate,
  NULL,  // NPInvalidate
  JavaNPObject::HasMethod,
  JavaNPObject::Invoke,
  NULL,  // NPInvokeDefault
  JavaNPObject::HasProperty,
  JavaNPObject::GetProperty,
  NULL,  // NPSetProperty,
  NULL,  // NPRemoveProperty
};

NPObject* JavaNPObject::Allocate(NPP npp, NPClass* np_class) {
  JavaNPObject* obj = new JavaNPObject();
  return obj;
}

void JavaNPObject::Deallocate(NPObject* np_object) {
  JavaNPObject* obj = reinterpret_cast<JavaNPObject*>(np_object);
  delete obj->bound_object;
  delete obj;
}

bool JavaNPObject::HasMethod(NPObject* np_object, NPIdentifier np_identifier) {
  std::string name(WebBindings::utf8FromIdentifier(np_identifier));
  JavaNPObject* obj = reinterpret_cast<JavaNPObject*>(np_object);
  return obj->bound_object->HasMethod(name);
}

bool JavaNPObject::Invoke(NPObject* np_object, NPIdentifier np_identifier,
                          const NPVariant* args, uint32_t arg_count,
                          NPVariant* result) {
  std::string name(WebBindings::utf8FromIdentifier(np_identifier));
  JavaNPObject* obj = reinterpret_cast<JavaNPObject*>(np_object);
  return obj->bound_object->Invoke(name, args, arg_count, result);
}

bool JavaNPObject::HasProperty(NPObject* np_object,
                               NPIdentifier np_identifier) {
  // LIVECONNECT_COMPLIANCE: Existing behavior is to return false to indicate
  // that the property is not present. Spec requires supporting this correctly.
  return false;
}

bool JavaNPObject::GetProperty(NPObject* np_object,
                               NPIdentifier np_identifier,
                               NPVariant* result) {
  // LIVECONNECT_COMPLIANCE: Existing behavior is to return false to indicate
  // that the property is undefined. Spec requires supporting this correctly.
  return false;
}

// Calls a Java method through JNI. If the Java method raises an uncaught
// exception, it is cleared and this method returns false. Otherwise, this
// method returns true and the Java method's return value is provided as an
// NPVariant. Note that this method does not do any type coercion. The Java
// return value is simply converted to the corresponding NPAPI type.
bool CallJNIMethod(jobject object, const JavaType& return_type, jmethodID id,
                   jvalue* parameters, NPVariant* result,
                   bool allow_inherited_methods) {
  JNIEnv* env = AttachCurrentThread();
  switch (return_type.type) {
    case JavaType::TypeBoolean:
      BOOLEAN_TO_NPVARIANT(env->CallBooleanMethodA(object, id, parameters),
                           *result);
      break;
    case JavaType::TypeByte:
      INT32_TO_NPVARIANT(env->CallByteMethodA(object, id, parameters), *result);
      break;
    case JavaType::TypeChar:
      INT32_TO_NPVARIANT(env->CallCharMethodA(object, id, parameters), *result);
      break;
    case JavaType::TypeShort:
      INT32_TO_NPVARIANT(env->CallShortMethodA(object, id, parameters),
                         *result);
      break;
    case JavaType::TypeInt:
      INT32_TO_NPVARIANT(env->CallIntMethodA(object, id, parameters), *result);
      break;
    case JavaType::TypeLong:
      DOUBLE_TO_NPVARIANT(env->CallLongMethodA(object, id, parameters),
                          *result);
      break;
    case JavaType::TypeFloat:
      DOUBLE_TO_NPVARIANT(env->CallFloatMethodA(object, id, parameters),
                          *result);
      break;
    case JavaType::TypeDouble:
      DOUBLE_TO_NPVARIANT(env->CallDoubleMethodA(object, id, parameters),
                          *result);
      break;
    case JavaType::TypeVoid:
      env->CallVoidMethodA(object, id, parameters);
      VOID_TO_NPVARIANT(*result);
      break;
    case JavaType::TypeArray:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to not call methods that
      // return arrays. Spec requires calling the method and converting the
      // result to a JavaScript array.
      VOID_TO_NPVARIANT(*result);
      break;
    case JavaType::TypeString: {
      jstring java_string = static_cast<jstring>(
          env->CallObjectMethodA(object, id, parameters));
      // If an exception was raised, we must clear it before calling most JNI
      // methods. ScopedJavaLocalRef is liable to make such calls, so we test
      // first.
      if (base::android::ClearException(env)) {
        return false;
      }
      ScopedJavaLocalRef<jstring> scoped_java_string(env, java_string);
      if (!scoped_java_string.obj()) {
        // LIVECONNECT_COMPLIANCE: Existing behavior is to return undefined.
        // Spec requires returning a null string.
        VOID_TO_NPVARIANT(*result);
        break;
      }
      std::string str =
          base::android::ConvertJavaStringToUTF8(scoped_java_string);
      // Take a copy and pass ownership to the variant. We must allocate using
      // NPN_MemAlloc, to match NPN_ReleaseVariant, which uses NPN_MemFree.
      size_t length = str.length();
      char* buffer = static_cast<char*>(NPN_MemAlloc(length));
      str.copy(buffer, length, 0);
      STRINGN_TO_NPVARIANT(buffer, length, *result);
      break;
    }
    case JavaType::TypeObject: {
      // If an exception was raised, we must clear it before calling most JNI
      // methods. ScopedJavaLocalRef is liable to make such calls, so we test
      // first.
      jobject java_object = env->CallObjectMethodA(object, id, parameters);
      if (base::android::ClearException(env)) {
        return false;
      }
      ScopedJavaLocalRef<jobject> scoped_java_object(env, java_object);
      if (!scoped_java_object.obj()) {
        NULL_TO_NPVARIANT(*result);
        break;
      }
      OBJECT_TO_NPVARIANT(JavaBoundObject::Create(scoped_java_object,
                                                  allow_inherited_methods),
                          *result);
      break;
    }
  }
  return !base::android::ClearException(env);
}

jvalue CoerceJavaScriptNumberToJavaValue(const NPVariant& variant,
                                         const JavaType& target_type,
                                         bool coerce_to_string) {
  // See http://jdk6.java.net/plugin2/liveconnect/#JS_NUMBER_VALUES.
  jvalue result;
  DCHECK(variant.type == NPVariantType_Int32 ||
         variant.type == NPVariantType_Double);
  bool is_double = variant.type == NPVariantType_Double;
  switch (target_type.type) {
    case JavaType::TypeByte:
      result.b = is_double ? static_cast<jbyte>(NPVARIANT_TO_DOUBLE(variant)) :
                             static_cast<jbyte>(NPVARIANT_TO_INT32(variant));
      break;
    case JavaType::TypeChar:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert double to 0.
      // Spec requires converting doubles the same as int32.
      result.c = is_double ? 0 :
                             static_cast<jchar>(NPVARIANT_TO_INT32(variant));
      break;
    case JavaType::TypeShort:
      result.s = is_double ? static_cast<jshort>(NPVARIANT_TO_DOUBLE(variant)) :
                             static_cast<jshort>(NPVARIANT_TO_INT32(variant));
      break;
    case JavaType::TypeInt:
      result.i = is_double ? static_cast<jint>(NPVARIANT_TO_DOUBLE(variant)) :
                             NPVARIANT_TO_INT32(variant);
      break;
    case JavaType::TypeLong:
      result.j = is_double ? static_cast<jlong>(NPVARIANT_TO_DOUBLE(variant)) :
                             NPVARIANT_TO_INT32(variant);
      break;
    case JavaType::TypeFloat:
      result.f = is_double ? static_cast<jfloat>(NPVARIANT_TO_DOUBLE(variant)) :
                             NPVARIANT_TO_INT32(variant);
      break;
    case JavaType::TypeDouble:
      result.d = is_double ? NPVARIANT_TO_DOUBLE(variant) :
                             NPVARIANT_TO_INT32(variant);
      break;
    case JavaType::TypeObject:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to null. Spec
      // requires handling object equivalents of primitive types.
      result.l = NULL;
      break;
    case JavaType::TypeString:
      result.l = coerce_to_string ?
          ConvertUTF8ToJavaString(
              AttachCurrentThread(),
              is_double ? StringPrintf("%.6lg", NPVARIANT_TO_DOUBLE(variant)) :
                          base::Int64ToString(NPVARIANT_TO_INT32(variant))).
                              Release() :
          NULL;
      break;
    case JavaType::TypeBoolean:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to false. Spec
      // requires converting to false for 0 or NaN, true otherwise.
      result.z = JNI_FALSE;
      break;
    case JavaType::TypeArray:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to null. Spec
      // requires raising a JavaScript exception.
      result.l = NULL;
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
      NOTREACHED();
      break;
  }
  return result;
}

jvalue CoerceJavaScriptBooleanToJavaValue(const NPVariant& variant,
                                          const JavaType& target_type,
                                          bool coerce_to_string) {
  // See http://jdk6.java.net/plugin2/liveconnect/#JS_BOOLEAN_VALUES.
  DCHECK_EQ(NPVariantType_Bool, variant.type);
  bool boolean_value = NPVARIANT_TO_BOOLEAN(variant);
  jvalue result;
  switch (target_type.type) {
    case JavaType::TypeBoolean:
      result.z = boolean_value ? JNI_TRUE : JNI_FALSE;
      break;
    case JavaType::TypeObject:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
      // requires handling java.lang.Boolean and java.lang.Object.
      result.l = NULL;
      break;
    case JavaType::TypeString:
      result.l = coerce_to_string ?
          ConvertUTF8ToJavaString(AttachCurrentThread(),
                                  boolean_value ? "true" : "false").Release() :
          NULL;
      break;
    case JavaType::TypeByte:
    case JavaType::TypeChar:
    case JavaType::TypeShort:
    case JavaType::TypeInt:
    case JavaType::TypeLong:
    case JavaType::TypeFloat:
    case JavaType::TypeDouble: {
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to 0. Spec
      // requires converting to 0 or 1.
      jvalue null_value = {0};
      result = null_value;
      break;
    }
    case JavaType::TypeArray:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
      // requires raising a JavaScript exception.
      result.l = NULL;
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
      NOTREACHED();
      break;
  }
  return result;
}

jvalue CoerceJavaScriptStringToJavaValue(const NPVariant& variant,
                                         const JavaType& target_type) {
  // See http://jdk6.java.net/plugin2/liveconnect/#JS_STRING_VALUES.
  DCHECK_EQ(NPVariantType_String, variant.type);
  jvalue result;
  switch (target_type.type) {
    case JavaType::TypeString:
      result.l = ConvertUTF8ToJavaString(
          AttachCurrentThread(),
          base::StringPiece(NPVARIANT_TO_STRING(variant).UTF8Characters,
                            NPVARIANT_TO_STRING(variant).UTF8Length)).Release();
      break;
    case JavaType::TypeObject:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
      // requires handling java.lang.Object.
      result.l = NULL;
      break;
    case JavaType::TypeByte:
    case JavaType::TypeShort:
    case JavaType::TypeInt:
    case JavaType::TypeLong:
    case JavaType::TypeFloat:
    case JavaType::TypeDouble: {
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to 0. Spec
      // requires using valueOf() method of corresponding object type.
      jvalue null_value = {0};
      result = null_value;
      break;
    }
    case JavaType::TypeChar:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to 0. Spec
      // requires using java.lang.Short.decode().
      result.c = 0;
      break;
    case JavaType::TypeBoolean:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to false. Spec
      // requires converting the empty string to false, otherwise true.
      result.z = JNI_FALSE;
      break;
    case JavaType::TypeArray:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
      // requires raising a JavaScript exception.
      result.l = NULL;
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
      NOTREACHED();
      break;
  }
  return result;
}

// Note that this only handles primitive types and strings.
jobject CreateJavaArray(const JavaType& type, jsize length) {
  JNIEnv* env = AttachCurrentThread();
  switch (type.type) {
    case JavaType::TypeBoolean:
      return env->NewBooleanArray(length);
    case JavaType::TypeByte:
      return env->NewByteArray(length);
    case JavaType::TypeChar:
      return env->NewCharArray(length);
    case JavaType::TypeShort:
      return env->NewShortArray(length);
    case JavaType::TypeInt:
      return env->NewIntArray(length);
    case JavaType::TypeLong:
      return env->NewLongArray(length);
    case JavaType::TypeFloat:
      return env->NewFloatArray(length);
    case JavaType::TypeDouble:
      return env->NewDoubleArray(length);
    case JavaType::TypeString: {
      ScopedJavaLocalRef<jclass> clazz(GetClass(env, "java/lang/String"));
      return env->NewObjectArray(length, clazz.obj(), NULL);
    }
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
    case JavaType::TypeArray:
    case JavaType::TypeObject:
      // Not handled.
      NOTREACHED();
  }
  return NULL;
}

// Sets the specified element of the supplied array to the value of the
// supplied jvalue. Requires that the type of the array matches that of the
// jvalue. Handles only primitive types and strings. Note that in the case of a
// string, the array takes a new reference to the string object.
void SetArrayElement(jobject array,
                     const JavaType& type,
                     jsize index,
                     const jvalue& value) {
  JNIEnv* env = AttachCurrentThread();
  switch (type.type) {
    case JavaType::TypeBoolean:
      env->SetBooleanArrayRegion(static_cast<jbooleanArray>(array), index, 1,
                                 &value.z);
      break;
    case JavaType::TypeByte:
      env->SetByteArrayRegion(static_cast<jbyteArray>(array), index, 1,
                              &value.b);
      break;
    case JavaType::TypeChar:
      env->SetCharArrayRegion(static_cast<jcharArray>(array), index, 1,
                              &value.c);
      break;
    case JavaType::TypeShort:
      env->SetShortArrayRegion(static_cast<jshortArray>(array), index, 1,
                               &value.s);
      break;
    case JavaType::TypeInt:
      env->SetIntArrayRegion(static_cast<jintArray>(array), index, 1,
                             &value.i);
      break;
    case JavaType::TypeLong:
      env->SetLongArrayRegion(static_cast<jlongArray>(array), index, 1,
                              &value.j);
      break;
    case JavaType::TypeFloat:
      env->SetFloatArrayRegion(static_cast<jfloatArray>(array), index, 1,
                               &value.f);
      break;
    case JavaType::TypeDouble:
      env->SetDoubleArrayRegion(static_cast<jdoubleArray>(array), index, 1,
                                &value.d);
      break;
    case JavaType::TypeString:
      env->SetObjectArrayElement(static_cast<jobjectArray>(array), index,
                                 value.l);
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
    case JavaType::TypeArray:
    case JavaType::TypeObject:
      // Not handled.
      NOTREACHED();
  }
  base::android::CheckException(env);
}

void ReleaseJavaValueIfRequired(JNIEnv* env,
                                jvalue* value,
                                const JavaType& type) {
  if (type.type == JavaType::TypeString ||
      type.type == JavaType::TypeObject ||
      type.type == JavaType::TypeArray) {
    env->DeleteLocalRef(value->l);
    value->l = NULL;
  }
}

jvalue CoerceJavaScriptValueToJavaValue(const NPVariant& variant,
                                        const JavaType& target_type,
                                        bool coerce_to_string);

// Returns a new local reference to a Java array.
jobject CoerceJavaScriptObjectToArray(const NPVariant& variant,
                                      const JavaType& target_type) {
  DCHECK_EQ(JavaType::TypeArray, target_type.type);
  NPObject* object = NPVARIANT_TO_OBJECT(variant);
  DCHECK_NE(&JavaNPObject::kNPClass, object->_class);

  const JavaType& target_inner_type = *target_type.inner_type.get();
  // LIVECONNECT_COMPLIANCE: Existing behavior is to return null for
  // multi-dimensional arrays. Spec requires handling multi-demensional arrays.
  if (target_inner_type.type == JavaType::TypeArray) {
    return NULL;
  }

  // LIVECONNECT_COMPLIANCE: Existing behavior is to return null for object
  // arrays. Spec requires handling object arrays.
  if (target_inner_type.type == JavaType::TypeObject) {
    return NULL;
  }

  // If the object does not have a length property, return null.
  NPVariant length_variant;
  if (!WebBindings::getProperty(0, object,
                                WebBindings::getStringIdentifier("length"),
                                &length_variant)) {
    WebBindings::releaseVariantValue(&length_variant);
    return NULL;
  }

  // If the length property does not have numeric type, or is outside the valid
  // range for a Java array length, return null.
  jsize length = -1;
  if (NPVARIANT_IS_INT32(length_variant)
      && NPVARIANT_TO_INT32(length_variant) >= 0) {
    length = NPVARIANT_TO_INT32(length_variant);
  } else if (NPVARIANT_IS_DOUBLE(length_variant)
             && NPVARIANT_TO_DOUBLE(length_variant) >= 0.0
             && NPVARIANT_TO_DOUBLE(length_variant) <= kint32max) {
    length = static_cast<jsize>(NPVARIANT_TO_DOUBLE(length_variant));
  }
  WebBindings::releaseVariantValue(&length_variant);
  if (length == -1) {
    return NULL;
  }

  // Create the Java array.
  // TODO(steveblock): Handle failure to create the array.
  jobject result = CreateJavaArray(target_inner_type, length);
  NPVariant value_variant;
  JNIEnv* env = AttachCurrentThread();
  for (jsize i = 0; i < length; ++i) {
    // It seems that getProperty() will set the variant to type void on failure,
    // but this doesn't seem to be documented, so do it explicitly here for
    // safety.
    VOID_TO_NPVARIANT(value_variant);
    // If this fails, for example due to a missing element, we simply treat the
    // value as JavaScript undefined.
    WebBindings::getProperty(0, object, WebBindings::getIntIdentifier(i),
                             &value_variant);
    jvalue element = CoerceJavaScriptValueToJavaValue(value_variant,
                                                      target_inner_type,
                                                      false);
    SetArrayElement(result, target_inner_type, i, element);
    // CoerceJavaScriptValueToJavaValue() creates new local references to
    // strings, objects and arrays. Of these, only strings can occur here.
    // SetArrayElement() causes the array to take its own reference to the
    // string, so we can now release the local reference.
    DCHECK_NE(JavaType::TypeObject, target_inner_type.type);
    DCHECK_NE(JavaType::TypeArray, target_inner_type.type);
    ReleaseJavaValueIfRequired(env, &element, target_inner_type);
    WebBindings::releaseVariantValue(&value_variant);
  }

  return result;
}

jvalue CoerceJavaScriptObjectToJavaValue(const NPVariant& variant,
                                         const JavaType& target_type,
                                         bool coerce_to_string) {
  // This covers both JavaScript objects (including arrays) and Java objects.
  // See http://jdk6.java.net/plugin2/liveconnect/#JS_OTHER_OBJECTS,
  // http://jdk6.java.net/plugin2/liveconnect/#JS_ARRAY_VALUES and
  // http://jdk6.java.net/plugin2/liveconnect/#JS_JAVA_OBJECTS
  DCHECK_EQ(NPVariantType_Object, variant.type);

  NPObject* object = NPVARIANT_TO_OBJECT(variant);
  bool is_java_object = &JavaNPObject::kNPClass == object->_class;

  jvalue result;
  switch (target_type.type) {
    case JavaType::TypeObject:
      if (is_java_object) {
        // LIVECONNECT_COMPLIANCE: Existing behavior is to pass all Java
        // objects. Spec requires passing only Java objects which are
        // assignment-compatibile.
        result.l = AttachCurrentThread()->NewLocalRef(
            JavaBoundObject::GetJavaObject(object));
      } else {
        // LIVECONNECT_COMPLIANCE: Existing behavior is to pass null. Spec
        // requires converting if the target type is
        // netscape.javascript.JSObject, otherwise raising a JavaScript
        // exception.
        result.l = NULL;
      }
      break;
    case JavaType::TypeString:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to
      // "undefined". Spec requires calling toString() on the Java object.
      result.l = coerce_to_string ?
          ConvertUTF8ToJavaString(AttachCurrentThread(), "undefined").
              Release() :
          NULL;
      break;
    case JavaType::TypeByte:
    case JavaType::TypeShort:
    case JavaType::TypeInt:
    case JavaType::TypeLong:
    case JavaType::TypeFloat:
    case JavaType::TypeDouble:
    case JavaType::TypeChar: {
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to 0. Spec
      // requires raising a JavaScript exception.
      jvalue null_value = {0};
      result = null_value;
      break;
    }
    case JavaType::TypeBoolean:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to false. Spec
      // requires raising a JavaScript exception.
      result.z = JNI_FALSE;
      break;
    case JavaType::TypeArray:
      if (is_java_object) {
        // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
        // requires raising a JavaScript exception.
        result.l = NULL;
      } else {
        result.l = CoerceJavaScriptObjectToArray(variant, target_type);
      }
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
      NOTREACHED();
      break;
  }
  return result;
}

jvalue CoerceJavaScriptNullOrUndefinedToJavaValue(const NPVariant& variant,
                                                  const JavaType& target_type,
                                                  bool coerce_to_string) {
  // See http://jdk6.java.net/plugin2/liveconnect/#JS_NULL.
  DCHECK(variant.type == NPVariantType_Null ||
         variant.type == NPVariantType_Void);
  jvalue result;
  switch (target_type.type) {
    case JavaType::TypeObject:
      result.l = NULL;
      break;
    case JavaType::TypeString:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert undefined to
      // "undefined". Spec requires converting undefined to NULL.
      result.l = (coerce_to_string && variant.type == NPVariantType_Void) ?
          ConvertUTF8ToJavaString(AttachCurrentThread(), "undefined").
              Release() :
          NULL;
      break;
    case JavaType::TypeByte:
    case JavaType::TypeChar:
    case JavaType::TypeShort:
    case JavaType::TypeInt:
    case JavaType::TypeLong:
    case JavaType::TypeFloat:
    case JavaType::TypeDouble: {
      jvalue null_value = {0};
      result = null_value;
      break;
    }
    case JavaType::TypeBoolean:
      result.z = JNI_FALSE;
      break;
    case JavaType::TypeArray:
      // LIVECONNECT_COMPLIANCE: Existing behavior is to convert to NULL. Spec
      // requires raising a JavaScript exception.
      result.l = NULL;
      break;
    case JavaType::TypeVoid:
      // Conversion to void must never happen.
      NOTREACHED();
      break;
  }
  return result;
}

// coerce_to_string means that we should try to coerce all JavaScript values to
// strings when required, rather than simply converting to NULL. This is used
// to maintain current behaviour, which differs slightly depending upon whether
// or not the coercion in question is for an array element.
//
// Note that the jvalue returned by this method may contain a new local
// reference to an object (string, object or array). This must be released by
// the caller.
jvalue CoerceJavaScriptValueToJavaValue(const NPVariant& variant,
                                        const JavaType& target_type,
                                        bool coerce_to_string) {
  // Note that in all these conversions, the relevant field of the jvalue must
  // always be explicitly set, as jvalue does not initialize its fields.

  switch (variant.type) {
    case NPVariantType_Int32:
    case NPVariantType_Double:
      return CoerceJavaScriptNumberToJavaValue(variant, target_type,
                                               coerce_to_string);
    case NPVariantType_Bool:
      return CoerceJavaScriptBooleanToJavaValue(variant, target_type,
                                                coerce_to_string);
    case NPVariantType_String:
      return CoerceJavaScriptStringToJavaValue(variant, target_type);
    case NPVariantType_Object:
      return CoerceJavaScriptObjectToJavaValue(variant, target_type,
                                               coerce_to_string);
    case NPVariantType_Null:
    case NPVariantType_Void:
      return CoerceJavaScriptNullOrUndefinedToJavaValue(variant, target_type,
                                                        coerce_to_string);
  }
  NOTREACHED();
  return jvalue();
}

}  // namespace


NPObject* JavaBoundObject::Create(const JavaRef<jobject>& object,
                                  bool allow_inherited_methods) {
  // The first argument (a plugin's instance handle) is passed through to the
  // allocate function directly, and we don't use it, so it's ok to be 0.
  // The object is created with a ref count of one.
  NPObject* np_object = WebBindings::createObject(0, const_cast<NPClass*>(
      &JavaNPObject::kNPClass));
  // The NPObject takes ownership of the JavaBoundObject.
  reinterpret_cast<JavaNPObject*>(np_object)->bound_object =
      new JavaBoundObject(object, allow_inherited_methods);
  return np_object;
}

JavaBoundObject::JavaBoundObject(const JavaRef<jobject>& object,
                                 bool allow_inherited_methods)
    : java_object_(object),
      are_methods_set_up_(false),
      allow_inherited_methods_(allow_inherited_methods) {
  // We don't do anything with our Java object when first created. We do it all
  // lazily when a method is first invoked.
}

JavaBoundObject::~JavaBoundObject() {
}

jobject JavaBoundObject::GetJavaObject(NPObject* object) {
  DCHECK_EQ(&JavaNPObject::kNPClass, object->_class);
  JavaBoundObject* jbo = reinterpret_cast<JavaNPObject*>(object)->bound_object;
  return jbo->java_object_.obj();
}

bool JavaBoundObject::HasMethod(const std::string& name) const {
  EnsureMethodsAreSetUp();
  return methods_.find(name) != methods_.end();
}

bool JavaBoundObject::Invoke(const std::string& name, const NPVariant* args,
                             size_t arg_count, NPVariant* result) {
  EnsureMethodsAreSetUp();

  // Get all methods with the correct name.
  std::pair<JavaMethodMap::const_iterator, JavaMethodMap::const_iterator>
      iters = methods_.equal_range(name);
  if (iters.first == iters.second) {
    return false;
  }

  // Take the first method with the correct number of arguments.
  JavaMethod* method = NULL;
  for (JavaMethodMap::const_iterator iter = iters.first; iter != iters.second;
       ++iter) {
    if (iter->second->num_parameters() == arg_count) {
      method = iter->second.get();
      break;
    }
  }
  if (!method) {
    return false;
  }

  // Coerce
  std::vector<jvalue> parameters(arg_count);
  for (size_t i = 0; i < arg_count; ++i) {
    parameters[i] = CoerceJavaScriptValueToJavaValue(args[i],
                                                     method->parameter_type(i),
                                                     true);
  }

  // Call
  bool ok = CallJNIMethod(java_object_.obj(), method->return_type(),
                          method->id(), &parameters[0], result,
                          allow_inherited_methods_);

  // Now that we're done with the jvalue, release any local references created
  // by CoerceJavaScriptValueToJavaValue().
  JNIEnv* env = AttachCurrentThread();
  for (size_t i = 0; i < arg_count; ++i) {
    ReleaseJavaValueIfRequired(env, &parameters[i], method->parameter_type(i));
  }

  return ok;
}

void JavaBoundObject::EnsureMethodsAreSetUp() const {
  if (are_methods_set_up_)
    return;
  are_methods_set_up_ = true;

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jclass> clazz(env, static_cast<jclass>(
      env->CallObjectMethod(java_object_.obj(),  GetMethodIDFromClassName(
          env,
          kJavaLangObject,
          kGetClass,
          kReturningJavaLangClass))));

  const char* get_method = allow_inherited_methods_ ?
      kGetMethods : kGetDeclaredMethods;

  ScopedJavaLocalRef<jobjectArray> methods(env, static_cast<jobjectArray>(
      env->CallObjectMethod(clazz.obj(), GetMethodIDFromClassName(
          env,
          kJavaLangClass,
          get_method,
          kReturningJavaLangReflectMethodArray))));

  size_t num_methods = env->GetArrayLength(methods.obj());
  if (num_methods <= 0)
    return;

  for (size_t i = 0; i < num_methods; ++i) {
    ScopedJavaLocalRef<jobject> java_method(
        env,
        env->GetObjectArrayElement(methods.obj(), i));

    bool is_method_allowed = true;
    if (!allow_inherited_methods_) {
      jint modifiers = env->CallIntMethod(java_method.obj(),
                                          GetMethodIDFromClassName(
                                              env,
                                              kJavaLangReflectMethod,
                                              kGetModifiers,
                                              kReturningInteger));
      is_method_allowed &= (modifiers & kJavaPublicModifier);
    }

    if (is_method_allowed) {
      JavaMethod* method = new JavaMethod(java_method);
      methods_.insert(std::make_pair(method->name(), method));
    }
  }
}
