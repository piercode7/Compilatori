
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "LocalOpts.h"

using namespace llvm;

PreservedAnalyses LocalOpts::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;

  for (auto &F : M.functions()) {
    for (auto &BB : F) {
      for (auto I = BB.begin(), E = BB.end(); I != E;) {
        Instruction *Inst = &*(I++); // Pre-increment to avoid invalidated iterator
        
        // Verifica se l'istruzione è una addizione e ottimizzazione di identità con add
        if (auto *Add = dyn_cast<BinaryOperator>(Inst)) {
          if (Add->getOpcode() == Instruction::Add) {
            // Controlla gli operandi per l'identità algebrica
            for (unsigned int i = 0; i < 2; ++i) {
              if (isa<ConstantInt>(Add->getOperand(i)) &&
                  cast<ConstantInt>(Add->getOperand(i))->isZero()) {
                Value *OtherOperand = Add->getOperand(1-i);
                // Sostituisci l'uso di Add con OtherOperand
                Add->replaceAllUsesWith(OtherOperand);
                // Rimuovi l'istruzione in modo sicuro
                Add->eraseFromParent();
                Changed = true;
                break; // Uscita anticipata dal ciclo for degli operandi
              }
            }
          }
        }
        
        // Ottimizzazione d'identità con mul
        if (auto *Mul = dyn_cast<BinaryOperator>(Inst)) {
          if (Mul->getOpcode() == Instruction::Mul) {
            for (unsigned int i = 0; i < 2; ++i) {
              if (auto *CI = dyn_cast<ConstantInt>(Mul->getOperand(i))) {
                if (CI->isOne()) {
                  Value *OtherOperand = Mul->getOperand(1-i);
                  Mul->replaceAllUsesWith(OtherOperand);
                  Mul->eraseFromParent();
                  Changed = true;
                  break;
                }
              }
            }
          }
        }
        
        // Ottimizzazione con shift
        if (auto *Div = dyn_cast<BinaryOperator>(Inst)) {
          if (Div->getOpcode() == Instruction::SDiv || Div->getOpcode() == Instruction::UDiv) {
            if (auto *CI = dyn_cast<ConstantInt>(Div->getOperand(1))) {
              if (CI->getValue().isPowerOf2()) {
                unsigned ShiftAmount = CI->getValue().logBase2();
                Value *ShiftAmtVal = ConstantInt::get(Div->getType(), ShiftAmount);
                Instruction *ShiftRight = BinaryOperator::Create(Instruction::LShr, Div->getOperand(0), ShiftAmtVal, "", Div);
                Div->replaceAllUsesWith(ShiftRight);
                Div->eraseFromParent();
                Changed = true;
              }
            }
          }
        }


      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
