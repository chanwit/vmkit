//===------------ CLIJit.cpp - CLI just in time compiler ------------------===//
//
//                              N3
//
// This file is distributed under the University Of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mvm/JIT.h"

#include "Assembly.h"
#include "CLIAccess.h"
#include "CLIJit.h"
#include "MSCorlib.h"
#include "NativeUtil.h"
#include "N3.h"
#include "N3ModuleProvider.h"
#include "Reader.h"
#include "VMArray.h"
#include "VMCache.h"
#include "VMClass.h"
#include "VMThread.h"
#include "N3MetaType.h"

#include <llvm/CallingConv.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Function.h>
#include <llvm/Instructions.h>
#include <llvm/LinkAllPasses.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Type.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/MutexGuard.h>

#include "N3Debug.h"

#include "types.h"

using namespace llvm;
using namespace n3;

void ExceptionBlockDesc::print(mvm::PrintBuffer* buf) const {
  buf->write("Exception<>");
}

#ifdef WITH_TRACER
// for structs
static void traceStruct(VMCommonClass* cl, BasicBlock* block, Value* arg) {
#ifdef MULTIPLE_GC
  Value* GC = ++(block->getParent()->arg_begin());
#endif

  for (std::vector<VMField*>::iterator i = cl->virtualFields.begin(), 
            e = cl->virtualFields.end(); i!= e; ++i) {

    VMField* field = *i;
    if (field->signature->super == MSCorlib::pValue) {
      if (!field->signature->isPrimitive) {
        Value* ptr = GetElementPtrInst::Create(arg, field->offset, "",
                                           block);
        traceStruct(field->signature, block, ptr);
      } else if (field->signature == MSCorlib::pIntPtr || 
                 field->signature == MSCorlib::pUIntPtr)  {
        Value* valCast = new BitCastInst(arg, VMObject::llvmType, "", block);
#ifdef MULTIPLE_GC
        std::vector<Value*> Args;
        Args.push_back(valCast);
        Args.push_back(GC);
        CallInst::Create(CLIJit::markAndTraceLLVM, Args.begin(), Args.end(),
                         "", block);
#else
        CallInst::Create(CLIJit::markAndTraceLLVM, valCast, "", block);
#endif
      }
    } else if (field->signature->super != MSCorlib::pEnum) {
      Value* valCast = new BitCastInst(arg, VMObject::llvmType, "", block);
#ifdef MULTIPLE_GC
      std::vector<Value*> Args;
      Args.push_back(valCast);
      Args.push_back(GC);
      CallInst::Create(CLIJit::markAndTraceLLVM, Args.begin(), Args.end(),
                       "", block);
#else
      CallInst::Create(CLIJit::markAndTraceLLVM, valCast, "", block);
#endif
    }
  }
}


// Always classes
static void traceClass(VMCommonClass* cl, BasicBlock* block, Value* arg, 
                       std::vector<VMField*>& fields, bool boxed = false) {
#ifdef MULTIPLE_GC
  Value* GC = ++(block->getParent()->arg_begin());
#endif
 
  Constant* zero = cl->vm->module->constantZero;
  for (std::vector<VMField*>::iterator i = fields.begin(), 
            e = fields.end(); i!= e; ++i) {
    VMField* field = *i;
    if (field->signature->super == MSCorlib::pValue) {
      std::vector<Value*> args; //size = 2
      args.push_back(zero);
      if (boxed) {
        ConstantInt* CI = dyn_cast<ConstantInt>(field->offset);
        args.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), CI->getValue() + 1));
      } else {
        args.push_back(field->offset);
      }
      Value* ptr = GetElementPtrInst::Create(arg, args.begin(), args.end(), "",
                                         block);
      traceStruct(field->signature, block, ptr);
    } else if (field->signature->super != MSCorlib::pEnum) {
      std::vector<Value*> args; //size = 2
      args.push_back(zero);
      if (boxed) {
        ConstantInt* CI = dyn_cast<ConstantInt>(field->offset);
        args.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), CI->getValue() + 1));
      } else {
        args.push_back(field->offset);
      }
      Value* ptr = GetElementPtrInst::Create(arg, args.begin(), args.end(), "",
                                         block);
      Value* val = new LoadInst(ptr, "", block);
      Value* valCast = new BitCastInst(val, VMObject::llvmType, "", block);
#ifdef MULTIPLE_GC
        std::vector<Value*> Args;
        Args.push_back(valCast);
        Args.push_back(GC);
        CallInst::Create(CLIJit::markAndTraceLLVM, Args.begin(), Args.end(),
                         "", block);
#else
      CallInst::Create(CLIJit::markAndTraceLLVM, valCast, "", block);
#endif
    }
  }
}
#endif


