//===------- LockedMap.cpp - Implementation of the UTF8 map ---------------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <map>

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaString.h"
#include "JavaTypes.h"
#include "LockedMap.h"
#include "Zip.h"

#include <cstring>

using namespace j3;

void StringMap::insert(JavaString* str) {
  llvm_gcroot(str, 0);
  map.insert(std::make_pair(JavaString::getValue(str), str));
}
