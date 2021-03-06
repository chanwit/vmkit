//===-- RuntimeHelpers.cpp - Implement rt.jar functions needed by MMTk  --===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaString.h"

using namespace j3;

extern "C" uint16_t MMTkCharAt(JavaString* str, uint32_t index) {
  llvm_gcroot(str, 0);
  return ArrayUInt16::getElement(JavaString::getValue(str), index);
}

extern "C" JavaObject* MMTkGetClass(JavaObject* obj) {
  llvm_gcroot(obj, 0);
  return JavaObject::getClass(obj)->delegatee[0];
}
