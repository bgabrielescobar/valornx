#!/usr/bin/env bash
# Compila el simulador y ejecuta los guiones de prueba (cada uno con una microSD vacía).
set -e
cd "$(dirname "$0")"
make -s sim
FONT="${FONT:-/mnt/c/Windows/Fonts/segoeui.ttf}"
fails=0
for s in scripts/*.txt; do
  name=$(basename "$s" .txt)
  echo "### $name"
  rm -rf sim_sd
  ./build/sim "$FONT" "$s" shots || fails=$((fails+1))
done
rm -rf sim_sd
echo "guiones con fallos: $fails"
exit $fails