N3VirtualTable* CLIJit::makeArrayVT(VMClassArray* cl) {
	VMClass *super = (VMClass*)cl->super;
  N3VirtualTable * res = 
		new(cl->assembly->allocator, super->vtSize) N3VirtualTable(VMObject::getN3VirtualTable(super->virtualInstance), super->vtSize);

#ifdef WITH_TRACER  
  Function* func = Function::Create(markAndTraceLLVMType,
                                GlobalValue::ExternalLinkage,
                                "markAndTraceObject",
                                cl->vm->getLLVMModule());
  Argument* arg = func->arg_begin();
#ifdef MULTIPLE_GC 
  Argument* GC = ++(func->arg_begin());
#endif
    // Constant Definitions
  Constant* const_int32_8 = cl->vm->module->constantZero;
  Constant* const_int32_9 = cl->vm->module->constantOne;
  Constant* const_int32_10 = cl->vm->module->constantTwo;
  
  
  // Function Definitions
  
  {
    BasicBlock* label_entry = BasicBlock::Create(getGlobalContext(), "entry",func,0);
    BasicBlock* label_bb = BasicBlock::Create(getGlobalContext(), "bb",func,0);
    BasicBlock* label_return = BasicBlock::Create(getGlobalContext(), "return",func,0);
    
    Value* ptr_v = new BitCastInst(arg, cl->naturalType, "", label_entry);
    
    // Block entry (label_entry)
    std::vector<Value*> ptr_tmp918_indices;
    ptr_tmp918_indices.push_back(const_int32_8);
    ptr_tmp918_indices.push_back(const_int32_9);
    Instruction* ptr_tmp918 = 
      GetElementPtrInst::Create(ptr_v, ptr_tmp918_indices.begin(), 
                                ptr_tmp918_indices.end(), "tmp918", 
                                label_entry);
    LoadInst* int32_tmp1019 = new LoadInst(ptr_tmp918, "tmp1019", false, 
                                           label_entry);

    ICmpInst* int1_tmp1221 = new ICmpInst(*label_entry, ICmpInst::ICMP_SGT,
                                          int32_tmp1019, 
                                          const_int32_8, "tmp1221");

    BranchInst::Create(label_bb, label_return, int1_tmp1221, label_entry);
    
    // Block bb (label_bb)
    Argument* fwdref_12 = new Argument(IntegerType::get(getGlobalContext(), 32));
    PHINode* int32_i_015_0 = PHINode::Create(Type::getInt32Ty(getGlobalContext()), "i.015.0", 
                                             label_bb);
    int32_i_015_0->reserveOperandSpace(2);
    int32_i_015_0->addIncoming(fwdref_12, label_bb);
    int32_i_015_0->addIncoming(const_int32_8, label_entry);
    
    std::vector<Value*> ptr_tmp3_indices;
    ptr_tmp3_indices.push_back(const_int32_8);
    ptr_tmp3_indices.push_back(const_int32_10);
    ptr_tmp3_indices.push_back(int32_i_015_0);
    Instruction* ptr_tmp3 = 
      GetElementPtrInst::Create(ptr_v, ptr_tmp3_indices.begin(), 
                                ptr_tmp3_indices.end(), "tmp3", label_bb);

    if (cl->baseClass->super == MSCorlib::pValue) {
      traceStruct(cl->baseClass, label_bb, ptr_tmp3);
    } else if (cl->baseClass->super != MSCorlib::pEnum) {
      LoadInst* ptr_tmp4 = new LoadInst(ptr_tmp3, "tmp4", false, label_bb);
      Value* arg = new BitCastInst(ptr_tmp4, VMObject::llvmType, "", label_bb);
#ifdef MULTIPLE_GC
      std::vector<Value*> Args;
      Args.push_back(arg);
      Args.push_back(GC);
      CallInst::Create(markAndTraceLLVM, Args.begin(), Args.end(), "",
                       label_bb);
#else
      CallInst::Create(markAndTraceLLVM, arg, "", label_bb);
#endif
    }
    BinaryOperator* int32_tmp6 = 
      BinaryOperator::Create(Instruction::Add, int32_i_015_0, const_int32_9,
                             "tmp6", label_bb);
    LoadInst* int32_tmp10 = new LoadInst(ptr_tmp918, "tmp10", false, label_bb);
    ICmpInst* int1_tmp12 = new ICmpInst(*label_bb, ICmpInst::ICMP_SGT,
                                        int32_tmp10, int32_tmp6, "tmp12");
    BranchInst::Create(label_bb, label_return, int1_tmp12, label_bb);
    
    // Block return (label_return)
    ReturnInst::Create(getGlobalContext(), label_return);
    
    // Resolve Forward References
    fwdref_12->replaceAllUsesWith(int32_tmp6); delete fwdref_12;
    
  }
  
  void* tracer = mvm::MvmModule::executionEngine->getPointerToGlobal(func);
  ((void**)res)[VT_TRACER_OFFSET] = tracer;
  cl->virtualTracer = func;
#endif

#define CASE_ARRAY(name, type)																					\
	if(cl->baseClass == MSCorlib::p##name) {															\
		((void**)res)[VT_PRINT_OFFSET] = ((void **)(unsigned int)Array##name::do_print); \
  } else
	
	ON_TYPES(CASE_ARRAY, _F_NT) {}

#undef CASE_ARRAY

  return res;
}

N3VirtualTable* CLIJit::makeVT(VMClass* cl, bool stat) {
	int n                = N3VirtualTable::baseVtSize();
  N3VirtualTable * res =
		stat ?       
		new(cl->assembly->allocator, n)          N3VirtualTable((uintptr_t)0, 
																														(uintptr_t)0, 
																														(uintptr_t)VMObject::_trace, 
																														(uintptr_t)VMObject::_print, 
																														(uintptr_t)mvm::Object::default_hashCode) : 
		(cl->super ? 
		 new(cl->assembly->allocator, cl->vtSize) N3VirtualTable(VMObject::getN3VirtualTable(((VMClass *)cl->super)->virtualInstance), 
																														 n, 
																														 cl->vtSize) :
		 new(cl->assembly->allocator, cl->vtSize) N3VirtualTable((uintptr_t)0, 
																														 (uintptr_t)0, 
																														 (uintptr_t)VMObject::_trace, 
																														 (uintptr_t)VMObject::_print, 
																														 (uintptr_t)mvm::Object::default_hashCode));
		

#ifdef WITH_TRACER  
  const Type* type = stat ? cl->staticType : cl->virtualType;
  std::vector<VMField*> &fields = stat ? cl->staticFields : cl->virtualFields;
  
  Function* func = Function::Create(markAndTraceLLVMType,
																		GlobalValue::ExternalLinkage,
																		"markAndTraceObject",
																		cl->vm->getLLVMModule());

  Argument* arg = func->arg_begin();
#ifdef MULTIPLE_GC 
  Argument* GC = ++(func->arg_begin());
#endif
  BasicBlock* block = BasicBlock::Create(getGlobalContext(), "", func);
  llvm::Value* realArg = new BitCastInst(arg, type, "", block);
 
#ifdef MULTIPLE_GC
  std::vector<Value*> Args;
  Args.push_back(arg);
  Args.push_back(GC);
  if (stat || cl->super == 0) {
    CallInst::Create(vmObjectTracerLLVM, Args.begin(), Args.end(), "", block);
  } else {
    CallInst::Create(((VMClass*)cl->super)->virtualTracer, Args.begin(),
                     Args.end(), "", block);
  }
#else
  if (stat || cl->super == 0) {
    CallInst::Create(vmObjectTracerLLVM, arg, "", block);
  } else {
    CallInst::Create(((VMClass*)cl->super)->virtualTracer, arg, "", block);
  }
#endif
  
  traceClass(cl, block, realArg, fields, (cl->super == MSCorlib::pValue && !stat));
  ReturnInst::Create(getGlobalContext(), block);

  void* tracer = mvm::MvmModule::executionEngine->getPointerToGlobal(func);
  ((void**)res)[VT_TRACER_OFFSET] = tracer;
  
  if (!stat) {
    cl->virtualTracer = func;
  } else {
    cl->staticTracer = func;
  }
#endif
	
  return res;
}

BasicBlock* CLIJit::createBasicBlock(const char* name) {
  return BasicBlock::Create(getGlobalContext(), name, llvmFunction);
}

void CLIJit::setCurrentBlock(BasicBlock* newBlock) {

  std::vector<Value*> newStack;
  uint32 index = 0;
  for (BasicBlock::iterator i = newBlock->begin(), e = newBlock->end(); i != e;
       ++i, ++index) {
    if (!(isa<PHINode>(i))) {
      break;
    } else {
      newStack.push_back(i);
    }
  }
  
  stack = newStack;
  currentBlock = newBlock;
}

extern void convertValue(Value*& val, const Type* t1, BasicBlock* currentBlock);

static void testPHINodes(BasicBlock* dest, BasicBlock* insert, CLIJit* jit) {
  if(dest->empty()) {
    for (std::vector<Value*>::iterator i = jit->stack.begin(),
            e = jit->stack.end(); i!= e; ++i) {
      Value* cur = (*i);
      PHINode* node = PHINode::Create(cur->getType(), "", dest);
      node->addIncoming(cur, insert);
    }
  } else {
    std::vector<Value*>::iterator stackit = jit->stack.begin();
    for (BasicBlock::iterator i = dest->begin(), e = dest->end(); i != e;
         ++i) {
      if (!(isa<PHINode>(i))) {
        break;
      } else {
        Instruction* ins = i;
        Value* cur = (*stackit);
        convertValue(cur, ins->getType(), insert);
        ((PHINode*)ins)->addIncoming(cur, insert);
        ++stackit;
      }
    }
  }
}

void CLIJit::branch(llvm::BasicBlock* dest, llvm::BasicBlock* insert) {
  testPHINodes(dest, insert, this);
  BranchInst::Create(dest, insert);
}

void CLIJit::branch(llvm::Value* test, llvm::BasicBlock* ifTrue,
                    llvm::BasicBlock* ifFalse, llvm::BasicBlock* insert) {  
  testPHINodes(ifTrue, insert, this);
  testPHINodes(ifFalse, insert, this);
  BranchInst::Create(ifTrue, ifFalse, test, insert);
}

Value* CLIJit::pop() {
  assert(stack.size());
  Value* ret = top();
  stack.pop_back();
  return ret;
}

Value* CLIJit::top() {
  return stack.back();
}

void CLIJit::push(Value* val) {
  assert(val);
  stack.push_back(val);
}

Value* CLIJit::changeType(Value* val, const Type* type) {
  const Type* valType = val->getType();
  if (type->isIntegerTy()) {
    if (valType == PointerType::getUnqual(type)) {
      // in cast it's a struct
      val = new LoadInst(val, "", currentBlock);
    }
    else if (type->getPrimitiveSizeInBits() < 
             valType->getPrimitiveSizeInBits()) {
      val = new TruncInst(val, type, "", currentBlock);
    } else {
      val = new SExtInst(val, type, "", currentBlock);
    }
  } else if (type == Type::getFloatTy(getGlobalContext())) {
    val = new FPTruncInst(val, type, "", currentBlock);
  } else if (type == Type::getDoubleTy(getGlobalContext())) {
    val = new FPExtInst(val, type, "", currentBlock);
  } else {
    val = new BitCastInst(val, type, "", currentBlock);
  }
  return val;
}

void CLIJit::makeArgs(const FunctionType* type, std::vector<Value*>& Args,
                      bool structReturn) {
  uint32 size = type->getNumParams();
  Value** args = (Value**)alloca(sizeof(Value*) * size);
  sint32 index = size - 1;
  FunctionType::param_iterator e = type->param_end();
  e--;
  if (structReturn) { e--; index--; size--; }
  for (; index >= 0; --e, --index) {
    const Type* argType = (*e);
    Value* val = pop();
    if (val->getType() != argType) {
      val = changeType(val, argType);
    }
    args[index] = val;
  }

  for (uint32 i = 0; i < size; ++i) {
    Args.push_back(args[i]);
  }
}

Instruction* CLIJit::lowerMathOps(VMMethod* meth, 
                                  std::vector<Value*>& args) {
  
  if (meth->name == N3::sqrt) {
    return CallInst::Create(module->func_llvm_sqrt_f64, args[0], "tmp1", 
                            currentBlock);
  } else if (meth->name == N3::sin) {
    return CallInst::Create(module->func_llvm_sin_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::cos) {
    return CallInst::Create(module->func_llvm_cos_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::exp) {
    return CallInst::Create(module->func_llvm_exp_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::log) {
    return CallInst::Create(module->func_llvm_log_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::floor) {
    return CallInst::Create(module->func_llvm_floor_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::log10) {
    return CallInst::Create(module->func_llvm_log10_f64, args[0], "tmp1",
                            currentBlock);
  } else if (meth->name == N3::pow) {
    Instruction* val = CallInst::Create(module->func_llvm_pow_f64, 
                                        args.begin(), args.end(), "tmp1",
                                        currentBlock);
    return val;
  }
  return 0;

}

Instruction* CLIJit::invokeInline(VMMethod* meth, 
                                  std::vector<Value*>& args, VMGenericClass* genClass, VMGenericMethod* genMethod) {  
	mvm::BumpPtrAllocator *a = new mvm::BumpPtrAllocator();
  CLIJit* jit = new(*a, "CLIJit") CLIJit(*a);
  jit->module = meth->classDef->vm->module;
  jit->compilingClass = meth->classDef; 
  jit->compilingMethod = meth;
  
  jit->unifiedUnreachable = unifiedUnreachable;
  jit->inlineMethods = inlineMethods;
  jit->inlineMethods[meth] = true;
  Instruction* ret = jit->inlineCompile(llvmFunction, currentBlock, 
                                        currentExceptionBlock, args, static_cast<VMGenericClass*>(jit->compilingClass), genMethod);
  inlineMethods[meth] = false;

	delete a;
  return ret;
}


void CLIJit::invoke(uint32 value, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  VMMethod* meth = compilingClass->assembly->getMethodFromToken(value, genClass, genMethod);

  if (meth->classDef->isArray) {
    uint8 func = 0;
    N3* vm = VMThread::get()->getVM();
    if (meth->name == vm->asciizToUTF8("Set")) {
      func = 0;
    } else if (meth->name == vm->asciizToUTF8("Get")) {
      func = 1;
    } else if (meth->name == vm->asciizToUTF8("Address")) {
      func = 2;
    } else {
      vm->error("implement me %s", mvm::PrintBuffer(meth->name).cString());
    }
      
    VMClassArray* type = (VMClassArray*)meth->classDef;
    uint32 dims = type->dims;
    Value** args = (Value**)alloca(sizeof(Value*) * dims);
    Value* val = 0;
    if (func == 0) {
      val = pop();
    }
    for (sint32 i = dims - 1; i >= 0 ; --i) {
      args[i] = pop();
    }
    Value* obj = pop();
    VMClassArray* base = type;
    for (uint32 v = 0; v < dims; ++v) {
      std::vector<Value*> Args;
      Args.push_back(module->constantZero);
      Args.push_back(module->constantTwo);
      Args.push_back(args[v]);
      obj = verifyAndComputePtr(obj, args[v], base->naturalType, true);
      if (v != dims - 1) {
        base = (VMClassArray*)base->baseClass;
        obj = new LoadInst(obj, "", currentBlock);
      }
    }
    
    if (func == 0) {
      new StoreInst(val, obj, false, currentBlock);
    } else if (func == 1) {
      push(new LoadInst(obj, "", currentBlock));
    } else {
      push(obj);
    }
    return;
  }

  std::vector<Value*> Args;
  const llvm::FunctionType* type = meth->getSignature(genMethod);
  makeArgs(type, Args, meth->structReturn);
  
  if (meth->classDef->nameSpace == N3::system && 
      meth->classDef->name == N3::math) {
    Value* val = lowerMathOps(meth, Args); 
    if (val) {
      push(val);
      return;
    }
  } else if (meth->classDef->nameSpace == N3::system && 
             meth->classDef->name == N3::doubleName) {
    if (meth->name == N3::isNan) {
      push(new FCmpInst(*currentBlock, FCmpInst::FCMP_UNO, Args[0], 
                        module->constantDoubleZero, "tmp1"));
      return;
    } else if (meth->name == N3::testInfinity) {
      BasicBlock* endBlock = createBasicBlock("end test infinity");
      BasicBlock* minusInfinity = createBasicBlock("- infinity");
      BasicBlock* noInfinity = createBasicBlock("no infinity");
      PHINode* node = PHINode::Create(Type::getInt32Ty(getGlobalContext()), "", endBlock);
      node->addIncoming(module->constantOne, currentBlock);
      node->addIncoming(module->constantMinusOne, minusInfinity);
      node->addIncoming(module->constantZero, noInfinity);
      Value* val1 = new FCmpInst(*currentBlock, FCmpInst::FCMP_OEQ, Args[0],
                                 module->constantDoubleInfinity, "tmp1"); 
      BranchInst::Create(endBlock, minusInfinity, val1, currentBlock);
      Value* val2 = new FCmpInst(*minusInfinity, FCmpInst::FCMP_OEQ, Args[0],
                                 module->constantDoubleMinusInfinity, "tmp1");
      BranchInst::Create(endBlock, noInfinity, val2, minusInfinity);
      BranchInst::Create(endBlock, noInfinity);
      currentBlock = endBlock; 
      push(node);
      return;
    }
  } else if (meth->classDef->nameSpace == N3::system && 
             meth->classDef->name == N3::floatName) {
    if (meth->name == N3::isNan) {
      push(new FCmpInst(*currentBlock, FCmpInst::FCMP_UNO, Args[0], 
                        module->constantFloatZero, "tmp1"));
      return;
    } else if (meth->name == N3::testInfinity) {
      BasicBlock* endBlock = createBasicBlock("end test infinity");
      BasicBlock* minusInfinity = createBasicBlock("- infinity");
      BasicBlock* noInfinity = createBasicBlock("no infinity");
      PHINode* node = PHINode::Create(Type::getInt32Ty(getGlobalContext()), "", endBlock);
      node->addIncoming(module->constantOne, currentBlock);
      node->addIncoming(module->constantMinusOne, minusInfinity);
      node->addIncoming(module->constantZero, noInfinity);
      Value* val1 = new FCmpInst(*currentBlock, FCmpInst::FCMP_OEQ, Args[0],
                                 module->constantFloatInfinity, "tmp1");
      BranchInst::Create(endBlock, minusInfinity, val1, currentBlock);
      Value* val2 = new FCmpInst(*minusInfinity, FCmpInst::FCMP_OEQ, Args[0],
                                 module->constantFloatMinusInfinity, "tmp1");
      BranchInst::Create(endBlock, noInfinity, val2, minusInfinity);
      BranchInst::Create(endBlock, noInfinity);
      currentBlock = endBlock; 
      push(node);
      return;
    }
  }
  
  Value* res = 0;
  if (meth && meth->canBeInlined && meth != compilingMethod && 
      inlineMethods[meth] == 0) {
    res = invokeInline(meth, Args, genClass, genMethod);
  } else {
    Function* func = meth->compiledPtr(genMethod);
    
    res = invoke(func, Args, "", currentBlock, meth->structReturn);
  }
  if (meth->parameters[0] != MSCorlib::pVoid) {
    push(res);
  }
}

void CLIJit::invokeNew(uint32 value, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  Assembly* ass = compilingClass->assembly;
  VMMethod* meth = ass->getMethodFromToken(value, genClass, genMethod);
  VMClass* type = meth->classDef;
  const FunctionType* funcType = meth->getSignature(genMethod);
    
  Value* obj = 0;
  if (type->isPointer) {
    VMThread::get()->getVM()->error("implement me %s", mvm::PrintBuffer(type).cString());
  } else if (type->isArray) {
    VMClassArray* arrayType = (VMClassArray*)type;
    Value* valCl = new LoadInst(arrayType->llvmVar(), "", currentBlock);
    Value** args = (Value**)alloca(sizeof(Value*) * (arrayType->dims + 1));
    args[0] = valCl;

    for (int cur = arrayType->dims; cur > 0; --cur)
      args[cur] = pop();
     
    std::vector<Value*> Args;
    for (uint32 v = 0; v < arrayType->dims + 1; ++v) {
      Args.push_back(args[v]);
    }
    push(invoke(arrayMultiConsLLVM, Args, "", currentBlock, false));
    return;

  } else if (type->super == MSCorlib::pValue || type->super == MSCorlib::pEnum) {
    obj = new AllocaInst(type->naturalType, "", currentBlock);
    uint64 size = mvm::MvmModule::getTypeSize(type->naturalType);
        
    std::vector<Value*> params;
    params.push_back(new BitCastInst(obj, module->ptrType, "", currentBlock));
    params.push_back(module->constantInt8Zero);
    params.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), size));
    params.push_back(module->constantZero);
    CallInst::Create(module->llvm_memset_i32, params.begin(), params.end(),
                     "", currentBlock);
  } else {
    Value* var = new LoadInst(type->llvmVar(), "", currentBlock);
    Value* val = CallInst::Create(objConsLLVM, var, "", currentBlock);
    obj = new BitCastInst(val, type->naturalType, "", currentBlock);
  }
  
  std::vector<Value*>::iterator i = stack.end();
  uint32 nbParams = funcType->getNumParams();
  while (--nbParams) --i;
  stack.insert(i, obj);

  std::vector<Value*> Args;
  makeArgs(funcType, Args, meth->structReturn);
  if (meth && meth->canBeInlined && meth != compilingMethod && 
      inlineMethods[meth] == 0) {
    invokeInline(meth, Args, genClass, genMethod);
  } else {
    Function* func = meth->compiledPtr(genMethod);
    
    invoke(func, Args, "", currentBlock, meth->structReturn);
  }
    
  if ((type->super == MSCorlib::pValue || type->super == MSCorlib::pEnum) &&
      type->virtualFields.size() == 1) {
    push(new LoadInst(obj, "", currentBlock));
  } else {
    push(obj);
  }
}
  
llvm::Value* CLIJit::getVirtualField(uint32 value, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  VMField* field = compilingClass->assembly->getFieldFromToken(value, false, genClass, genMethod);
  Value* obj = pop();
  if ((field->classDef->super == MSCorlib::pValue ||
      field->classDef->super == MSCorlib::pEnum) &&
      field->classDef->virtualFields.size() == 1){
    // struct!
    return obj;
  } else {
    if (field->classDef->super != MSCorlib::pValue && 
        field->classDef->super != MSCorlib::pEnum) {
      obj = new BitCastInst(obj, field->classDef->naturalType, "",
                            currentBlock);
    }
    std::vector<Value*> args;
    args.push_back(module->constantZero);
    args.push_back(field->offset);
    Value* ptr = GetElementPtrInst::Create(obj, args.begin(), args.end(), "", 
                                           currentBlock);
    return ptr;
  }
}

llvm::Value* CLIJit::getStaticField(uint32 value, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  VMField* field = compilingClass->assembly->getFieldFromToken(value, true, genClass, genMethod);
  VMCommonClass* cl = field->classDef;
  cl->resolveType(true, false, genMethod);
  Value* arg = new LoadInst(cl->llvmVar(), "", currentBlock);
  Value* call = invoke(initialiseClassLLVM, arg, "", currentBlock, false);
  Value* staticCl = new BitCastInst(call, cl->staticType, "", currentBlock);
  
  std::vector<Value*> args;
  args.push_back(module->constantZero);
  args.push_back(field->offset);
  Value* ptr = GetElementPtrInst::Create(staticCl, args.begin(), args.end(), "",
                                         currentBlock);
  
  return ptr;

}
  
void CLIJit::setVirtualField(uint32 value, bool isVolatile, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  VMField* field = compilingClass->assembly->getFieldFromToken(value, false, genClass, genMethod);
  Value* val = pop();
  Value* obj = pop();
  const Type* valType = val->getType();

  Value* ptr = 0;
  const Type* type = obj->getType();
  if ((field->classDef->super == MSCorlib::pValue ||
      field->classDef->super == MSCorlib::pEnum) &&
      field->classDef->virtualFields.size() == 1){
    // struct!
    ptr = obj;
  } else {
    if (field->classDef->super != MSCorlib::pValue &&
        field->classDef->super != MSCorlib::pEnum) {
      obj = new BitCastInst(obj, field->classDef->naturalType, "", currentBlock);
    }
    std::vector<Value*> args;
    args.push_back(module->constantZero);
    args.push_back(field->offset);
    ptr = GetElementPtrInst::Create(obj, args.begin(), args.end(), "",
                                currentBlock);
  }
  
  if (field->signature->super == MSCorlib::pValue &&
      field->signature->virtualFields.size() > 1) {
    uint64 size = mvm::MvmModule::getTypeSize(field->signature->naturalType);
        
    std::vector<Value*> params;
    params.push_back(new BitCastInst(ptr, PointerType::getUnqual(Type::getInt8Ty(getGlobalContext())), "", currentBlock));
    params.push_back(new BitCastInst(val, PointerType::getUnqual(Type::getInt8Ty(getGlobalContext())), "", currentBlock));
    params.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), size));
    params.push_back(module->constantZero);
    CallInst::Create(module->llvm_memcpy_i32, params.begin(), params.end(), "", currentBlock);

  } else {
    type = field->signature->naturalType;
    if (val == constantVMObjectNull) {
      val = Constant::getNullValue(type);
    } else if (type != valType) {
      val = changeType(val, type);
    }
  
    new StoreInst(val, ptr, isVolatile, currentBlock);
  }
}

