//===----------- Assert.cpp - Implementation of the Assert class  ---------===//
//
//                              The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaObject.h"
#include "JavaThread.h"

using namespace j3;

extern "C" void Java_org_j3_mmtk_Assert_dumpStack__ () { JavaThread::get()->printBacktrace(); abort(); }
