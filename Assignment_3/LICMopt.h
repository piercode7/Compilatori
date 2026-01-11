#ifndef LICM_OPTS_H
#define LICM_OPTS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Constants.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

namespace llvm {

class LICMopt : public PassInfoMixin<LICMopt> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  bool isSafeToMove(Instruction &I, Loop *L, DominatorTree &DT, SmallVector<BasicBlock*> ExitBlocks);
  bool runOnLoop(Loop *L, DominatorTree &DT);

};
}

#endif