void CLIJit::setStaticField(uint32 value, bool isVolatile, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  VMField* field = compilingClass->assembly->getFieldFromToken(value, true, genClass, genMethod);
  
  VMCommonClass* cl = field->classDef;
  Value* arg = new LoadInst(cl->llvmVar(), "", currentBlock);
  Value* call = invoke(initialiseClassLLVM, arg, "", currentBlock, false);
  Value* staticCl = new BitCastInst(call, cl->staticType, "", currentBlock);
  
  std::vector<Value*> args;
  args.push_back(module->constantZero);
  args.push_back(field->offset);
  Value* ptr = GetElementPtrInst::Create(staticCl, args.begin(), args.end(), "",
                                     currentBlock);
  Value* val = pop();
  const Type* type = field->signature->naturalType;
  const Type* valType = val->getType();
  if (val == constantVMObjectNull) {
    val = Constant::getNullValue(type);
  } else if (type != valType) {
    val = changeType(val, type);
  }
  new StoreInst(val, ptr, isVolatile, currentBlock);
}

void CLIJit::JITVerifyNull(Value* obj) {
  CLIJit* jit = this;
  Constant* zero = Constant::getNullValue(obj->getType());
  Value* test = new ICmpInst(*jit->currentBlock, ICmpInst::ICMP_EQ, obj,
                             zero, "");

  BasicBlock* exit = jit->createBasicBlock("verifyNullExit");
  BasicBlock* cont = jit->createBasicBlock("verifyNullCont");

  BranchInst::Create(exit, cont, test, jit->currentBlock);
  
  std::vector<Value*> args;
  if (currentExceptionBlock != endExceptionBlock) {
    InvokeInst::Create(CLIJit::nullPointerExceptionLLVM, unifiedUnreachable,
                   currentExceptionBlock, args.begin(),
                   args.end(), "", exit);
  } else {
    CallInst::Create(CLIJit::nullPointerExceptionLLVM, args.begin(),
                 args.end(), "", exit);
    new UnreachableInst(getGlobalContext(), exit);
  }


  jit->currentBlock = cont;
}

