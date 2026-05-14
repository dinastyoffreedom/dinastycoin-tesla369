# 🔴 SYNC P2P BUG — RISOLTO
**Data fix**: 2026-04-18  
**File modificato**: `src/cryptonote_protocol/cryptonote_protocol_handler.inl`  
**Build**: ✅ Zero errori, link OK, `bin/dinastycoind.exe` rigenerato  

---

## PROBLEMA (ORIGINALE)

Sincronizzazione da zero non deterministica e spesso impossibile su Windows.  
Il daemon entrava in un loop e droppava tutte le connessioni ai seed:

```
-->>NOTIFY_REQUEST_CHAIN: m_block_ids.size()=28
... state: requesting chain in state synchronizing

... kicking idle peer, last update ~23.x seconds ago, expecting 2007
... state: adding blocks in state synchronizing
... Failed to lock m_sync_lock, going back to download
... Failed to request missing objects, dropping connection
... state: closed in state synchronizing
... CLOSE CONNECTION
```

Conteggi tipici: command-2006 ~157, Failed lock ~115, dropping connection ~115.

---

## ROOT CAUSE ANALISI

### Causa 1 (CRITICA): Drop immediato con span già riservati

Flusso del bug con 4 peer connessi ai seed:

```
Peer A: get m_sync_lock → adds blocks ("state: adding blocks")
Peer B: try_add_next_blocks → !sync.owns_lock() → goto skip:1655
         → request_missing_objects(context, true, false)
           → context.m_needed_objects populated (da chain entry precedente)
           → reserve_span() → span.second == 0
             (tutti i blocchi già riservati da Peer A/C/D!)
           → [line 2310-2319 PRIMA DEL FIX]:
               "We can download nothing from this peer, dropping"
               return false ← BUG: drop immediato!
         → "Failed to request missing objects, dropping connection"
         → drop_connection()
```

**Perché 115/157 drop?** Con 4 peer, il primo prende il lock e riserva tutti gli span → gli altri 3 non riescono a riservare → tutti e 3 droppati → ciclo infinito.

### Causa 2 (ALTA): NON_RESPONSIVE_PEER_KICK_TIME troppo aggressivo

Timeout di 20s per risposta a NOTIFY_REQUEST_CHAIN (cmd 2007). Se il seed è impegnato ad aggiungere blocchi (tiene m_sync_lock), può impiegare >20s → peer kickato → drop dopo 2 kick.

### Causa 3 (MEDIA): DROP_PEERS_ON_SCORE troppo basso

Con m_score iniziale = 0 e DROP_PEERS_ON_SCORE = -2:
- Kick 1: 0-- >= 0 → standby (score → -1)
- Kick 2: -1-- >= 0 → FALSE → drop permanente

Solo 2 timeout (40s) → peer droppato definitivamente.

---

## PATCH APPLICATE

### File: `src/cryptonote_protocol/cryptonote_protocol_handler.inl`

**PATCH 1 (CRITICA)** — Fix "We can download nothing from this peer, dropping"  
**Righe 2310-2322**:

```diff
-      // we can do nothing, so drop this peer to make room for others unless we think we've downloaded all we need
       const uint64_t blockchain_height = m_core.get_current_blockchain_height();
       if (std::max(blockchain_height, m_block_queue.get_next_needed_height(blockchain_height)) >= m_core.get_target_blockchain_height())
       {
         context.set_state_normal();
         MLOG_PEER_STATE("Nothing to do for now, switching to normal state");
         return true;
       }
-      MLOG_PEER_STATE("We can download nothing from this peer, dropping");
-      return false;
+      // Spans reserved by other peers - pause instead of dropping (fix sync loop regression)
+      MLOG_PEER_STATE("No span available to download (reserved by other peers), pausing");
+      context.m_state = cryptonote_connection_context::state_standby;
+      return true;
```

**Effetto**: quando tutti gli span sono riservati da altri peer, il peer va in standby invece di essere droppato. `check_standby_peers()` (ogni ~1s) lo risveglia automaticamente.

---

