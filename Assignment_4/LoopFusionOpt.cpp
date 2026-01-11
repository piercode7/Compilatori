#include "llvm/Transforms/Utils/LoopFusionOpt.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// *****************************************************************************
// Driver del passo
llvm::PreservedAnalyses llvm::LoopFusionOpt::run(Function &F,
                                                 FunctionAnalysisManager &FAM) {
  // F: Funzione in cui applico modifiche all'IR
  // FAM: Gestore delle analisi. Memorizza e fornisce risultati analisi
  // LI: Contiene la Struttura di tutti i Loop di F per analizzarli
  // topLevelLoops: elenco di tutti i Loop di alto livello
  // - runOnLoops: funzione con logica di controllo e trasformazione
  // PreservedAnalyses: segnalatore di stato per le analisi precedenti

  outs() << "\nAvvio LoopFusionOpt su funzione: " << F.getName() << "\n";
  auto &LI = FAM.getResult<LoopAnalysis>(F);
  bool changed = false;
  const std::vector<Loop *> &topLevelLoops = LI.getTopLevelLoops();
  // crea array di loop toplevel da LI

  changed = runOnLoops(F, FAM, topLevelLoops);
  // tenta di fare delle fusioni interne passandogli analizzatore, toplevel loops e F

  if (changed)
    return llvm::PreservedAnalyses::none();
  return llvm::PreservedAnalyses::all();
}

// *****************************************************************************
// Itera i loop
bool LoopFusionOpt::runOnLoops(Function &F, FunctionAnalysisManager &FAM,
                               const std::vector<Loop *> &Loops) {
  bool changed = false;

  outs() << "\n[runOnLoops] Loop top-level trovati: " << Loops.size() << "\n";

  // Iteriamo a ritroso sui top-level loops.

  Loop *Prec = nullptr;

  for (auto It = Loops.rbegin(); It != Loops.rend(); ++It) {
    Loop *Corr = *It;

    if (!Corr) {
      outs() << "   [FALLITO] loop nullo nella lista top-level\n";
      continue;
    }

    // Provo la fusione solo se ho una coppia consecutiva nella scansione (Prec, Corr).
    if (Prec && isOptimizable(F, FAM, Prec, Corr)) {
      outs() << "   [OK]: coppia fondibile, eseguo fuseLoops\n";

      Loop *Fused = fuseLoops(F, FAM, Prec, Corr);
      if (Fused) {
        Prec = Fused;
        changed = true;
        continue;
      }

      outs() << "   [FALLITO] fuseLoops ha restituito nullptr\n";
    }

    // Se non ho fuso, avanzo: la coppia successiva sarà (Corr, prossimo).
    Prec = Corr;
  }

  return changed;
}


// Helper per cancellare blocchi divenuti inutili dopo la fusione.
// Non devono avere piu usi
static void deleteBlock(BasicBlock *BB) {
  if (!BB)
    return;
  // Il blocco deve essere completamente scollegato dal CFG.
  assert(BB->use_empty() && "Deleting a block that is still referenced");
  // Rimuove tutte le istruzioni dal blocco (dal fondo per sicurezza),
  while (!BB->empty())
    BB->back().eraseFromParent();
  BB->eraseFromParent();
}


