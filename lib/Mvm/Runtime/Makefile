##===- lib/Mvm/Runtime/Makefile ----------------------------*- Makefile -*-===##
# 
#                     The vmkit project
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##
LEVEL = ../../..

include $(LEVEL)/Makefile.config

ifeq ($(WITH_LLVM_GCC), 1)

  MODULE_NAME = Mvm

else
  LIBRARYNAME = Mvm

  VMKIT_ASSEMBLY = $(PROJ_SRC_DIR)/LLVMAssembly.ll
  BUILT_SOURCES = LLVMAssembly.s

  ifeq ($(WITH_64), 1)
    VMKIT_ASSEMBLY += $(PROJ_SRC_DIR)/LLVMAssembly64.ll
  endif

  SOURCES = LLVMAssembly.s $(notdir $(wildcard $(PROJ_SRC_DIR)/*.cpp))

endif

include $(LEVEL)/Makefile.common
