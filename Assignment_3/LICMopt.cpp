#include "llvm/Transforms/Utils/LICMopt.h"
using namespace llvm;

// Driver del passo
llvm::PreservedAnalyses LICMopt::run(Function &F, FunctionAnalysisManager &FAM) {
  outs() << "\nRunning LICMopt su funzione: " << F.getName() << "\n";

  auto &LI = FAM.getResult<LoopAnalysis>(F);
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);

  bool changed = false;

  // Ciclo sui loop top-level della funzione
  for (Loop *L : LI) {
    bool localChanged = runOnLoop(L, DT);
    if (localChanged) {
      changed = true;
    }
  }

  if (changed) {
    outs() << "  [OK] Almeno un loop è stato modificato\n";
    return PreservedAnalyses::none();
  }

  outs() << "  [FALLITO] Nessuna trasformazione applicata\n";
  return PreservedAnalyses::all();
}


// ################################################################################
// Cuore operativo (LICM):
// - visita i blocchi del loop
// - individua istruzioni loop-invariant candidabili all'hoisting
// - sposta le istruzioni nel preheader in modo sicuro
bool LICMopt::runOnLoop(Loop *L, DominatorTree &DT) {

  if (!L) {
   outs() << "  [FALLITO] Loop nullo ricevuto in runOnLoop\n";
    return false;
  }

  BasicBlock *preheader = L->getLoopPreheader();
  // estraggo preheader dal loop
  // sarà il parcheggio delle istruzioni hoistate
  if (!preheader) {
    outs() << "   [FALLITO] Preheader assente.\n";
    return false;
  }

  SetVector<Instruction *> movable, moved;
  // movable: insieme ordinato (di inserimento) di istruzioni che possiamo
  // spostare moved: insieme di ciò che è stato spostato
  SmallVector<BasicBlock *> ExitBlocks;
  // insieme di BB di uscita dal loop (servono per prove di dominanza in
  // isSafeToMove
  L->getExitBlocks(ExitBlocks);
  // riempimento effettivo del vettore con i blocchi di uscita

  auto isLoopInvariant = [&](Instruction &I) -> bool {
    // qui controlla se una istruzione dentro il loop è Loop-invariant:
    // 1. accetta solo operatori binari
    // 2. operandi:
    //      - costanti
    //      - istruzioni già riconosciute invarianti
    //      - istruzioni definite fuori dal loop
    //      - istruzioni NON dipendenti da PHI
    //      - istruzioni NON dipendenti da istruzioni non ancora invariant

    if (!I.isBinaryOp())
      // solo operazioni binarie (add,sub,mul...)
      // NO a PHI, terminatori, load, store, call, cast, icmp
      return false;

    for (Value *op : I.operands()) {
      // esamina tutti gli operandi
      if (isa<Constant>(op) || isa<Argument>(op))
        continue;
      // se l'operando è costante o argomento di funzione è definito fuori da L

      if (auto *OpInst = dyn_cast<Instruction>(op)) {
        if (isa<PHINode>(OpInst))
          // niente PHI come dipendenze, legherebbe il valore all'iterazione
          return false;

        // se la dipendenza non è nel loop dove è definita?
        if (!L->contains(OpInst) || movable.contains(OpInst))
          // se fuori ok continua
          // se nel movable ok continua
          continue;

        // altrimenti è nel loop e ancora da muovere
        return false;
      }
    }
    return true;
  };


  // Scansione dei blocchi del loop e raccolta delle candidate
  for (BasicBlock *BB : depth_first(L->getHeader())) {
    // depth_first(...) fa scansione in profondità dall'header in giù
    if (!L->contains(BB))
      continue;
    // solo blocchi nel loop (NO exit block o succ fuori)

    for (Instruction &I : *BB) {
      // itera sulle istruzioni del I del BB
      if (isLoopInvariant(I) && isSafeToMove(I, L, DT, ExitBlocks)) {
        // controllo di candidabilità
        movable.insert(&I);
        // se positive al controllo si candidano
        outs() << "Found movable loopInvariant: " << I << "\n";
        // condizioni per una istruzione I
        // 1. NO side-effect
        // 2. deve dominare tutte le uscite
        // 3. non deve essere invalidata da PHI
        // 4. deve dominare tutti gli usi
      }
    }
  }

  // istruzione mossa
  for (Instruction *I : movable) {
    I->moveBefore(preheader->getTerminator());
    moved.insert(I);
    outs() << "Moved to preheader: " << *I << "\n";
  }

  if (moved.empty())
    return false;
  return true;
}







// ################################################################################
// qui stabilisco se una istruzione già riconosciuta Loop invriant può essere hoistata
bool LICMopt::isSafeToMove(Instruction &I, Loop *L, DominatorTree &DT,
                           SmallVector<BasicBlock *> ExitBlocks) {

  // 1)
  // tutti gli usi di I devono essere dentro il loop
  // vuol dire che il valore servirà solo nel loop.
  // se invece esiste un uso fuori bisogna andare al controllo successivo.
  auto deadOutsideLoopControll = [&](Instruction &I) -> bool {
    for (User *user : I.users()) {
      if (Instruction *userInstr = dyn_cast<Instruction>(user)) {
        if (!L->contains(userInstr)) {
          return false;
        }
      }
    }
    return true;
  };

  // 2)
  // il blocco che contiene I domina tutti gli exit blocks del loop
  // quando ci sono usi fuori dal loop
  // Check if instruction will execute before any possible loop exit.
  auto dominatesAllExitsControll = [&](Instruction &I) -> bool {
    for (BasicBlock *Exit : ExitBlocks) {
      if (!DT.dominates(I.getParent(), Exit)) {
        return false;
      }
    }

    return true;
  };

  // 3)
  // nessun PHI interno al loop usa I
  // se un PHI ricombina definizioni su più cammini spostare I rompe la
  // semantica imoedisce casi con multiple defs tramite PH Check for PHIs using
  // this value — assumes multiple defs
  auto definedOnlyOnceControll = [&](Instruction &I) -> bool {
    for (User *user : I.users()) {
      if (PHINode *phi = dyn_cast<PHINode>(user)) {
        if (L->contains(phi)) {
          return false;
        }
      }
    }
    return true;
  };

  // 4)
  // il bloco che contiene I domina il blocco di ogni uso
  // se vero spostare nel preheader aumenta la dominanza
  // The instruction must be executed before all its uses
  auto dominatesAllUsesControll = [&](Instruction &I) -> bool {
    for (User *user : I.users()) {
      if (Instruction *userInstr = dyn_cast<Instruction>(user)) {
        BasicBlock *userBlock = userInstr->getParent();
        if (userBlock && !DT.dominates(I.getParent(), userBlock)) {
          return false;
        }
      }
    }
    return true;
  };

  // alla fine se:
  // 1. il valore non serve fuori
  // 2. (serve fuori) e il valore domina le uscite
  // 3. niente PHI interni che danno definizioni multi-cammino
  // 4. c'è dominanza su tutti gli usi
  // ritorna true e rende l'istruzioni candidabile
  return (deadOutsideLoopControll(I) || dominatesAllExitsControll(I)) && definedOnlyOnceControll(I) &&
         dominatesAllUsesControll(I);
}
