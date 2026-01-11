// LocalOpts.cpp
#include "llvm/Transforms/Utils/LocalOpts.h"

using namespace llvm;


// Cicla su tutti i BB di F, per ogni BB chiama runOnBasicBlock.
// Accumula i cambiamenti e lo ritorna se c'è stato
llvm::PreservedAnalyses llvm::LocalOpts::run(Function &F,
                                             FunctionAnalysisManager &FAM) {
  llvm::errs() << "\nRunning on functio: " << F.getName() << "\n";
  bool functionChanged = false;

  for (auto &BB : F)
    functionChanged |= runOnBasicBlock(BB);

  return functionChanged ? llvm::PreservedAnalyses::none()
                         : llvm::PreservedAnalyses::all();
}



// Cuore operativo:
// scorre le istruzioni del BB
// prova a ottimizzarle chiamando le Utility
// raccoglie quelle modificate
// cancella le modificate in sicurezza
bool LocalOpts::runOnBasicBlock(llvm::BasicBlock &B) {
  bool blockChanged = false;
  std::set<Instruction *> toBeErased;

  for (auto &I : B) {

    bool instructionChanged =
        I.isBinaryOp() && (
          AlgebraicIdentityOpt2(I) ||
          StrengthReductionOpt(I)  ||
          MultiInstructionOpt(I)
        );


    if (instructionChanged)
      toBeErased.insert(&I);

    blockChanged |= instructionChanged;
  }
  // alla fine elimino le istruzioni che ho candidato
  for (auto *I : toBeErased)
    I->eraseFromParent();

  return blockChanged;
}

bool LocalOpts::AlgebraicIdentityOpt2(Instruction &I) {
  errs() << "\nRunning on AlgebricIdentityOpt:" << "\n";
  auto opCode = I.getOpcode();
  // cerco le tipologie di istruzione corrette
  auto Op1 = I.getOperand(0);
  auto Op2 = I.getOperand(1);
  switch (opCode) {
  case Instruction::Add: {
    // COMMUTATIVITY = true
    // NEUTRAL = 0
    // casi: (N+0), (0+N)
    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (C1 && C1->isZero()) {
      // l'istruzione è un'identita
      I.replaceAllUsesWith(Op2);
      errs() << "ADD opt! Op1=0\n";
      return true;
    }
    if (C2 && C2->isZero()) {
      I.replaceAllUsesWith(Op1);
      errs() << "ADD opt! Op2=0\n";
      return true;
    }
    errs() << "No ADD opt.\n";
    return false;
  }

  case Instruction::Sub: {
    // COMMUTATIVITY = false
    // NEUTRAL = 0
    // casi: (N-0), (0+N)
    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (C2 && C2->isZero()) {
      I.replaceAllUsesWith(Op1);
      errs() << "SUB opt! Op2=0\n";
      return true;
    }
    errs() << "No SUB opt.\n";
    return false;
  }

  case Instruction::Mul: {
    // COMMUTATY = true
    // MEUTRAL = 1
    // casi: (N*1), (1*N)
    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (C1 && C1->isOne()) {
      // l'istruzione è un'identita
      I.replaceAllUsesWith(Op2);
      errs() << "MUL opt! Op1=1\n";
      return true;
    }
    if (C2 && C2->isOne()) {
      // l'istruzione è un'identita
      I.replaceAllUsesWith(Op1);
      errs() << "MUL opt! Op2=1\n";
      return true;
    }
    return false;
  }

  case Instruction::SDiv: {
    // COMMUTATY = false
    // NEUTRO = 1
    // Casi: (N/1)
    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (C2 && C2->isOne()) {
      I.replaceAllUsesWith(Op1);
      errs() << "SDIV opt! Op1=1\n";
      return true;
    }
    return false;
  }

  case Instruction::UDiv: {
    // logica comune
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (C2 && C2->isOne()) {
      I.replaceAllUsesWith(Op1);
      errs() << "DIV opt! RHS=1\n";
      return true;
    }
    return false;
  }

    // altre istruzioni ...

  default:
    return false;
  }
}

