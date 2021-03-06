#!/bin/bash

rm -f *.rasl-module *.rasl caller
../../../bin/rlc --keep-rasls -OC -R module.sref --incorporated=alias
../../../bin/rlmake --keep-rasls -X-OC -X--markup-context caller.ref
./caller
if [[ $? -gt 0 ]]; then
  echo TEST FAILED!
  exit 1
else
  echo Test OK!
  rm -f *.rasl-module *.rasl caller *.lst
fi
