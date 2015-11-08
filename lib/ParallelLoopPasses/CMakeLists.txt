add_llvm_library(LLVMParallelLoopPasses
  IsParallelizableLoopPass.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ParallelLoopPasses
  )

add_dependencies(LLVMParallelLoopPasses intrinsics_gen)
