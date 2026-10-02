#!/bin/sh
# Runs the plugin under Wine with an SKSE stand-in and checks log and environment.
set -eu
cd "$(dirname "$0")"
rm -rf work && mkdir -p work
cp ../MGOBudgetPlugin.dll ../MGOBudgetPlugin.ini skse_harness.exe work/
cd work
WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$PWD/prefix}" ${WINE:-wine} skse_harness.exe > out.txt 2>/dev/null
fail=0
check() { if grep -q "$2" "$1"; then echo "ok   $3"; else echo "FAIL $3"; fail=1; fi; }
check out.txt 'query=1 name=MGOBudgetPlugin' "SKSEPlugin_Query reports the plugin"
check out.txt 'load=1' "SKSEPlugin_Load succeeds"
check out.txt 'DXVK_CONFIG=dxgi.syncInterval=1;dxvk.maxMemoryBudget=13500;d3d11.cachedDynamicResources=c' "DXVK options appended"
check MGOBudgetPlugin.log 'Wine ' "Wine detected"
check MGOBudgetPlugin.log '\[check\] done' "health check ran"
check MGOBudgetPlugin.log 'QuestFixGenericProfile=0' "Quest fix off by default"
exit $fail