llvm::Value* CLIJit::verifyAndComputePtr(llvm::Value* obj, llvm::Value* index,
                                         const llvm::Type* arrayType,
                                         bool verif) {
  JITVerifyNull(obj);
  
  if (index->getType() != Type::getInt32Ty(getGlobalContext())) {
    index = changeType(index, Type::getInt32Ty(getGlobalContext()));
  }
  
  if (true) {
    Value* size = arraySize(obj);
    
    Value* cmp = new ICmpInst(*currentBlock, ICmpInst::ICMP_SLT, index,
                              size, "");

    BasicBlock* ifTrue =  createBasicBlock("true verifyAndComputePtr");
    BasicBlock* ifFalse = createBasicBlock("false verifyAndComputePtr");

    branch(cmp, ifTrue, ifFalse, currentBlock);
    
    std::vector<Value*>args;
    args.push_back(new BitCastInst(obj, VMObject::llvmType, "", ifFalse));
    args.push_back(index);
    if (currentExceptionBlock != endExceptionBlock) {
      InvokeInst::Create(CLIJit::indexOutOfBoundsExceptionLLVM,
                         unifiedUnreachable,
                         currentExceptionBlock, args.begin(),
                         args.end(), "", ifFalse);
    } else {
      CallInst::Create(CLIJit::indexOutOfBoundsExceptionLLVM, args.begin(),
                   args.end(), "", ifFalse);
      new UnreachableInst(getGlobalContext(), ifFalse);
    }
  
    currentBlock = ifTrue;
  }
  
  Constant* zero = module->constantZero;
  Value* val = new BitCastInst(obj, arrayType, "", currentBlock);
  
  std::vector<Value*> indexes; //[3];
  indexes.push_back(zero);
  indexes.push_back(VMArray::elementsOffset());
  indexes.push_back(index);
  Value* ptr = GetElementPtrInst::Create(val, indexes.begin(), indexes.end(), 
                                         "", currentBlock);

  return ptr;

}