bool LocalOpts::StrengthReductionOpt(Instruction &I) {
  errs() << "\nRunning su StrengthReduction:" << "\n";
  auto opCode = I.getOpcode();
  // cerco le tipologie di istruzione corrette
  auto Op1 = I.getOperand(0);
  auto Op2 = I.getOperand(1);
  switch (opCode) {
  case Instruction::Mul: {
    // COMMUTATIVA = true
    // è una mul?
    // sono scalari/vettori e NON float? se vettore costante splat.
    // estrai op1 e op2
    // se nessuno è constant int esci
    // prendi i valori in C e X
    // k = 2^k? fai shift
    // k != 2^k? fai
    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if ((!C1 && !C2) || (C1 && C2)) {
      return false;
    }
    ConstantInt *C = C1 ? C1 : C2;
    Value *X = C1 ? Op2 : Op1;
    // identità coperte da AlgebraicIdentity: 0, 1, -1
    if (C->isZero() || C->isOne() || C->isMinusOne())
      return false;

    // qui avrò una costante C e un valore X
    // controllo se C è potenza di 2
    APInt K = C->getValue().abs();
    bool Neg = C->isNegative();
    if (K.isPowerOf2()) {
      // ottengo di quanto shiftare X
      unsigned shift = K.logBase2();
      Type *Ty = X->getType();
      if (!Ty->isIntegerTy()) {
        return false;
      }
      unsigned BitWidth = cast<IntegerType>(Ty)->getBitWidth();
      auto *ShAmt = ConstantInt::get(cast<IntegerType>(Ty), shift);
      auto *Sh =
          BinaryOperator::Create(Instruction::Shl, X, ShAmt, "mul.sr.shl", &I);
      // a questo punto Sh vale (X<<shift)

      // gestione del segno
      Value *Res = Sh;
      if (Neg) {
        Res = BinaryOperator::CreateNeg(Sh, "mul.sr.neg", &I);
      }
      I.replaceAllUsesWith(Res);
      errs() << "MUL strength-reduced to shift for pow2\n";
      return true;
    }

    if (!K.isPowerOf2()) {
      // calcolo la prima potenza di 2 superiore
      unsigned shiftU = K.logBase2() + 1;
      Type *Ty = X->getType();
      if (!Ty->isIntegerTy()) {
        return false;
      }
      unsigned BitWidth = cast<IntegerType>(Ty)->getBitWidth();
      auto *ShAmt = ConstantInt::get(cast<IntegerType>(Ty), shiftU);
      auto *Sh =
          BinaryOperator::Create(Instruction::Shl, X, ShAmt, "mul.sr.shl", &I);
      auto *Sub =
          BinaryOperator::Create(Instruction::Sub, Sh, X, "mul.sr.shl.sub", &I);
      // gestione del segno
      Value *Res = Sub;
      if (Neg) {
        Res = BinaryOperator::CreateNeg(Sub, "mul.sr.neg.sub", &I);
      }
      I.replaceAllUsesWith(Res);
      errs() << "MUL strength-reduced to shift for not pow2\n";
      return true;
    }
    return false;
  }

  case Instruction::SDiv: {
    // Ottimizziamo solo X / C con C costante sul RHS (non commutativo).
    Value *Op1 = I.getOperand(0); // X
    Value *Op2 = I.getOperand(1); // C (costante)

    auto *C1 = dyn_cast<ConstantInt>(Op1);
    auto *C2 = dyn_cast<ConstantInt>(Op2);
    if (!C2 || C1)
      return false; // costante deve stare a destra

    ConstantInt *C = C2;
    Value *X = Op1;

    // Solo interi scalari (no float, no vettori qui).
    Type *ValTy = X->getType();
    if (!ValTy->isIntegerTy())
      return false;
    auto *IntTy = cast<IntegerType>(ValTy);
    unsigned BitWidth = IntTy->getBitWidth();
    if (BitWidth == 0)
      return false;

    // Esclusioni banali già coperte altrove o non trattabili qui.
    if (C->isZero() || C->isOne() || C->isMinusOne())
      return false;

    // K = |C|; NegDivisor vero se C < 0
    APInt K = C->getValue().abs();
    bool NegDivisor = C->isNegative();

    // Trattiamo solo divisori potenze di 2: K = 2^shift
    if (!K.isPowerOf2())
      return false;

    unsigned shift = K.logBase2();
    if (shift >= BitWidth)
      return false; // evita shift fuori range

    // ---- Bias per rispettare il troncamento verso 0 di sdiv ----
    // mask = (1<<shift) - 1   (stessa width di X)
    APInt MaskAP = (APInt(BitWidth, 1).shl(shift)) - APInt(BitWidth, 1);
    auto *MaskC = ConstantInt::get(IntTy, MaskAP);

    // sign = X >>_a (BitWidth - 1)  -> 0 se X>=0, -1 (tutti 1) se X<0
    auto *ShAmtSign = ConstantInt::get(IntTy, BitWidth - 1);
    Value *Sign = BinaryOperator::Create(Instruction::AShr, X, ShAmtSign,
                                         "sdiv.sr.sign", &I);

    // bias = sign & mask  -> 0 se X>=0, (2^shift - 1) se X<0
    Value *Bias = BinaryOperator::Create(Instruction::And, Sign, MaskC,
                                         "sdiv.sr.bias", &I);

    // adj = X + bias
    Value *Adj =
        BinaryOperator::Create(Instruction::Add, X, Bias, "sdiv.sr.adj", &I);

    // quot = adj >>_a shift
    auto *ShAmtK = ConstantInt::get(IntTy, shift);
    Value *Quot = BinaryOperator::Create(Instruction::AShr, Adj, ShAmtK,
                                         "sdiv.sr.ashr", &I);

    // Se il divisore era negativo, nega il risultato finale
    Value *Res =
        NegDivisor ? (Value *)BinaryOperator::CreateNeg(Quot, "sdiv.sr.neg", &I)
                   : (Value *)Quot;

    I.replaceAllUsesWith(Res);
    errs() << "SDIV strength-reduced to arithmetic right shift with bias\n";
    return true;
  }
  default:
    return false;
  }


}



