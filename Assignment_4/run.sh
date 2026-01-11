#!/usr/bin/env bash

# 1. SETUP E CONTROLLI
if [ ! -f test.c ]; then
  echo "Errore: test.c non trovato"
  exit 1
fi
echo "check su test.c: OK."
echo "---------------------------"

# Pulizia
rm -f test.bc test.ll test_pre.bc test_pre.ll test_out.bc test_out.ll

# 2. COMPILAZIONE (C -> Bitcode Grezzo)
# Usiamo O0 ma disabilitiamo optnone per permettere a opt di lavorare
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -c test.c -o test.bc
echo "clang: Generato test.bc (Grezzo)"
echo "---------------------------"

# 3. PREPARAZIONE (Canonicalizzazione)
# Qui eseguiamo tutti i passi standard per pulire i loop.
# Questo crea l'input PERFETTO per il tuo passo.
echo "Eseguo fase PREPARAZIONE (mem2reg, simplify, rotate, lcssa)..."
opt -passes='mem2reg,loop-simplify,loop-rotate,lcssa' test.bc -o test_pre.bc

if [ ! -f test_pre.bc ]; then
  echo "ERRORE: La fase di preparazione ha fallito."
  exit 1
fi

# Disassembliamo il file preparato.
# Questo file ci serve per vedere come sono i loop PRIMA della fusione.
llvm-dis test_pre.bc -o test_pre.ll
echo "Generato test_pre.ll (IR Pulito, pronto per la fusione)"
echo "---------------------------"

# 4. ESECUZIONE LOOP FUSION (Solo il tuo passo)
echo "Eseguo IL TUO PASSO (loop-fusion-opt)..."

# Nota: input è test_pre.bc, non test.bc!
opt -passes='loop-fusion-opt' test_pre.bc -o test_out.bc

if [ ! -f test_out.bc ]; then
  echo "ERRORE: loop-fusion-opt ha fallito."
  exit 1
fi

llvm-dis test_out.bc -o test_out.ll
echo "Generato test_out.ll (IR Finale)"
echo "---------------------------"

# 5. CONFRONTO INTELLIGENTE
echo
echo "=== RISULTATO DEL DIFF ==="
echo "Confronto tra PRE (Loop puliti) e OUT (Dopo il tuo passo)."
echo "Se non vedi nulla qui sotto, significa che NON ha fuso."
echo

diff test_pre.ll test_out.ll || true

echo
echo "---------------------------"
echo "Fine."