Constant* VMArray::sizeOffset() {
  return VMThread::get()->getVM()->module->constantOne;
}

Constant* VMArray::elementsOffset() {
  return VMThread::get()->getVM()->module->constantTwo;
}

Value* CLIJit::arraySize(Value* array) {
  if (array->getType() != VMArray::llvmType) {
    array = new BitCastInst(array, VMArray::llvmType, "", currentBlock);
  }
  return CallInst::Create(arrayLengthLLVM, array, "", currentBlock);
  /*
  std::vector<Value*> args; //size=  2
  args.push_back(module->constantZero);
  args.push_back(VMArray::sizeOffset());
  Value* ptr = GetElementPtrInst::Create(array, args.begin(), args.end(),
                                     "", currentBlock);
  return new LoadInst(ptr, "", currentBlock);*/
}

Function* CLIJit::createDelegate() {
  Function* func = llvmFunction = compilingMethod->methPtr;
  Function::arg_iterator i = func->arg_begin()++, e = func->arg_end();
  Value* obj = i++;
  Value* target = i++;
  Value* handle = i++;
  assert(i == e);
  
  BasicBlock* entry = createBasicBlock("entry");
  obj = new BitCastInst(obj, MSCorlib::pDelegate->virtualType, "", entry);
  std::vector<Value*> elts;
  elts.push_back(module->constantZero);
  elts.push_back(module->constantOne);
  Value* targetPtr = GetElementPtrInst::Create(obj, elts.begin(), elts.end(),
                                               "", entry);
  
  elts.pop_back();
  elts.push_back(module->constantTwo);
  Value* handlePtr = GetElementPtrInst::Create(obj, elts.begin(), elts.end(),
                                               "", entry);

  new StoreInst(target, targetPtr, false, entry); 
  new StoreInst(handle, handlePtr, false, entry);
  ReturnInst::Create(getGlobalContext(), entry);
  
  return func;
}
Function* CLIJit::invokeDelegate() {
  VMThread::get()->getVM()->error("implement me");
  return 0;
}

Function* CLIJit::compileIntern() {
  PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL, "intern compile %s\n",
              mvm::PrintBuffer(compilingMethod).cString());

  if (compilingClass->subclassOf(MSCorlib::pDelegate)) {
    const UTF8* name = compilingMethod->name;
    if (name == N3::ctorName) return createDelegate();
    else if (name == N3::invokeName) return invokeDelegate();
    else VMThread::get()->getVM()->error("implement me");
  } else {
    VMThread::get()->getVM()->error("implement me %s", mvm::PrintBuffer(compilingClass).cString());
  }
  return 0;
}

Function* CLIJit::compileNative(VMGenericMethod* genMethod) {
  PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL, "native compile %s\n",
              mvm::PrintBuffer(compilingMethod).cString());
    
  const FunctionType *funcType = compilingMethod->getSignature(genMethod);
  
  Function* func = llvmFunction = compilingMethod->methPtr;
  currentBlock = createBasicBlock("start");
  // TODO
  /*endExceptionBlock = createBasicBlock("exceptionBlock");
  endBlock = createBasicBlock("end");
  new UnwindInst(endExceptionBlock);*/
  
  uint32 nargs = func->arg_size();
  std::vector<Value*> nativeArgs;
  
  uint32 index = 0;
  for (Function::arg_iterator i = func->arg_begin(); 
       index < nargs; ++i, ++index) {   
    nativeArgs.push_back(i);
  }

  void* natPtr = NativeUtil::nativeLookup(compilingClass, compilingMethod);
  
  Value* valPtr = 
    ConstantExpr::getIntToPtr(ConstantInt::get(Type::getInt64Ty(getGlobalContext()), (uint64)natPtr),
                              PointerType::getUnqual(funcType));
  
  Value* result = CallInst::Create(valPtr, nativeArgs.begin(),
                                   nativeArgs.end(), "", currentBlock);
  
  
  if (result->getType() != Type::getVoidTy(getGlobalContext()))
    ReturnInst::Create(getGlobalContext(), result, currentBlock);
  else
    ReturnInst::Create(getGlobalContext(), currentBlock);
  
  
  return llvmFunction;
}

uint32 CLIJit::readExceptionTable(uint32 offset, bool fat, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  Assembly* ass = compilingClass->assembly;
  ByteCode* bytes = ass->bytes;
  uint32 nbe = 0;
  if (fat) {
    nbe = (READ_U3(bytes, offset) - 4) / 24;
  } else {
    nbe = (READ_U1(bytes, offset) - 4) / 12;
    READ_U2(bytes, offset);
  }
  
   if (nbe) {
    supplLocal = new AllocaInst(VMObject::llvmType, "exceptionVar",
                                currentBlock);
  }
  
  BasicBlock* realEndExceptionBlock = endExceptionBlock;
  // TODO synchronized

  for (uint32 i = 0; i < nbe; ++i) {
    ExceptionBlockDesc* ex = new(allocator, "ExceptionBlockDesc") ExceptionBlockDesc();
    uint32 flags = 0;
    uint32 classToken = 0;
    if (fat) {
      flags = READ_U4(bytes, offset);
      ex->tryOffset = READ_U4(bytes, offset);
      ex->tryLength = READ_U4(bytes, offset);
      ex->handlerOffset = READ_U4(bytes, offset);
      ex->handlerLength = READ_U4(bytes, offset);
      classToken = READ_U4(bytes, offset);
    } else {
      flags = READ_U2(bytes, offset);
      ex->tryOffset = READ_U2(bytes, offset);
      ex->tryLength = READ_U1(bytes, offset);
      ex->handlerOffset = READ_U2(bytes, offset);
      ex->handlerLength = READ_U1(bytes, offset);
      classToken = READ_U4(bytes, offset);
    }
    
    if (!(opcodeInfos[ex->handlerOffset].newBlock)) {
      opcodeInfos[ex->handlerOffset].newBlock = 
                              createBasicBlock("handlerException");
    }
    
    ex->handler = opcodeInfos[ex->handlerOffset].newBlock;
    
    if (flags == CONSTANT_COR_ILEXCEPTION_CLAUSE_EXCEPTION) {
      ex->test = createBasicBlock("testException");
      if (classToken) {
        ex->catchClass = ass->loadType((N3*)VMThread::get()->getVM(), classToken,
                                       true, false, false, true, genClass, genMethod);
      } else {
        ex->catchClass = MSCorlib::pException;
      }
      opcodeInfos[ex->handlerOffset].reqSuppl = true;
      exceptions.push_back(ex);

      for (uint16 i = ex->tryOffset; i < ex->tryOffset + ex->tryLength; ++i) {
        if (opcodeInfos[i].exceptionBlock == endExceptionBlock) {
          opcodeInfos[i].exceptionBlock = ex->test;
        }    
      }  

    } else if (flags == CONSTANT_COR_ILEXCEPTION_CLAUSE_FINALLY) {
      ex->catchClass = 0;
      finallyHandlers.push_back(ex);
    } else {
      VMThread::get()->getVM()->error("implement me");
    }
  }
  
  bool first = true;
  for (std::vector<ExceptionBlockDesc*>::iterator i = exceptions.begin(),
    e = exceptions.end(); i!= e; ++i) {

    ExceptionBlockDesc* cur = *i;
    ExceptionBlockDesc* next = 0;
    if (i + 1 != e) {
      next = *(i + 1);
    }

    if (first) {
      cur->realTest = createBasicBlock("realTestException");
    } else {
      cur->realTest = cur->test;
    }
    
    if (next && cur->tryOffset == next->tryOffset && 
        cur->tryOffset + cur->tryLength == next->tryOffset + next->tryLength)
      first = false;
    else
      first = true;
      
  }

  for (std::vector<ExceptionBlockDesc*>::iterator i = exceptions.begin(),
    e = exceptions.end(); i!= e; ++i) {

    ExceptionBlockDesc* cur = *i;
    ExceptionBlockDesc* next = 0;
    BasicBlock* bbNext = 0;
    if (i + 1 != e) {
      next = *(i + 1);
      if (cur->tryOffset >= next->tryOffset && 
          cur->tryOffset + cur->tryLength <=
            next->tryOffset + next->tryLength) {
        bbNext = realEndExceptionBlock;
      } else {
        bbNext = next->realTest;
      }
    } else {
      bbNext = realEndExceptionBlock;
    }

    if (cur->realTest != cur->test) {
      const PointerType* PointerTy_0 = module->ptrType;
      std::vector<Value*> int32_eh_select_params;
      Instruction* ptr_eh_ptr = CallInst::Create(module->llvmGetException,
                                                 "eh_ptr", cur->test);
      int32_eh_select_params.push_back(ptr_eh_ptr);
      Constant* C = ConstantExpr::getCast(Instruction::BitCast,
                                          module->personality, PointerTy_0);
      int32_eh_select_params.push_back(C);
      int32_eh_select_params.push_back(module->constantPtrNull);
      CallInst::Create(module->exceptionSelector,
                       int32_eh_select_params.begin(),
                       int32_eh_select_params.end(), "eh_select", cur->test);
      BranchInst::Create(cur->realTest, cur->test);
    } 

    Value* cl = new LoadInst(cur->catchClass->llvmVar(), "", cur->realTest);
    Value* cmp = CallInst::Create(compareExceptionLLVM, cl, "", cur->realTest);
    BranchInst::Create(cur->handler, bbNext, cmp, cur->realTest);
    
    if (cur->handler->empty()) {
      Value* cpp = CallInst::Create(getCppExceptionLLVM, "", cur->handler);
      Value* exc = CallInst::Create(getCLIExceptionLLVM, "", cur->handler);
      CallInst::Create(clearExceptionLLVM, "", cur->handler);
      CallInst::Create(module->exceptionBeginCatch, cpp, "tmp8",
                       cur->handler);
      std::vector<Value*> void_28_params;
      CallInst::Create(module->exceptionEndCatch, void_28_params.begin(),
                       void_28_params.end(), "", cur->handler);
      new StoreInst(exc, supplLocal, false, cur->handler);
      
      for (uint16 i = cur->tryOffset; i < cur->tryOffset + cur->tryLength;
           ++i) {
        opcodeInfos[i].exception = exc;
      }  
    } 
     
  }

  return nbe;

}

