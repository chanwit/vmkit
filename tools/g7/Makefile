##===- tools/jnjvm/Makefile --------------------------------*- Makefile -*-===##
# 
#                     The GVM-vmkit project
#
# This file is distributed under Suranaree University of Technology Open Source
# License. See LICENSE.TXT for details.
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##
LEVEL = ../..

include $(LEVEL)/Makefile.config

TOOLNAME = g7

ifeq ($(WITH_LLVM_GCC), 1)

  MODULESNAME = g7
  USEDMODULES = G7.bc Classpath.bc J3Compiler.bc Allocator.bc CommonThread.bc \
		Mvm.bc MvmCompiler.bc

  ifeq ($(GC_MMTK), 1)
    USEDMODULES += FinalMMTk.bc
  else
    USEDMODULES += $(GCLIB).bc
  endif

  ifeq ($(ISOLATE_SHARING_BUILD), 1) 
    USEDMODULES += Isolate.bc
  endif

  BUILT_SOURCES = g7.s
  SOURCES = g7.s $(notdir $(wildcard $(PROJ_SRC_DIR)/*.cpp))

else

USEDLIBS = G7.a Classpath.a G7.a G7Compiler.a Allocator.a \
	   Mvm.a MvmCompiler.a $(GCLIB).a CommonThread.a

  ifeq ($(ISOLATE_SHARING_BUILD), 1) 
    USEDLIBS += Isolate
  endif

endif

LINK_COMPONENTS = jit nativecodegen scalaropts instrumentation ipa ipo bitreader asmparser linker


include $(LEVEL)/Makefile.common