// *****************************************************************************
// Esegue il taglia e cuci
Loop *LoopFusionOpt::fuseLoops(Function &F, FunctionAnalysisManager &FAM,
                               Loop *First, Loop *Second) {

  if (!First || !Second)
    return nullptr;

  ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = FAM.getResult<LoopAnalysis>(F);

  // === Componenti fondamentali dei due loop ===
  auto *firstPreheader = First->getLoopPreheader();
  auto *firstLatch = First->getLoopLatch();
  auto *firstExit = First->getExitBlock();

  auto *secondPreheader = Second->getLoopPreheader();
  auto *secondLatch = Second->getLoopLatch();
  auto *secondExit = Second->getExitBlock();

  // Devono esserci per rscrivere il CFG poi
  if (!firstPreheader || !firstLatch || !firstExit || !secondPreheader ||
      !secondLatch || !secondExit)
    return nullptr;

  // Devono essere definiti e avere un successore, da usare poi
  if (!firstPreheader->getSingleSuccessor() ||
      !firstLatch->getSinglePredecessor() ||
      !secondPreheader->getSingleSuccessor() ||
      !secondLatch->getSinglePredecessor())
    return nullptr;

  auto *firstBody = firstLatch->getSinglePredecessor();
  auto *firstHeader = firstPreheader->getSingleSuccessor();
  auto *firstGuard = First->getLoopGuardBranch();

  auto *secondBody = secondLatch->getSinglePredecessor();
  auto *secondHeader = secondPreheader->getSingleSuccessor();
  auto *secondGuard = Second->getLoopGuardBranch();

  // === 1) Unificazione della i: Loop2 usa la i di Loop1 al posto della sua j ===
  auto *firstIV = First->getInductionVariable(SE);
  auto *secondIV = Second->getInductionVariable(SE);

  if (!firstIV || !secondIV)
    return nullptr;

  secondIV->replaceAllUsesWith(firstIV);
  secondIV->eraseFromParent();
  // Resta solo i di Loop1

  // === 2) Sistemazione PHI e gestione LCSSA ===
  // controllo: è un LCSSA?
  auto isLCSSAPhi = [](PHINode *PHI, Loop *L) {
    if (L->contains(PHI->getParent())) {
      // il nodo PHI LCSSA deve essere in un blocco fuori dal loop (tipicamente
      // exit block/post-exit)
      return false;
    }

    for (unsigned i = 0, e = PHI->getNumIncomingValues(); i < e; ++i)
      if (!L->contains(PHI->getIncomingBlock(i))) {
        // il valore deve provenire dall'interno del loop
        return false;
        // la LCSSA è definita fuori dal loop, ma raccoglie valori da blocchi nel
        // loop
      }
    return true;
  };
  // i nodi LCSSA devono stare SOLO sui confini di uscita. Da spostare i
  // precedenti

  // Aggiornamenti (vecchio, nuovo)
  secondHeader->replacePhiUsesWith(secondLatch, firstLatch);
  // nel secondHeader ai PHI si accederà da firstLatch
  secondHeader->replacePhiUsesWith(secondPreheader, firstPreheader);
  // nel secondHeader ai PHI si accederà da firstLatch
  secondPreheader->replacePhiUsesWith(secondPreheader->getSinglePredecessor(), firstBody);
  // nel secondPreheader  si accederà da firstBody
  secondExit->replacePhiUsesWith(secondLatch, firstLatch);
  // nel secondExit ai PHI si accederà da firstLatch

  // raccoglie i PHI di secondHeader
  SmallVector<PHINode *, 8> secondHeaderPHIs;
  for (auto &I : *secondHeader)
    if (auto *PHI = dyn_cast<PHINode>(&I))
      secondHeaderPHIs.push_back(PHI);

  // raccoglie i PHI di firstHeader
  SmallVector<PHINode *, 8> firstHeaderPHIs;
  for (auto &I : *firstHeader)
    if (auto *PHI = dyn_cast<PHINode>(&I))
      firstHeaderPHIs.push_back(PHI);

  // Spostiamo i PHI di header2 in header1, eliminando LCSSA "intermedia" quando possibile.
  Instruction *insertBefore = firstHeader->getFirstNonPHI();
  for (auto *PHI : secondHeaderPHIs) {
    Value *in0 = PHI->getIncomingValue(0);
    Value *in1 = PHI->getIncomingValue(1);

    // Caso: PHI che prende come valore una LCSSA PHI del primo loop.
    if (auto *lcssaPHI = dyn_cast<PHINode>(in0)) {
      if (firstExit == lcssaPHI->getParent() && isLCSSAPhi(lcssaPHI, First)) {
        Value *lcssaValue = lcssaPHI->getIncomingValue(0);

        // Aggiorna i PHI del primo header che dipendevano da quel valore.
        for (auto *firstPHI : firstHeaderPHIs)
          if (firstPHI->getIncomingValue(1) == lcssaValue)
            firstPHI->setIncomingValue(1, in1);

        PHI->replaceAllUsesWith(lcssaValue);
        PHI->eraseFromParent();
        lcssaPHI->eraseFromParent();
        continue;
      }
    }

    PHI->moveBefore(insertBefore);
  }
  // Ora i dati non si fermano più nel blocco di uscita del primo loop ma vanno
  // dritti al secondo
  // ora le PHINode di Loop2 sono nel preheader di Loop1

  // Spostiamo le LCSSA PHI dall'exit del primo loop all'exit del secondo (nuova exit comune).
  Instruction *movePoint = secondExit->getFirstNonPHI();
  SmallVector<PHINode *> lcssaToMove;
  for (Instruction &I : *firstExit)
    if (auto *phi = dyn_cast<PHINode>(&I)) {
      phi->setIncomingBlock(0, firstLatch);
      lcssaToMove.push_back(phi);
    }

  for (auto *phi : lcssaToMove)
    phi->moveBefore(movePoint);
  // i PHINode di exit1 vanno in exit2 comune

  // === 3) Caso guarded: unifichiamo la "skip path" (se salta First deve saltare anche Second) ===
  if (firstGuard && secondGuard) {
    BasicBlock *guardDest = secondExit->getSingleSuccessor();

    // Robustezza: se non esiste un successor unico, abortiamo
    if (!guardDest)
      return nullptr;

    // Il ramo "non loop" della guard di First deve saltare all'uscita comune.
    firstGuard->setSuccessor(1, guardDest);
    guardDest->replacePhiUsesWith(secondGuard->getParent(),
                                  firstGuard->getParent());

    // Evita di perdere l'edge: ricolleghiamo e poi sposteremo le istruzioni.
    secondGuard->replaceSuccessorWith(guardDest, secondGuard->getParent());

    // Blocco temporaneamente l'uscita di First su se stessa (sarà rimossa).
    firstExit->getTerminator()->setSuccessor(0, firstExit);

    // Sposto eventuali istruzioni "utili" del guard2 nell'uscita comune (prima del terminator).
    Instruction *insertPt = guardDest->getFirstNonPHI();
    SmallVector<Instruction *> toMove;

    for (Instruction &I : *secondGuard->getParent())
      if (!I.isTerminator() && &I != secondGuard->getCondition())
        toMove.push_back(&I);

    for (Instruction *inst : toMove)
      inst->moveBefore(insertPt);

    guardDest->replacePhiUsesWith(firstExit, secondExit);

    deleteBlock(secondGuard->getParent());
    deleteBlock(firstExit);
  }
  // se salta il primo loop anche il secondo, poi prendo i pezzi nel Guard2 e li
  // metto
  // in exit2 ed elimino i blocchi fantasma

  // === 4) Riscrittura CFG: incolla il corpo del secondo loop dentro il primo ===
  firstLatch->getTerminator()->setSuccessor(1, secondExit);
  firstBody->getTerminator()->replaceSuccessorWith(firstLatch, secondHeader);
  secondBody->getTerminator()->replaceSuccessorWith(secondLatch, firstLatch);
  secondLatch->getTerminator()->replaceSuccessorWith(secondExit, secondLatch);

  deleteBlock(secondLatch);
  deleteBlock(secondPreheader);


  // === 5) Aggiorna LoopInfo: i blocchi del secondo loop diventano blocchi del primo ===
  SmallVector<BasicBlock *, 16> secondBlocks;
  for (BasicBlock *BB : Second->getBlocks()) {
    if (BB == secondLatch || BB == secondPreheader)
      continue;
    secondBlocks.push_back(BB);
  }

  for (BasicBlock *BB : secondBlocks) {
    First->addBasicBlockToLoop(BB, LI);
    BB->moveBefore(firstLatch);
  }

  return First;
}