#if N3_EXECUTE > 1
static void printArgs(std::vector<llvm::Value*> args, BasicBlock* insertAt) {
	N3 *vm = VMThread::get()->getVM();

  for (std::vector<llvm::Value*>::iterator i = args.begin(),
       e = args.end(); i!= e; ++i) {
    llvm::Value* arg = *i;
    const llvm::Type* type = arg->getType();
    if (type == Type::getInt8Ty(getGlobalContext()) || type == Type::getInt16Ty(getGlobalContext()) || type == Type::getInt1Ty(getGlobalContext())) {
      CallInst::Create(vm->module->printIntLLVM, new ZExtInst(arg, Type::getInt32Ty(getGlobalContext()), "", insertAt), "", insertAt);
    } else if (type == Type::getInt32Ty(getGlobalContext())) {
      CallInst::Create(vm->module->printIntLLVM, arg, "", insertAt);
    } else if (type == Type::getInt64Ty(getGlobalContext())) {
      CallInst::Create(vm->module->printLongLLVM, arg, "", insertAt);
    } else if (type == Type::getFloatTy(getGlobalContext())) {
      CallInst::Create(vm->module->printFloatLLVM, arg, "", insertAt);
    } else if (type == Type::getDoubleTy(getGlobalContext())) {
      CallInst::Create(vm->module->printDoubleLLVM, arg, "", insertAt);
    } else {
      CallInst::Create(vm->module->printIntLLVM, new PtrToIntInst(arg, Type::getInt32Ty(getGlobalContext()), "", insertAt), "", insertAt);
    }
  }

}
#endif

Function* CLIJit::compileFatOrTiny(VMGenericClass* genClass, VMGenericMethod* genMethod) {
  PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL, "tiny or fat compile %s\n",
              mvm::PrintBuffer(compilingMethod).cString());
  uint32 offset = compilingMethod->offsetInTextSection;
	ByteCode* bytes = compilingClass->assembly->bytes;
  uint8 header = READ_U1(bytes, offset);
  bool tiny = false;
  uint32 localVarSig = 0;
  uint32 maxStack = 0;
  uint32 codeLen = 0;
  uint32 nbe = 0;

  if ((header & 3) == CONSTANT_CorILMethod_TinyFormat) {
    tiny = true;
    codeLen = (header & 0xfffc) >> 2;
  } else if ((header & 3) != CONSTANT_CorILMethod_FatFormat) {
    VMThread::get()->getVM()->error("unknown Method Format");
  } else {
    header += (READ_U1(bytes, offset) << 8); //header
    maxStack = READ_U2(bytes, offset);
    codeLen = READ_U4(bytes, offset);
    localVarSig = READ_U4(bytes, offset);
  }
    

  /* TODO Synchronize
  bool synchro = isSynchro(compilingMethod->flags);
  */

  const FunctionType *funcType = compilingMethod->getSignature(genMethod);
  
  Function* func = llvmFunction = compilingMethod->methPtr;
  currentBlock = createBasicBlock("start");
  endExceptionBlock = createBasicBlock("exceptionBlock");
  unifiedUnreachable = createBasicBlock("unifiedUnreachable"); 
  

  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExceptionBlock;
  }
  
  if (!tiny) {
    if (header & CONSTANT_CorILMethod_MoreSects) {
      uint32 excpOffset = 0;
      if ((codeLen % 4) == 0) {
        excpOffset = offset + codeLen;
      } else {
        excpOffset = offset + codeLen + (4 - (codeLen % 4));
      }

      uint8 flags = READ_U1(bytes, excpOffset);
      nbe = readExceptionTable(excpOffset, 
                         flags & CONSTANT_CorILMethod_Sect_FatFormat, genClass, genMethod);
    }
  }

  for (Function::arg_iterator i = func->arg_begin(), e = func->arg_end(); 
       i != e; ++i) {
    
    const Type* cur = i->getType();

    AllocaInst* alloc = new AllocaInst(cur, "", currentBlock);
    new StoreInst(i, alloc, false, currentBlock);
    arguments.push_back(alloc);
  } 
  
  if (localVarSig) {
    std::vector<VMCommonClass*> temp;
    compilingClass->assembly->readSignature(localVarSig, temp, genClass, genMethod);

    for (std::vector<VMCommonClass*>::iterator i = temp.begin(), 
            e = temp.end(); i!= e; ++i) {
      VMCommonClass* cl = *i;
      cl->resolveType(false, false, genMethod);
      AllocaInst* alloc = new AllocaInst(cl->naturalType, "", currentBlock);
      if (cl->naturalType->isSingleValueType()) {
        new StoreInst(Constant::getNullValue(cl->naturalType), alloc, false,
                      currentBlock);
      } else {
        uint64 size = mvm::MvmModule::getTypeSize(cl->naturalType);
        
        std::vector<Value*> params;
        params.push_back(new BitCastInst(alloc, module->ptrType, "",
                                         currentBlock));
        params.push_back(module->constantInt8Zero);
        params.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), size));
        params.push_back(module->constantZero);
        CallInst::Create(module->llvm_memset_i32, params.begin(),
                         params.end(), "", currentBlock);

      }
      locals.push_back(alloc);
    }
  }

  exploreOpcodes(&compilingClass->assembly->bytes->elements[offset], codeLen); 
 
  endBlock = createBasicBlock("end");

  const Type* endType = funcType->getReturnType(); 
  if (endType != Type::getVoidTy(getGlobalContext())) {
    endNode = PHINode::Create(endType, "", endBlock);
  } else if (compilingMethod->structReturn) {
    const Type* lastType = 
      funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    endNode = PHINode::Create(lastType, "", endBlock);
  }

  compileOpcodes(&compilingClass->assembly->bytes->elements[offset], codeLen, genClass, genMethod);
  
  currentBlock = endBlock;
  pred_iterator PI = pred_begin(endBlock);
  pred_iterator PE = pred_end(endBlock);
  if (PI == PE) {
    endBlock->eraseFromParent();
  } else {
    if (endType != Type::getVoidTy(getGlobalContext())) {
#if N3_EXECUTE > 1
      std::vector<Value*> args;
      args.push_back(endNode);
      printArgs(args, endBlock);
#endif
      ReturnInst::Create(getGlobalContext(), endNode, endBlock);
    } else if (compilingMethod->structReturn) {
      const Type* lastType = 
        funcType->getContainedType(funcType->getNumContainedTypes() - 1);
      uint64 size = mvm::MvmModule::getTypeSize(lastType->getContainedType(0));
      Value* obj = --llvmFunction->arg_end();
      std::vector<Value*> params;
      params.push_back(new BitCastInst(obj, module->ptrType, "",
                                       currentBlock));
      params.push_back(new BitCastInst(endNode, module->ptrType, "",
                                       currentBlock));
      params.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), size));
      params.push_back(module->constantFour);
      CallInst::Create(module->llvm_memcpy_i32, params.begin(), params.end(),
                       "", currentBlock);
      ReturnInst::Create(getGlobalContext(), currentBlock);
    } else {
      ReturnInst::Create(getGlobalContext(), endBlock);
    }
  }
  
  PI = pred_begin(endExceptionBlock);
  PE = pred_end(endExceptionBlock);
  if (PI == PE) {
    endExceptionBlock->eraseFromParent();
  } else {
    CallInst* ptr_eh_ptr = CallInst::Create(getCppExceptionLLVM, "eh_ptr", 
                                        endExceptionBlock);
    CallInst::Create(module->unwindResume, ptr_eh_ptr, "", endExceptionBlock);
    new UnreachableInst(getGlobalContext(), endExceptionBlock);
  }
  
  PI = pred_begin(unifiedUnreachable);
  PE = pred_end(unifiedUnreachable);
  if (PI == PE) {
    unifiedUnreachable->eraseFromParent();
  } else {
    new UnreachableInst(getGlobalContext(), unifiedUnreachable);
  }
  
  mvm::MvmModule::runPasses(llvmFunction, VMThread::get()->perFunctionPasses);
  
  if (nbe == 0 && codeLen < 50) {
    PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL, "%s can be inlined\n",
                mvm::PrintBuffer(compilingMethod).cString());
    compilingMethod->canBeInlined = true;
  }
  
  return llvmFunction;
}

