# Deployment baseline

## Windows baseline

| Machine | Role | ProductName | DisplayVersion | CurrentBuild |
|---|---|---|---|---|
| DESKTOP-P4KR8FD | development only | Windows 10 Pro | 22H2 | 19045 |

## Plant baseline — UNVERIFIED (DEFERRED 2026-08-11)

No plant PC has been inspected. Qt 6 requires Windows 10 build 17763 or later, and
nothing below has been checked against a real target machine.

**Trigger:** before any plant PC is purchased, written into a tender, or inherited from
an existing line, re-run T1 and T2 against that machine. A machine below build 17763
invalidates Gate A and returns the decision to the sidecar (A1) or OT-network (A2)
option — after the port is already paid for.

**Cheapest mitigation:** make it a procurement constraint rather than a later discovery.
"Windows 10 build 17763 or later, x64" costs nothing to specify while hardware is still
being chosen, and is the default on anything bought new.

## Vendor DLL architecture — NOT APPLICABLE IN P1 (DEFERRED 2026-08-11)

Phase 1 loads no vendor or device DLL. No architecture constraint is known.

**Trigger:** when the first device SDK is chosen, determine its architecture *before*
committing to it. An x86-only SDK forces a 32-bit process, and Qt 6 ships no official
32-bit Windows binaries — which invalidates Gate A after the port is already paid for.

    dumpbin /headers <vendor.dll> | Select-String "machine"

`dumpbin` ships with Visual Studio. An SDK whose architecture cannot be determined counts
as `unknown`, and `unknown` blocks the decision — a guess does not.

## Verdict

GO (DEV-ONLY)
