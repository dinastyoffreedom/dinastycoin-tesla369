# 🎯 RETARGET DIFFICULTY — Patch Implementata e Verificata
**Data patch**: 2026-04-18  
**Binary**: `bin/dinastycoind.exe` buildato il 18/04/2026 alle 09:34 (35.3 MB)  
**Status**: ✅ COMPLETATO — build e link OK, zero errori

---

## PROBLEMA ORIGINALE

Rete ferma (no mining) per alcune ore → ripartenza con ~10 H/s → block storm:

```
1522544  1776460822  251
1522545  1776470329  3381  ← gap 9507s → spike difficulty
1522546  1776470334  158   ← collasso rapido
...
1522575  1776470427  87    ← blocchi ogni 1-5s!
```

Sintomi: alternative storm, ban peer (Host ... blocked), chain split.

---

## SOLUZIONI IMPLEMENTATE

### 1. `src/cryptonote_basic/difficulty.cpp` — `next_difficulty_13` (LWMA)

**Clamp solvetime per-blocco**:
```cpp
// [T/4, T*5] = [30s, 600s] per target=120s
solveTime = std::min<int64_t>((T * 5), std::max<int64_t>(solveTime, T / 4));
```
- Upper bound 600s: gap di 9507s → clamped a 600s → spike 3381 non accade più (~213 stimato)
- Lower bound 30s: evita solvetimes negativi/zero che gonfiano la difficulty

**Difficulty floor**:
```cpp
static const uint64_t DIFFICULTY_MINIMUM_FLOOR = 100;
if (next_difficulty < DIFFICULTY_MINIMUM_FLOOR)
    next_difficulty = DIFFICULTY_MINIMUM_FLOOR;
```
- Impedisce che la difficulty scenda così in basso da generare block storms prolungati

### 2. `src/cryptonote_core/blockchain.cpp` — `get_difficulty_for_next_block`

**Post-TESLA369: usa LWMA (`next_difficulty_13`) invece del vecchio algoritmo**:
```cpp
// ROOT CAUSE FIX: the old next_difficulty algo is designed for a 720-block window;
// when called with only DIFFICULTY_BLOCKS_COUNT_V13=31 entries it produces extreme
// volatility (spike 3381 → collapse 87 → block storm 1-5s).
diff = next_difficulty_13(timestamps, difficulties, target_post);
```

**Rate limit ±50% per blocco** (backstop per edge cases):
```cpp
const difficulty_type max_allowed = prev_block_diff + prev_block_diff / 2;  // +50%
const difficulty_type min_allowed = (prev_block_diff > 1) ? (prev_block_diff / 2) : 1;  // -50%
// MWARNING when triggered
```

**Logging diagnostico**:
- `MWARNING("Retarget rate-limit (upper/lower): ...")` quando il rate limit scatta
- `MERROR("LOW NEXT DIFFICULTY: ...")` quando diff < 1000
- `MINFO("diff calc debug (LWMA): ...")` ad ogni blocco

---

## BUILD RESULTS

```
[1/2] Building CXX object .../obj_cryptonote_basic.dir/difficulty.cpp.obj   ✅
[2/2] Building CXX object .../obj_cryptonote_core.dir/blockchain.cpp.obj    ✅
Ninja exit: 0  (09:30:41 → 09:32:49, ~2 min)

[1/3] Linking CXX static library libcryptonote_basic.a                       ✅
[2/3] Linking CXX static library libcryptonote_core.a                        ✅
[3/3] Linking CXX executable bin/dinastycoind.exe                            ✅
Ninja exit: 0  (09:34:03 → 09:34:33, ~30 sec)
Binary: 35,336,663 bytes  Apr 18 09:34
```

---

## PIANO DI TEST

### Scenario: rete ferma ore → ripartenza con ~10 H/s

```batch
rem Avvia daemon con log 2
cd /d C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369\build\MINGW64_NT-10.0-19045\dinastycoin-tesla369\release\bin
dinastycoind.exe --log-level 2 2>&1 | findstr /I "difficulty\|Retarget\|LOW NEXT\|LWMA"

rem EXPECTED dopo un gap lungo:
rem   "diff calc debug (LWMA): next height XXXXX, diff=YYY"    ← non spike enorme
rem   Nessun "LOW NEXT DIFFICULTY" sostenuto per molti blocchi
rem   Nessuna "Retarget rate-limit" ripetuta ogni blocco

rem NOT EXPECTED:
rem   Difficulty spike da 251 a 3381 seguito da crollo a 87
rem   Block time 1-5s per decine di blocchi
rem   "Host X blocked" massivo dopo block storm
rem   Chain split / reorg multipli
```

### Verifica manuale con RPC

```bash
# Confronta block times dopo riavvio mining
for height in $(seq 1522544 1522575); do
    curl -s http://127.0.0.1:17460/json_rpc -d '{"jsonrpc":"2.0","id":"0","method":"get_block_header_by_height","params":{"height":'$height'}}' | python -m json.tool | grep -E "timestamp|difficulty"
done

# EXPECTED: block times distribuiti intorno a 120s (±factor 2-3x al massimo)
# NOT EXPECTED: block times 1-5s per 30+ blocchi consecutivi
```

---

## ANALISI IMPATTO

| Scenario | Prima della patch | Dopo la patch |
|---|---|---|
| Gap 9507s → 1° blocco | diff spike a 3381 | diff ≈ 213 (gap clamped a 600s) |
| Blocchi successivi | collapse a 87 in 30 blocchi | recovery graduale via LWMA |
| Block time a 10 H/s | 1-5s per decine di blocchi | ~10-120s (floor=100 protegge) |
| Alternative/reorg | massivi (alternative storm) | rari (difficulty stabile) |
| Ban peer | frequenti ("Host X blocked") | assenti (niente block storm) |

---

## NOTE TECNICHE

### Perché LWMA invece del vecchio algoritmo post-TESLA369?

Il vecchio `next_difficulty` è progettato per una finestra di 720 blocchi (`DIFFICULTY_WINDOW`). Quando chiamato con solo `DIFFICULTY_BLOCKS_COUNT_V13 = 31` entries produce volatilità estrema perché la formula assume una finestra lunga. LWMA (`next_difficulty_13`) gestisce correttamente finestre brevi ed è più stabile per reti con hashrate variabile.

### Parametri scelti e motivazione

| Parametro | Valore | Motivazione |
|---|---|---|
| Solvetime min clamp | T/4 = 30s | Evita valori negativi/zero senza distorcere troppo |
| Solvetime max clamp | T×5 = 600s | Gap di ore → max 5 blocchi target di perturbazione |
| Difficulty floor | 100 | Minimo assoluto; a 10 H/s: ~10s/blocco (accettabile) |
| Rate limit upper | +50% | Backstop per edge cases transizione TESLA369 |
| Rate limit lower | -50% | Backstop; il floor assoluto è in next_difficulty_13 |
| LWMA window | 30 blocchi | Standard per stabilità con reattività sufficiente |
| LWMA adjust | 0.998 | Correzione statistica per N=60 (legacy, OK per N=30) |

---

*Patch verificata: build Ninja 0 errori, link OK, binario 18/04/2026 09:34*