Instruction* CLIJit::inlineCompile(Function* parentFunction, BasicBlock*& curBB,
                                   BasicBlock* endExBlock,
                                   std::vector<Value*>& args, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  
  PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL, "tiny or fat inline compile %s\n",
              mvm::PrintBuffer(compilingMethod).cString());
  uint32 offset = compilingMethod->offsetInTextSection;
  ByteCode* bytes = compilingClass->assembly->bytes;
  uint8 header = READ_U1(bytes, offset);
  bool tiny = false;
  uint32 localVarSig = 0;
  uint32 maxStack = 0;
  uint32 codeLen = 0;
  
  if ((header & 3) == CONSTANT_CorILMethod_TinyFormat) {
    tiny = true;
    codeLen = (header & 0xfffc) >> 2;
  } else if ((header & 3) != CONSTANT_CorILMethod_FatFormat) {
    VMThread::get()->getVM()->error("unknown Method Format");
  } else {
    header += (READ_U1(bytes, offset) << 8); //header
    maxStack = READ_U2(bytes, offset);
    codeLen = READ_U4(bytes, offset);
    localVarSig = READ_U4(bytes, offset);
  }
    

  /* TODO Synchronize
  bool synchro = isSynchro(compilingMethod->flags);
  */

  const FunctionType *funcType = compilingMethod->getSignature(genMethod);
  
  llvmFunction = parentFunction;
  currentBlock = curBB;
  endExceptionBlock = 0; 
  

  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExBlock;
  }
  
  if (!tiny) {
    if (header & CONSTANT_CorILMethod_MoreSects) {
      assert(0 && "inlining a function with exceptions!");
      uint32 excpOffset = 0;
      if ((codeLen % 4) == 0) {
        excpOffset = offset + codeLen;
      } else {
        excpOffset = offset + codeLen + (4 - (codeLen % 4));
      }

      uint8 flags = READ_U1(bytes, excpOffset);
      readExceptionTable(excpOffset, 
                         flags & CONSTANT_CorILMethod_Sect_FatFormat, genClass, genMethod);
    }
  }

  for (std::vector<Value*>::iterator i = args.begin(), e = args.end(); 
       i != e; ++i) {
    
    const Type* cur = (*i)->getType();

    AllocaInst* alloc = new AllocaInst(cur, "", currentBlock);
    new StoreInst(*i, alloc, false, currentBlock);
    arguments.push_back(alloc);
  } 

  if (localVarSig) {
    std::vector<VMCommonClass*> temp;
    compilingClass->assembly->readSignature(localVarSig, temp, genClass, genMethod);

    for (std::vector<VMCommonClass*>::iterator i = temp.begin(), 
            e = temp.end(); i!= e; ++i) {
      VMCommonClass* cl = *i;
      cl->resolveType(false, false, genMethod);
      AllocaInst* alloc = new AllocaInst(cl->naturalType, "", currentBlock);
      if (cl->naturalType->isSingleValueType()) {
        new StoreInst(Constant::getNullValue(cl->naturalType), alloc, false,
                      currentBlock);
      } else {
        uint64 size = mvm::MvmModule::getTypeSize(cl->naturalType);
        
        std::vector<Value*> params;
        params.push_back(new BitCastInst(alloc, module->ptrType, "",
                                         currentBlock));
        params.push_back(module->constantInt8Zero);
        params.push_back(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), size));
        params.push_back(module->constantZero);
        CallInst::Create(module->llvm_memset_i32, params.begin(),
                         params.end(), "", currentBlock);

      }
      locals.push_back(alloc);
    }
  }

  exploreOpcodes(&compilingClass->assembly->bytes->elements[offset], codeLen); 
 
  endBlock = createBasicBlock("end");

  const Type* endType = funcType->getReturnType(); 
  if (endType != Type::getVoidTy(getGlobalContext())) {
    endNode = PHINode::Create(endType, "", endBlock);
  } else if (compilingMethod->structReturn) {
    const Type* lastType = 
      funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    endNode = PHINode::Create(lastType, "", endBlock);
  }

  compileOpcodes(&compilingClass->assembly->bytes->elements[offset], codeLen, genClass, genMethod);
  
  curBB = endBlock;
  
  PRINT_DEBUG(N3_COMPILE, 1, COLOR_NORMAL,
              "end tiny or fat inline compile %s\n",
              mvm::PrintBuffer(compilingMethod).cString());
  
  return endNode;
}


Function* CLIJit::compile(VMClass* cl, VMMethod* meth) {
	mvm::BumpPtrAllocator *a = new mvm::BumpPtrAllocator();
  CLIJit* jit = new(*a, "CLIJit") CLIJit(*a);
  jit->compilingClass = cl; 
  jit->compilingMethod = meth;
  jit->module = cl->vm->module;
  Function* func;
  meth->getSignature(static_cast<VMGenericMethod*>(meth));
  
  if (isInternal(meth->implFlags)) {
    func = jit->compileNative(static_cast<VMGenericMethod*>(meth));
  } else if (meth->offsetInTextSection == 0) {
    func = jit->compileIntern();
  } else {
    func = jit->compileFatOrTiny(static_cast<VMGenericClass*>(cl), static_cast<VMGenericMethod*>(meth));
  }

	delete a;
	//	printf("Compiling: %s\n", mvm::PrintBuffer(meth).cString());
  return func;
}


llvm::Function *VMMethod::compiledPtr(VMGenericMethod* genMethod) {
  if (methPtr != 0) return methPtr;
  else {
    classDef->aquire();
    if (methPtr == 0) {
      methPtr = Function::Create(getSignature(genMethod), GlobalValue::ExternalWeakLinkage,
                                 mvm::PrintBuffer(this).cString(), classDef->vm->getLLVMModule());
      classDef->vm->functions->hash(methPtr, this);
    }
    classDef->release();
    return methPtr;
  }
}

VMMethod* CLIJit::getMethod(llvm::Function* F) { 
  VMMethod* meth = VMThread::get()->getVM()->functions->lookup(F);
  return meth;
}

void VMField::initField(VMObject* obj) {
  VMField* field = this;
  Constant* offset = field->offset;
  const TargetData* targetData = mvm::MvmModule::executionEngine->getTargetData();
  bool stat = isStatic(field->flags);
  const Type* clType = stat ? field->classDef->staticType :
                              field->classDef->virtualType;
  
  const StructLayout* sl =
    targetData->getStructLayout((StructType*)(clType->getContainedType(0)));
  uint64 ptrOffset = sl->getElementOffset(dyn_cast<ConstantInt>(offset)->getZExtValue());
  
  field->ptrOffset = ptrOffset;

}

void CLIJit::initialise() {
}

void CLIJit::initialiseAppDomain(N3* vm) {
  mvm::MvmModule::protectEngine.lock();
  mvm::MvmModule::executionEngine->addModule(vm->getLLVMModule());
  mvm::MvmModule::protectEngine.unlock();
}

