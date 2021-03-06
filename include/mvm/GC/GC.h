//===----------- GC.h - Garbage Collection Interface -----------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef MVM_GC_H
#define MVM_GC_H

#include <stdint.h>

struct VirtualTable;

class gcRoot {
public:
  virtual           ~gcRoot() {}
  virtual void      tracer(uintptr_t closure) {}
  
  /// getVirtualTable - Returns the virtual table of this object.
  ///
  VirtualTable* getVirtualTable() const {
    return ((VirtualTable**)(this))[0];
  }
  
  /// setVirtualTable - Sets the virtual table of this object.
  ///
  void setVirtualTable(VirtualTable* VT) {
    ((VirtualTable**)(this))[0] = VT;
  }
};

namespace mvm {

class Thread;

class StackScanner {
public:
  virtual void scanStack(mvm::Thread* th, uintptr_t closure) = 0;
  virtual ~StackScanner() {}
};

class UnpreciseStackScanner : public StackScanner {
public:
  virtual void scanStack(mvm::Thread* th, uintptr_t closure);
};

class PreciseStackScanner : public StackScanner {
public:
  virtual void scanStack(mvm::Thread* th, uintptr_t closure);
};

}

#endif
