#!/usr/bin/env bash
set -u
mkdir -p out
fail=0
for src in examples/*.cpp; do
  name=$(basename "$src" .cpp)
  if [ "$name" = "bad" ]; then
    if ./cppparser "$src" >/dev/null 2>&1; then echo "FAIL $name: expected syntax error"; fail=1; else echo "ok   $name (syntax error reported)"; fi
    continue
  fi
  if [ "$name" = "sema_errors" ]; then
    ./cppparser "$src" >/dev/null 2>&1; code=$?
    if [ $code -eq 4 ]; then echo "ok   $name (semantic errors reported)"; else echo "FAIL $name: expected semantic errors, exit $code"; fail=1; fi
    continue
  fi
  input=""; [ -f "examples/$name.in" ] && input=$(cat "examples/$name.in")
  echo "$input" | ./cppparser "$src" --run --gen "out/$name.py" --ast "out/$name.json" > "out/$name.interp.txt" 2>/dev/null || { echo "FAIL $name: interpreter"; fail=1; continue; }
  echo "$input" | python3 "out/$name.py" > "out/$name.py.txt" 2>/dev/null || { echo "FAIL $name: python"; fail=1; continue; }
  if diff -q "out/$name.interp.txt" "out/$name.py.txt" >/dev/null; then echo "ok   $name"; else echo "FAIL $name: outputs differ"; diff "out/$name.interp.txt" "out/$name.py.txt"; fail=1; fi
done
exit $fail