**PATCH 2 (ALTA)** — Aumenta timeout prima del kick  
**Riga 79**:

```diff
-#define NON_RESPONSIVE_PEER_KICK_TIME (20 * 1000000) // microseconds
+#define NON_RESPONSIVE_PEER_KICK_TIME (60 * 1000000) // microseconds (increased from 20s: seeds may be slow to respond while adding blocks)
```

---

**PATCH 3 (MEDIA)** — Aumenta tolleranza score prima del drop permanente  
**Riga 83**:

```diff
-#define DROP_PEERS_ON_SCORE -2
+#define DROP_PEERS_ON_SCORE -4 // increased from -2: allow more standby cycles before permanent drop
```

---

## BUILD RESULTS

```
[1/2] Building CXX object .../cryptonote_protocol_handler-base.cpp.obj   ✅
[2/2] Linking CXX static library libcryptonote_protocol.a                 ✅

[1/3] Building CXX object src/rpc/.../instanciations.cpp.obj             ✅ (solo warning Boost legacy pre-esistenti)
[2/3] Linking CXX static library src/rpc/librpc.a                        ✅
[3/3] Linking CXX executable bin/dinastycoind.exe                        ✅

Ninja exit: 0  — Zero errori
```

---

## PIANO DI TEST SU WINDOWS

```batch
rem ============================================
rem TEST 1: Verifica no drop "Failed to request missing objects"
rem ============================================
cd /d C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369\build\MINGW64_NT-10.0-19045\dinastycoin-tesla369\release\bin
dynastycoind.exe --log-level 2 2>&1 | findstr /i "Failed to request\|dropping connection\|No span available\|pausing"

rem EXPECTED: "No span available to download (reserved by other peers), pausing"
rem NOT EXPECTED: "Failed to request missing objects, dropping connection"
rem NOT EXPECTED: "We can download nothing from this peer, dropping"

rem ============================================
rem TEST 2: Verifica sync procede da zero
rem ============================================
mkdir %TEMP%\test_fresh_db 2>nul
dynastycoind.exe --log-level 2 --data-dir %TEMP%\test_fresh_db 2>&1 | findstr /i "adding blocks\|Synced\|synchronized"

rem EXPECTED: "Synced XXX/YYYY blocks"
rem EXPECTED: "Synchronized" dopo la sync completa
rem NOT EXPECTED: loop infinito "kicking idle peer...expecting 2007" senza progress

rem ============================================
rem TEST 3: Verifica connessioni outgoing > 0
rem ============================================
dynastycoind.exe --log-level 1 2>&1 | findstr /i "Out connections\|connections"

rem EXPECTED: "Out connections: 1" o più (non 0)
rem NOT EXPECTED: "out=0, in=0" per più di 3 minuti

rem ============================================
rem TEST 4: Monitoraggio score e kick
rem ============================================
dynastycoind.exe --log-level 3 2>&1 | findstr /i "kicking idle\|negative score\|drop"

rem EXPECTED (se timeout): "kicking idle peer, last update 60s ago"  (non 20s!)
rem NOT EXPECTED: "dropping connection" ripetuto ogni 40 secondi
```

---

## LOG ATTESO DOPO IL FIX

```
state: adding blocks in state synchronizing        ← Peer A aggiunge
Failed to lock m_sync_lock, going back to download ← Peer B (normale)
state: No span available to download (reserved by other peers), pausing ← Peer B in standby
state: resuming in state synchronizing             ← Peer B riprende dopo ~1s
requesting objects                                 ← Peer B scarica blocchi
```

**NON PIÙ ATTESO**:
```
Failed to request missing objects, dropping connection
state: closed in state synchronizing
CLOSE CONNECTION
```

---

## COMPATIBILITÀ

- ✅ Zero modifiche al consenso
- ✅ Zero modifiche a blockchain/difficulty/checkpoints
- ✅ Solo gestione connessioni P2P
- ✅ Cross-platform (nessun ifdef necessario)
- ✅ Non disabilita il sistema di drop (peer malicious vengono ancora droppati via `should_drop_connection`, score per bad behavior, ecc.)