bool LocalOpts::MultiInstructionOpt(Instruction &I) {
  // I = (x + C) - C → x

  // Lavoriamo solo su interi (no float/vettori).
  if (!I.getType()->isIntegerTy())
    return false;

  auto *OuterBO = dyn_cast<BinaryOperator>(&I);
  if (!OuterBO)
    return false;

  auto opCode = I.getOpcode();
  if (opCode != Instruction::Sub && opCode != Instruction::Add)
    return false;

  // Operandi dell'outer
  Value *OL = I.getOperand(0);
  Value *OR = I.getOperand(1);

  // Funzione di utilità: controlla se due costanti sono davvero uguali
  auto sameConst = [](const ConstantInt *A, const ConstantInt *B) -> bool {
    // Se almeno una non è una costante, non sono uguali
    if (!A || !B)
      return false;

    // Devono avere lo stesso tipo (es. entrambe i32, non i32 e i64)
    if (A->getType() != B->getType())
      return false;

    // Devono avere lo stesso valore numerico
    if (A->getValue() != B->getValue())
      return false;

    // Tutti i controlli passati: sono la stessa costante
    return true;
  };


  // Da qui in poi andremo a completare i singoli casi nello switch.
  switch (opCode) {
  case Instruction::Sub: {
    // ---------- Pattern 1: (x + C) - C -> x ----------
    // outer = Sub( InnerAdd , Cout )

    auto *Cout = dyn_cast<ConstantInt>(OR);
    // operando destro della SUB (C), prendo costante

    auto *InnerAdd = dyn_cast<BinaryOperator>(OL);
    // operando sinistro della SUB (X+C)
    if (Cout && InnerAdd && InnerAdd->getOpcode() == Instruction::Add) {
      // Prendo ADD come operando sinistro
      // Normalizza l'Add: costante a destra se necessario
      Value *IL = InnerAdd->getOperand(0); // candidato x
      Value *IR = InnerAdd->getOperand(1); // candidato C_in
      ConstantInt *Cin = dyn_cast<ConstantInt>(IR);
      if (!Cin) {
        if (auto *CinL = dyn_cast<ConstantInt>(IL)) {
          std::swap(IL, IR);
          Cin = CinL;
        }
      }

      if (Cin && sameConst(Cin, Cout)) {
        // Se gli operandi Cin e Cout coincidono allora abbiamo il pattern
        errs() << "MultiInstr (add-sub): (x + C) - C -> x  | " << I << "\n";
        I.replaceAllUsesWith(IL);   // IL è x dopo normalizzazione
        return true;
      }
    }

    // ---------- Pattern 2: C - (C - x) -> x ----------
    // outer = Sub( Cout , InnerSub ) con InnerSub = Sub( Cin , X )
    if (auto *Cout2 = dyn_cast<ConstantInt>(OL)) {
      if (auto *InnerSub = dyn_cast<BinaryOperator>(OR)) {
        if (InnerSub->getOpcode() == Instruction::Sub) {
          Value *IL2 = InnerSub->getOperand(0); // atteso Cin
          Value *IR2 = InnerSub->getOperand(1); // atteso x
          auto *Cin2 = dyn_cast<ConstantInt>(IL2); // costante davanti
          if (Cin2 && sameConst(Cin2, Cout2)) {
            errs() << "MultiInstr (front-sub): C - (C - x) -> x  | " << I << "\n";
            I.replaceAllUsesWith(IR2); // IR2 è x
            return true;
          }
        }
      }
    }

    break; // nessun match
  }

  case Instruction::Add: {
    // ---------- Pattern 3: (x - C) + C -> x ----------
    // outer = Add( InnerSub , Cout )
    // Per Add normalizziamo: se la costante è a sinistra, la spostiamo a destra
    ConstantInt *Cout = nullptr;
    Value *OLn = OL; // candidato Inner
    Value *ORn = OR; // candidato costante

    if ((Cout = dyn_cast<ConstantInt>(OLn))) {
      std::swap(OLn, ORn); // metto la costante a destra
    }
    if (!Cout) Cout = dyn_cast<ConstantInt>(ORn);
    if (!Cout) break; // niente costante, niente match

    // L'operando non costante (sinistro) deve essere una Sub
    auto *InnerSub = dyn_cast<BinaryOperator>(OLn);
    if (!InnerSub || InnerSub->getOpcode() != Instruction::Sub)
      break;

    // In Sub l'ordine conta: vogliamo Sub(X, Cin) con costante a destra
    Value *IL = InnerSub->getOperand(0);     // X
    Value *IR = InnerSub->getOperand(1);     // Cin
    auto *Cin = dyn_cast<ConstantInt>(IR);
    if (!Cin) break;

    // Stessa costante (tipo + valore) dentro e fuori
    if (sameConst(Cin, Cout)) {
      errs() << "MultiInstr (sub-add): (x - C) + C -> x  | " << I << "\n";
      I.replaceAllUsesWith(IL); // IL è x
      return true;
    }

    break; // nessun match
  }

  default:
    return false;
  }

  return false; // se nessun caso è scattato
}