namespace n3 { 
  namespace llvm_runtime { 
    #include "LLVMRuntime.inc"
  }
}


void CLIJit::initialiseBootstrapVM(N3* vm) {
  Module* module = vm->getLLVMModule();
  mvm::MvmModule::protectEngine.lock();
  mvm::MvmModule::executionEngine->addModule(module);
  mvm::MvmModule::protectEngine.unlock();
    
  n3::llvm_runtime::makeLLVMModuleContents(module);

  VMObject::llvmType = 
    PointerType::getUnqual(module->getTypeByName("CLIObject"));
  VMArray::llvmType = 
    PointerType::getUnqual(module->getTypeByName("CLIArray"));
  CacheNode::llvmType = 
    PointerType::getUnqual(module->getTypeByName("CacheNode"));
  Enveloppe::llvmType = 
    PointerType::getUnqual(module->getTypeByName("Enveloppe"));

#define GET_LLVM_ARRAY_TYPE(name, type)																\
	Array##name::llvmType =																							\
    PointerType::getUnqual(module->getTypeByName("Array"#name));			\

	ON_TYPES(GET_LLVM_ARRAY_TYPE, _F_NT)

#undef GET_LLVM_ARRAY_TYPE

#ifdef WITH_TRACER
  markAndTraceLLVM = module->getFunction("MarkAndTrace");
  markAndTraceLLVMType = markAndTraceLLVM->getFunctionType();
  vmObjectTracerLLVM = module->getFunction("CLIObjectTracer");
#endif


  initialiseClassLLVM = module->getFunction("initialiseClass");
  virtualLookupLLVM = module->getFunction("n3VirtualLookup");

  arrayConsLLVM = module->getFunction("newArray");
  objConsLLVM = module->getFunction("newObject");
  newStringLLVM = module->getFunction("newString");
  objInitLLVM = module->getFunction("initialiseObject");
  arrayMultiConsLLVM = module->getFunction("newMultiArray");
  arrayLengthLLVM = module->getFunction("arrayLength");
  instanceOfLLVM = module->getFunction("n3InstanceOf");

  nullPointerExceptionLLVM = module->getFunction("n3NullPointerException");
  classCastExceptionLLVM = module->getFunction("n3ClassCastException");
  indexOutOfBoundsExceptionLLVM = module->getFunction("indexOutOfBounds");

  
  throwExceptionLLVM = module->getFunction("ThrowException");
  clearExceptionLLVM = module->getFunction("ClearException");
  compareExceptionLLVM = module->getFunction("CompareException");
  getCppExceptionLLVM = module->getFunction("GetCppException");
  getCLIExceptionLLVM = module->getFunction("GetCLIException");


  printExecutionLLVM = module->getFunction("n3PrintExecution");


  
  constantVMObjectNull = Constant::getNullValue(VMObject::llvmType);
}

Constant* CLIJit::constantVMObjectNull;


Value* CLIJit::invoke(Value *F, std::vector<llvm::Value*> args,
                       const char* Name,
                       BasicBlock *InsertAtEnd, bool structReturn) {
#if N3_EXECUTE > 1
  printArgs(args, InsertAtEnd);
#endif

  Value* ret = 0;
  if (structReturn) {
    const Type* funcType = F->getType();
    if (isa<PointerType>(funcType)) {
      funcType = funcType->getContainedType(0);
    }
    const Type* lastType = funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    ret = new AllocaInst(lastType->getContainedType(0), "", InsertAtEnd);
    args.push_back(ret);
  }
  Value* val = 0;
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    val = InvokeInst::Create(F, ifNormal, currentExceptionBlock, args.begin(), 
                         args.end(), Name, InsertAtEnd);
  } else {
    val = CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
  if (ret) return ret;
  else return val;
}

Value* CLIJit::invoke(Value *F, Value* arg1, const char* Name,
                       BasicBlock *InsertAtEnd, bool structReturn) {
  
  std::vector<Value*> args;
  args.push_back(arg1);
#if N3_EXECUTE > 1
  printArgs(args, InsertAtEnd);
#endif
  Value* ret = 0;
  if (structReturn) {
    const Type* funcType = F->getType();
    if (isa<PointerType>(funcType)) {
      funcType = funcType->getContainedType(0);
    }
    const Type* lastType = funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    ret = new AllocaInst(lastType->getContainedType(0), "", InsertAtEnd);
    args.push_back(ret);
  }
  
  Value* val = 0;
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    val = InvokeInst::Create(F, ifNormal, currentExceptionBlock, args.begin(),
                                args.end(), Name, InsertAtEnd);
  } else {
    val = CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
  
  if (ret) return ret;
  else return val; 
}

Value* CLIJit::invoke(Value *F, Value* arg1, Value* arg2,
                       const char* Name, BasicBlock *InsertAtEnd, 
                       bool structReturn) {
  
  std::vector<Value*> args;
  args.push_back(arg1);
  args.push_back(arg2);
#if N3_EXECUTE > 1
  printArgs(args, InsertAtEnd);
#endif
  
  Value* ret = 0;
  if (structReturn) {
    const Type* funcType = F->getType();
    if (isa<PointerType>(funcType)) {
      funcType = funcType->getContainedType(0);
    }
    const Type* lastType = funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    ret = new AllocaInst(lastType->getContainedType(0), "", InsertAtEnd);
    args.push_back(ret);
  }

  Value* val = 0;
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    val = InvokeInst::Create(F, ifNormal, currentExceptionBlock, args.begin(),
                         args.end(), Name, InsertAtEnd);
  } else {
    val = CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
  if (ret) return ret;
  else return val; 
}

Value* CLIJit::invoke(Value *F, const char* Name,
                       BasicBlock *InsertAtEnd, bool structReturn) {
  
  std::vector<Value*> args;
  Value* ret = 0;
  if (structReturn) {
    const Type* funcType = F->getType();
    if (isa<PointerType>(funcType)) {
      funcType = funcType->getContainedType(0);
    }
    const Type* lastType = funcType->getContainedType(funcType->getNumContainedTypes() - 1);
    ret = new AllocaInst(lastType->getContainedType(0), "", InsertAtEnd);
    args.push_back(ret);
  }

  Value* val = 0;
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    val = InvokeInst::Create(F, ifNormal, currentExceptionBlock, args.begin(),
                         args.end(), Name, InsertAtEnd);
  } else {
    val = CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
  if (ret) return ret;
  else return val; 
}




namespace mvm {
llvm::FunctionPass* createEscapeAnalysisPass(llvm::Function*, llvm::Function*);
llvm::FunctionPass* createLowerArrayLengthPass();
}


static void addPass(FunctionPassManager *PM, Pass *P) {
  // Add the pass to the pass manager...
  PM->add(P);
}

void AddStandardCompilePasses(FunctionPassManager *PM) {
  llvm::MutexGuard locked(mvm::MvmModule::executionEngine->lock);
  // LLVM does not allow calling functions from other modules in verifier
  //PM->add(llvm::createVerifierPass());                  // Verify that input is correct
  
  addPass(PM, createCFGSimplificationPass()); // Clean up disgusting code
  addPass(PM, createPromoteMemoryToRegisterPass());// Kill useless allocas
  
  addPass(PM, createInstructionCombiningPass()); // Cleanup for scalarrepl.
  addPass(PM, createJumpThreadingPass());        // Thread jumps.
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createScalarReplAggregatesPass()); // Break up aggregate allocas
  addPass(PM, createInstructionCombiningPass()); // Combine silly seq's
  
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createReassociatePass());          // Reassociate expressions
  addPass(PM, createLICMPass());                 // Hoist loop invariants
  
  addPass(PM, createLoopUnswitchPass());         // Unswitch loops.
  addPass(PM, createIndVarSimplifyPass());       // Canonicalize indvars
  addPass(PM, createLoopDeletionPass());         // Delete dead loops
  addPass(PM, createLoopUnrollPass());           // Unroll small loops*/
  addPass(PM, createInstructionCombiningPass()); // Clean up after the unroller
  addPass(PM, createGVNPass());                  // Remove redundancies
  addPass(PM, createSCCPPass());                 // Constant prop with SCCP
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
 
  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  addPass(PM, createInstructionCombiningPass());

  addPass(PM, createDeadStoreEliminationPass()); // Delete dead stores
  addPass(PM, createAggressiveDCEPass());        // Delete dead instructions
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs

  addPass(PM, mvm::createLowerArrayLengthPass());
}
