/*
 * GroovyJITCompiler.cpp
 *
 *  Created on: 11 Jul 2010
 *      Author: chanwit
 */

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "MvmGC.h"
#include "mvm/VirtualMachine.h"

#include "../lib/J3/VMCore/JavaClass.h"
#include "../lib/J3/VMCore/JavaConstantPool.h"
#include "../lib/J3/VMCore/JavaThread.h"
#include "../lib/J3/VMCore/JavaTypes.h"
#include "../lib/J3/VMCore/Jnjvm.h"

#include "j3/JavaJITCompiler.h"
#include "j3/J3Intrinsics.h"
#include "j3/LLVMMaterializer.h"

#include "g7/GroovyJITCompiler.h"

using namespace j3;
using namespace llvm;

namespace g7 {

GroovyJITCompiler::GroovyJITCompiler(const std::string &ModuleID, bool trusted) :
  JavaJITCompiler(ModuleID, trusted) {
}

GroovyJITCompiler::~GroovyJITCompiler() {
	executionEngine->removeModule(TheModule);
	delete executionEngine;
}

}