// *****************************************************************************
// Verifica le condizioni di ottimizzabilita
// 0. Strutturali
// 1. Adiacenza
// 2. Trip Count
// 3. Control Flow
// 4. Dipendenze
bool LoopFusionOpt::isOptimizable(Function &F, FunctionAnalysisManager &FAM,
                                  Loop *First, Loop *Second) {
  raw_ostream &OS = outs();

  OS << "\n\n#############################################\n";
  OS << "[LoopFusionOpt] isOptimizable()\n";
  OS << "  Funzione: " << F.getName() << "\n";
  OS << "  Primo puntatore:  " << (const void*)First  << "\n";
  OS << "  Secondo puntatore: " << (const void*)Second << "\n";


  // Analisi dominanza e post-dominanza da usare poi
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);

  if (!First || !Second) {
    OS << "   [FALLITO] Uno dei loop è null.\n";
    return false;
  }

  // === Struttura del loop ===
  // preheader, header, latch ed exit unici.
  auto strutturaAdeguata = [&](Loop *L, const char *Tag) -> bool {
    BasicBlock *Preheader = L->getLoopPreheader();
    BasicBlock *Latch = L->getLoopLatch();
    BasicBlock *Exit = L->getExitBlock();
    BasicBlock *Header = L->getHeader();

    if (!Preheader || !Latch || !Exit || !Header) {
      OS << "   [FALLITO] Manca qualcuno dei componenti fondamentali.\n";
      return false;
    }
    if (!Preheader->getSingleSuccessor()) {
      OS << "   [FALLITO] Preheader non ha un unico successore.\n";
      return false;
    }
    if (!Latch->getSinglePredecessor()) {
      OS << "   [FALLITO] Latch non ha unico predecessore.\n";
      OS << "     predecessori latch: ";
      for (auto *P : predecessors(Latch)) { OS << P->getName() << " "; }
      OS << "\n";
      return false;
    }
    OS << "   [OK] Struttura loop adeguata.\n";
    return true;
  };

  if (!strutturaAdeguata(First, "First") || !strutturaAdeguata(Second, "Second")) {
    OS <<   "[FALLITO] Struttura lop non adeguata.\n";
    return false;
  }


  // ==== 1) ADIACENZA ====
  // Controllo sulla adiacenza dei loop tramite uscita First e entry Second
  auto adjacentControll = [&]() -> bool {
    OS << "\n[Controllo 1/4] Adiacenza\n";

    BasicBlock *firstExitBB = nullptr;
    BasicBlock *secondEntryBB = nullptr;

    // uscita First
    if (First->isGuarded()) {
      BasicBlock *Exit = First->getExitBlock();
      if (Exit)
        firstExitBB = Exit->getSingleSuccessor();
    } else {
      firstExitBB = First->getExitBlock();
    }

    if (!firstExitBB) {
      OS << "   [FALLITO]: firstExitBB è null.\n";
      return false;
    }

    // ingresso Second
    if (Second->isGuarded()) {
      auto *GB = Second->getLoopGuardBranch();
      if (GB)
        secondEntryBB = GB->getParent();
    } else {
      secondEntryBB = Second->getLoopPreheader();
    }

    if (!secondEntryBB) {
      OS << "   [FALLITO]: secondExitBB è null.\n";
      return false;
    }

    // Controllo decisivo
    if (firstExitBB == secondEntryBB) {
      OS << "   [OK] I loop sono adiacenti.\n";
      return true;
    }

    OS << "   [FALLITO] I loop NON sono adiacenti.\n";
    return false;
  };

  // ==== 2) TRIP COUNT ====
  // Controllo sul numero di iterazioni
  auto sameLoopTripCountControll = [&]() -> bool {
    OS << "\n[Controllo 2/4] Trip Count\n";
    ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    const SCEV *TC1 = SE.getBackedgeTakenCount(First);
    const SCEV *TC2 = SE.getBackedgeTakenCount(Second);

    OS << "  TC First  = " << *TC1 << "\n";
    OS << "  TC Second = " << *TC2 << "\n";

    if (isa<SCEVCouldNotCompute>(TC1) || isa<SCEVCouldNotCompute>(TC2)) {
      OS << "   [FALLITO] Manca informazione su almeno un trip count.\n";
      return false;
    }

    if (TC1->getType() != TC2->getType()) {
      OS << "  [FALLITO] I trip count hanno tipi differenti.\n";
      return false;
    }

    // TC1 e TC2 non sono numeri ma espressioni simboliche SCEV (n-1, n ecc)
    bool eq = SE.isKnownPredicate(ICmpInst::ICMP_EQ, TC1, TC2);
    if (!eq) {
      OS << "   [FALLITO] trip count diversi o non dimostrabilmente uguali\n";
      OS << "     TC First  = " << *TC1 << "\n";
      OS << "     TC Second = " << *TC2 << "\n";
      return false;
    }

    OS << "   [OK] Stesso trip count dimostrato.\n";
    return true;
  };

  // ==== 3) EQUIVALENZA FLUSSO ====
  // Controllo sulla correttezza del flusso: First deve dominare Second e Second deve post-dominare First.
  // Se i loop sono guarded, richiediamo anche guard equivalente (stessa condizione).
  auto controlFlowEqControll = [&]() -> bool {
    OS << "\n[Check 3/4] Equivalenza flusso (DT + PDT + GuardEq)\n";

    BasicBlock *E1 = First->getHeader();
    BasicBlock *E2 = Second->getHeader();

    // Policy: o entrambi guarded o entrambi non-guarded
    if (First->isGuarded() != Second->isGuarded()) {
      OS << "   [FALLITO] loop misti (guarded/non-guarded)\n";
      return false;
    }

    // Guard equivalence (solo se entrambi guarded)
    if (First->isGuarded() && Second->isGuarded()) {
      OS << "  Entrambi i loop sono guarded\n";

      BranchInst *G1 = First->getLoopGuardBranch();
      BranchInst *G2 = Second->getLoopGuardBranch();

      if (!G1 || !G2) {
        OS << "   [FALLITO] guard branch mancante in uno dei due loop\n";
        return false;
      }

      // Nei loop guarded, l'entry per i check è il basic block della guard (non l'header del loop)
      E1 = G1->getParent();
      E2 = G2->getParent();

      if (!G1->isConditional() || !G2->isConditional()) {
        OS << "   [FALLITO] guard non condizionale in uno dei due loop\n";
        return false;
      }

      auto *C1 = dyn_cast<ICmpInst>(G1->getCondition());
      auto *C2 = dyn_cast<ICmpInst>(G2->getCondition());

      if (!C1 || !C2) {
        OS << "   [FALLITO] guard non basata su ICmp\n";
        return false;
      }

      if (!C1->isIdenticalTo(C2)) {
        OS << "   [FALLITO] condizioni di guard diverse\n";
        OS << "     GuardFirst  = " << *C1 << "\n";
        OS << "     GuardSecond = " << *C2 << "\n";
        return false;
      }
    }

    if (!E1 || !E2) {
      OS << "   [FALLITO] entry block nullo per il check di dominanza\n";
      return false;
    }

    bool dom  = DT.dominates(E1, E2);
    bool pdom = PDT.dominates(E2, E1);

    if (!dom || !pdom) {
      OS << "   [FALLITO] dominanza/post-dominanza non soddisfatte\n";
      OS << "     DT:  First domina Second? " << (dom  ? "true" : "false") << "\n";
      OS << "     PDT: Second post-domina First? " << (pdom ? "true" : "false") << "\n";
      return false;
    }

    OS << "  -> OK\n";
    return true;
  };


