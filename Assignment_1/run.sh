#!/usr/bin/env bash
set -euo pipefail

# --- Config ---
SRC="test.c"
BC_IN="test.bc"
LL_IN="test.ll"
BC_OUT="test_out.bc"
LL_OUT="test_out.ll"

# Pipeline: mem2reg + tuo pass
# (Opzionale) aggiungi adce per pulire istruzioni morte rimaste
PIPELINE="mem2reg,local-opts"
# PIPELINE="mem2reg,local-opts,adce"

# --- Checks ---
if [[ ! -f "$SRC" ]]; then
  echo "Errore: $SRC non trovato"
  exit 1
fi

echo "check su $SRC, trovato."
echo "---------------------------"

rm -f "$BC_IN" "$LL_IN" "$BC_OUT" "$LL_OUT"
echo "rm sui file precedenti per rimuoverli."
echo "---------------------------"

clang -O0 -Xclang -disable-O0-optnone -emit-llvm -c "$SRC" -o "$BC_IN"
echo "clang su $SRC per ottenere $BC_IN fatto."
echo "---------------------------"

llvm-dis "$BC_IN" -o "$LL_IN"
echo "llvm-dis su $BC_IN per ottenere $LL_IN fatto."
echo "---------------------------"

opt -passes="$PIPELINE" "$BC_IN" -o "$BC_OUT"
echo "opt (-passes='$PIPELINE') eseguito."
echo "---------------------------"

llvm-dis "$BC_OUT" -o "$LL_OUT"
echo "llvm-dis su $BC_OUT per ottenere $LL_OUT fatto."
echo "---------------------------"
echo

echo "Confronto di test prima e dopo l'ottimizzazione:"

# diff ritorna 0 se uguali, 1 se diversi.
# Qui NON vogliamo che 1 faccia fallire lo script, quindi gestiamo esplicitamente.
if diff -q "$LL_IN" "$LL_OUT" >/dev/null; then
  echo "Nessuna differenza: IR identico."
else
  echo "Differenze trovate (ok). Diff unificato:"
  diff -u "$LL_IN" "$LL_OUT" || true
fi

