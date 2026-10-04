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
check out.txt 'listener 7 for SKSE' "listens for SKSE messages"
check out.txt 'dispatch from 7 type 0x9a3f1c08 len 8 to devbench' "asks DevBench for its interface"
check out.txt 'registered mgobudget.status' "DevBench tool mgobudget.status registered"
check out.txt 'registered mgobudget.set' "DevBench tool mgobudget.set registered"
check out.txt 'tool mgobudget.status {} -> {"plugin":"MGOBudgetPlugin"' "status answers with JSON"
check out.txt '"dxvkConfig":"dxgi.syncInterval=1;dxvk.maxMemoryBudget=13500' "status shows DXVK_CONFIG"
check out.txt 'mib": -1} -> {"ok":false' "set rejects a negative budget"
check out.txt 'mgobudget.set {} -> {"ok":false' "set rejects missing mib"
check MGOBudgetPlugin.log 'DevBench build 12200: tools' "log names the DevBench build"
if grep -q '"hooked":true' out.txt; then
    check out.txt 'mib": 20000} -> {"ok":true,"previousMiB":24000,"reportedMiB":20000}' "set changes the budget live"
    check out.txt '"reportedMiB":20000,' "status shows the new budget"
    check out.txt 'dxgi budget before set: 24000 MiB' "DXGI reports the configured budget"
    check out.txt 'dxgi budget after set: 20000 MiB' "DXGI reports the live budget"
    check out.txt '"queries":2,' "status counts the DXGI queries"
else
    echo "skip budget hook not installed under this Wine (no DXGI adapter), live set not checked"
fi
exit $fail