// ==== 4) DEPENDENCE CHECK ====
// Obiettivo: evitare fusioni non sicure per dipendenze tra i due loop.
auto notDependenciesControll = [&]() -> bool {
  OS << "\n[Check 4/4] Dipendenze\n";

  auto &DI = FAM.getResult<DependenceAnalysis>(F);
  ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  const DataLayout &DL = F.getParent()->getDataLayout();

  // Ritorna il pointer operand solo per load/store (altrimenti nullptr).
  auto getInstructionPointer = [&](Instruction *Inst) -> Value * {
    if (auto *S = dyn_cast<StoreInst>(Inst))
      return S->getPointerOperand();
    if (auto *L = dyn_cast<LoadInst>(Inst))
      return L->getPointerOperand();
    return nullptr;
  };


  for (auto *BB1 : First->getBlocks()) {
    for (auto &I1 : *BB1) {
      for (auto *BB2 : Second->getBlocks()) {
        for (auto &I2 : *BB2) {

          // 1) Esiste una dipendenza tra I1 e I2?
          auto Dep = DI.depends(&I1, &I2, /*LoopIndependent=*/true);
          if (!Dep)
            continue;

          // 2) Provo ad estrarre i pointer operand (solo load/store).
          Value *P1 = getInstructionPointer(&I1);
          Value *P2 = getInstructionPointer(&I2);
          if (!P1 || !P2) {
            continue;
          }

          // 3) Rappresento gli indirizzi come espressioni SCEV.
          const SCEV *E1 = SE.getSCEV(P1);
          const SCEV *E2 = SE.getSCEV(P2);

          // 4) Voglio AddRec (pattern affine nel loop) per stimare una distanza.
          auto *AR1 = dyn_cast<SCEVAddRecExpr>(E1);
          auto *AR2 = dyn_cast<SCEVAddRecExpr>(E2);
          if (!AR1 || !AR2) {
            continue;
          }

          // 5) Se gli step sono diversi, niente confronto.
          const SCEV *Step1 = AR1->getStepRecurrence(SE);
          const SCEV *Step2 = AR2->getStepRecurrence(SE);
          if (Step1 != Step2) {
            continue;
          }

          // 6) Distanza = start1 - start2. Deve essere costante per usarla.
          const SCEV *Dist = SE.getMinusSCEV(AR1->getStart(), AR2->getStart());
          auto *CD = dyn_cast<SCEVConstant>(Dist);
          if (!CD) {
            continue;
          }

          // 7) Converto la distanza da byte a numero di elementi.
          const APInt &ByteOffset = CD->getAPInt();

          auto *GEP1 = dyn_cast<GetElementPtrInst>(P1);
          auto *GEP2 = dyn_cast<GetElementPtrInst>(P2);
          if (!GEP1 && !GEP2) {
            continue;
          }

          Type *ElemTy = GEP1 ? GEP1->getResultElementType()
                              : GEP2->getResultElementType();

          uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
          if (ElemSize == 0) {
            continue;
          }

          int64_t ElementOffset =
              ByteOffset.getSExtValue() / (int64_t)ElemSize;

          // se la distanza è negativa, la fusione non e sicura
          if (ElementOffset < 0) {
            OS << "   [FALLITO] dipendenza con distanza negativa\n";
            OS << "     I1 (" << BB1->getName() << "): " << I1 << "\n";
            OS << "     I2 (" << BB2->getName() << "): " << I2 << "\n";
            OS << "     distanza(elementi) = " << ElementOffset << "\n";
            return false;
          }
        }
      }
    }
  }

  OS << "   [OK]\n";


  return true;
};


  // Combinazione dei controlli
  bool Adj = adjacentControll();
  bool TC  = sameLoopTripCountControll();
  bool CF  = controlFlowEqControll();
  bool Dep = notDependenciesControll();

  OS << "\n[Flags] Adiacenza=" << Adj
     << " TripCount=" << TC
     << " ControlFlow=" << CF
     << " Dipendenze=" << Dep << "\n";

  bool ok = Adj && TC && CF && Dep;

  if (!ok) {
    OS << "[RISULTATO] isOptimizable = false\n";
  } else {
    OS << "[RISULTATO] isOptimizable = true\n";
  }
  OS << "#############################################\n\n";
  return ok;
}

