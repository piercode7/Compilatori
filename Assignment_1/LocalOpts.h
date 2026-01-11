#ifndef LOCAL_OPTS_H
#define LOCAL_OPTS_H

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

namespace llvm {

class LocalOpts : public PassInfoMixin<LocalOpts> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool runOnBasicBlock(BasicBlock &B);
  static bool AlgebraicIdentityOpt2(Instruction &I);
  static bool AlgebraicIdentityOpt(Instruction &I);
  static bool StrengthReductionOpt(Instruction &I);
  static bool AdvancedMulSROpt(Instruction &I);
  static bool MultiInstructionOpt(Instruction &I);
  static bool SubMultiInstrOpt(Instruction &I);
};
}

#endif