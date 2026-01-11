#!/usr/bin/env bash
set -euo pipefail

LLVM_BIN="/home/pierpop/workspace/LLVM_17/INSTALL/bin"
OPT="$LLVM_BIN/opt"
CLANG="clang"
PASS_NAME="licm-opt"

SRC="test.c"
BC="test.bc"
BASE_LL="test_base.ll"
OUT_LL="test_licm.ll"

# Controllo esistenza file sorgente
if [ ! -f "$SRC" ]; then
    echo "Errore: $SRC non trovato."
    exit 1
fi

echo "[1/3] Compilazione..."
"$CLANG" -O0 -Xclang -disable-O0-optnone -emit-llvm -c "$SRC" -o "$BC"

echo "[2/3] Preparazione (mem2reg)..."
"$OPT" -S -verify-each -passes='mem2reg,loop-simplify' "$BC" -o "$BASE_LL"

echo "[3/3] Esecuzione $PASS_NAME..."
"$OPT" -S -verify-each -passes="$PASS_NAME" "$BASE_LL" -o "$OUT_LL"

echo
echo
echo
echo "PRIMA (Base):"
echo "=========================================="
cat "$BASE_LL"
echo "=========================================="

echo
echo
echo
echo "DOPO (Licm):"
echo "=========================================="
cat "$OUT_LL"
echo "=========================================="

#echo
#echo "DIFF:"
#echo "=========================================="
# Aggiunto "|| true" per evitare che lo script vada in errore se trova differenze
#diff -u \
#  <(grep -v "ModuleID" "$BASE_LL") \
#  <(grep -v "ModuleID" "$OUT_LL") || true
#echo "=========================================="
