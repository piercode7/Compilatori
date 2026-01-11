#ifndef LOOPFUSION_OPT_H
#define LOOPFUSION_OPT_H

// Pass Management
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

// IR and Instructions
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h" // Se usi BasicBlock direttamente
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h" // Per DataLayout

// LLVM Containers and Utilities
#include "llvm/ADT/APInt.h" // Per APInt
#include "llvm/ADT/SmallVector.h" // Per SmallVector
#include "llvm/Support/raw_ostream.h"

// Core Analysis
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h" // Per SCEVAddRecExpr, SCEVConstant, SCEVCouldNotCompute
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

// Transformation Utilities
#include "llvm/Transforms/Utils/Local.h"


namespace llvm {

class LoopFusionOpt : public PassInfoMixin<LoopFusionOpt> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  bool runOnLoops(Function &F, FunctionAnalysisManager &FAM, const std::vector<Loop*> &Loops);
  bool isOptimizable(Function &F, FunctionAnalysisManager &FAM, Loop *prev, Loop *curr);
  Loop* fuseLoops(Function &F, FunctionAnalysisManager &FAM, Loop *prev, Loop *curr);

};
}

#endif