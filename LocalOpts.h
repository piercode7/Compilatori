#ifndef LLVM_TRANSFORMS_UTILS_LOCALOPTS_H
#define LLVM_TRANSFORMS_UTILS_LOCALOPTS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"

namespace llvm {

class LocalOpts : public PassInfoMixin<LocalOpts> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOCALOPTS_H

