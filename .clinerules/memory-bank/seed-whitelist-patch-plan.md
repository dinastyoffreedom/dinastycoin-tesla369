# 🔒 SEED WHITELIST PATCH — Analisi e Piano
**Data**: 2026-04-17  
**File coinvolto**: `src/p2p/net_node.inl`  
**Problema**: I seed ufficiali (3.8.236.183, 35.176.51.4, 3.9.137.162) vengono bloccati → daemon resta a 0(out)+0(in)

---

## 🔴 DIAGNOSI: Perché i seed vengono bloccati

### Flusso esatto del ban (tracciato nel codice)

```
do_handshake_with_peer()                         [net_node.inl:1149]
  → async_invoke_remote_command2(COMMAND_HANDSHAKE) al seed
  → rsp ricevuta: peerlist del seed
  → handle_remote_peerlist(rsp.local_peerlist_new, context)   [linea 1181]
    → restituisce FALSE (peerlist contiene voci invalide)
  → LOG_WARNING "COMMAND_HANDSHAKE: failed to handle_remote_peerlist(...), closing connection."
  → add_host_fail(context.m_remote_address)                   [linea 1184]  ← ACCUMULATO

add_host_fail()                                  [net_node.inl:406-423]
  → m_host_fails_score[ip] += score (default=1)
  → MDEBUG "Host X fail score=N"
  → if N > P2P_IP_FAILS_BEFORE_BLOCK:
      it->second = P2P_IP_FAILS_BEFORE_BLOCK/2  (reset parziale)
      block_host(address)                        ← BLOCCO DEFINITIVO

block_host()                                     [net_node.inl:248-328]
  → m_blocked_hosts[host_str] = time_limit
  → evict_host_from_peerlist() / remove_from_peer_white() / remove_from_peer_gray()
  → remove_from_peer_anchor(addr)               ← rimosso anche dall'anchor!
  → chiude TUTTE le connessioni attive al seed
  → MCLOG_CYAN "Host 3.8.236.183 blocked."     ← log visibile nel problema
```

### Perché `handle_remote_peerlist` fallisce per i seed?

Il seed restituisce nella sua peerlist voci che possono fallire la validazione locale per:
1. **Voci duplicate** con lo stesso peer_id/address
2. **Eccezioni durante il merge** della peerlist nel nostro nodo locale
3. **Timeout** del handshake → `add_host_fail` ripetuto ad ogni retry

---

## 📋 CALL-SITE COMPLETE di `add_host_fail` / `block_host`

| Linea | Funzione | Causa | Frequenza |
|---|---|---|---|
| **1184** | `do_handshake_with_peer` | `handle_remote_peerlist` fallisce in COMMAND_HANDSHAKE | ← **PRINCIPALE per i seed** |
| **1265** | `handle_timed_sync` | `handle_remote_peerlist` fallisce in COMMAND_TIMED_SYNC | ogni ~60s |
| **2619** | `handle_handshake` | wrong network_id (incoming) | raro |
| **2627** | `handle_handshake` | handshake non da incoming | raro |
| **420** | `add_host_fail` | trigger finale dopo N fallimenti → `block_host` | automatico |
| **571** | init | lista blocklist statica `--ban-list` file | all'avvio |
| **2164** | `update_dns_blocklist` | DNS blocklist update | ogni N ore |

### Funzioni di blocco

```cpp
// block_host() firma (net_node.h):
virtual bool block_host(epee::net_utils::network_address address,
                         time_t seconds = P2P_IP_BLOCKTIME,
                         bool add_only = false);

// block_subnet() firma (net_node.h):
virtual bool block_subnet(const epee::net_utils::ipv4_network_subnet &subnet,
                           time_t seconds = P2P_IP_BLOCKTIME);

// add_host_fail() firma (net_node.h):
virtual bool add_host_fail(const epee::net_utils::network_address &address,
                             unsigned int score = 1);
```

---

## 🔧 PATCH PROPOSTA: Seed Whitelist

**Strategia**: funzione helper `is_seed_whitelisted(host_str)` + guard nelle 3 funzioni critiche.  
**Vincoli rispettati**:
- ✅ Non tocca consenso
- ✅ Non disabilita il sistema di ban globale
- ✅ Solo eccezione per seed ufficiali
- ✅ Log "Seed whitelisted, skipping block: host=..." come richiesto

---

### MODIFICA 1 — Helper function `is_seed_whitelisted`

**Posizione**: all'inizio di `src/p2p/net_node.inl`, dopo le `#include` e prima della prima funzione template.

```cpp
// =====================================================================
// Seed whitelist: i seed ufficiali Dinastycoin non vengono mai bannati.
// Questo protegge da block_host() e add_host_fail() involontari.
// =====================================================================
namespace {
  bool is_seed_whitelisted(const std::string& host_str) {
    static const std::set<std::string> SEED_WHITELIST = {
      // Hostnames ufficiali
      "seed1.dinastycoin.com",
      "seed2.dinastycoin.com",
      "seed3.dinastycoin.com",
      "seed4.dinastycoin.com",
      // IP noti dei seed (seed1=3.8.236.183, seed2=35.176.51.4, seed3=3.9.137.162)
      "3.8.236.183",
      "35.176.51.4",
      "3.9.137.162"
    };
    return SEED_WHITELIST.count(host_str) > 0;
  }
} // namespace anonymous
```

---

### MODIFICA 2 — Guard in `block_host()` (linea 248)

**File**: `src/p2p/net_node.inl`  
**Posizione**: dopo `if(!addr.is_blockable()) return false;`, PRIMA di qualsiasi logica di blocco.

```diff
  bool node_server<t_payload_net_handler>::block_host(epee::net_utils::network_address addr,
      time_t seconds, bool add_only)
  {
    if(!addr.is_blockable())
      return false;

+   // Seed whitelist: non bloccare mai i seed ufficiali Dinastycoin
+   const std::string wl_check = addr.host_str();
+   if (is_seed_whitelisted(wl_check)) {
+     MINFO("Seed whitelisted, skipping block: host=" << wl_check);
+     return false;
+   }
+
    const time_t now = time(nullptr);
    bool added = false;
    ...
```

---

### MODIFICA 3 — Guard in `add_host_fail()` (linea 406)

**File**: `src/p2p/net_node.inl`  
**Posizione**: dopo `if(!address.is_blockable()) return false;`, PRIMA di accumulare il punteggio.

```diff
  bool node_server<t_payload_net_handler>::add_host_fail(const epee::net_utils::network_address &address,
      unsigned int score)
  {
    if(!address.is_blockable())
      return false;

+   // Seed whitelist: non accumulare fail score per i seed ufficiali
+   if (is_seed_whitelisted(address.host_str())) {
+     MINFO("Seed whitelisted, skipping fail count: host=" << address.host_str());
+     return false;
+   }
+
    CRITICAL_REGION_LOCAL(m_host_fails_score_lock);
    uint64_t fails = m_host_fails_score[address.host_str()] += score;
    ...
```

---

### MODIFICA 4 — Guard in `block_subnet()` (linea 343)

**File**: `src/p2p/net_node.inl`  
**Posizione**: all'inizio della funzione, PRIMA della logica di blocco.  
**Scopo**: impedire che una subnet che copre un IP seed venga bloccata.

```diff
  bool node_server<t_payload_net_handler>::block_subnet(const epee::net_utils::ipv4_network_subnet &subnet,
      time_t seconds)
  {
+   // Seed whitelist: non bloccare subnet che coprono IP dei seed ufficiali
+   static const std::vector<std::string> seed_ips_wl = {
+     "3.8.236.183", "35.176.51.4", "3.9.137.162"
+   };
+   for (const auto &ip_str : seed_ips_wl) {
+     uint32_t ip = 0;
+     if (epee::string_tools::get_ip_int32_from_string(ip, ip_str)) {
+       epee::net_utils::ipv4_network_address seed_addr{ip, 0};
+       if (subnet.matches(seed_addr)) {
+         MWARNING("Seed whitelisted, skipping subnet block: "
+                  << subnet.host_str() << " covers seed IP " << ip_str);
+         return false;
+       }
+     }
+   }
+
    const time_t now = time(nullptr);
    ...
```

---

## 📊 RIEPILOGO MODIFICHE

| # | Funzione | Linea | Modifica |
|---|---|---|---|
| 1 | `is_seed_whitelisted` (new) | ~50 (top file) | Helper function con set statico di IP+hostnames whitelisted |
| 2 | `block_host()` | 248-252 | Guard: skip se `is_seed_whitelisted(addr.host_str())` |
| 3 | `add_host_fail()` | 406-410 | Guard: skip se `is_seed_whitelisted(address.host_str())` |
| 4 | `block_subnet()` | 343-348 | Guard: skip se la subnet copre un IP seed |

**File da modificare**: solo `src/p2p/net_node.inl`  
**Rischio**: 🟢 basso — nessun tocco a consenso, sincronizzazione, o peerlist normale

---

## 🧪 PIANO DI TEST SU WINDOWS

```batch
rem ============================================
rem TEST 1: Verifica che i seed NON vengano bloccati
rem ============================================
cd /d C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369\build\release\bin
dinastycoind.exe --log-level 2 2>&1 | findstr /i "blocked\|whitelisted\|fail score"

rem EXPECTED (se tentato blocco): "Seed whitelisted, skipping block: host=3.8.236.183"
rem EXPECTED (se tentato fail):   "Seed whitelisted, skipping fail count: host=..."
rem NOT EXPECTED: "Host 3.8.236.183 blocked."
rem NOT EXPECTED: "Host 35.176.51.4 blocked."
rem NOT EXPECTED: "Host 3.9.137.162 blocked."

rem ============================================
rem TEST 2: Verifica connessioni outgoing > 0 dopo 120 secondi
rem ============================================
:: Avviare il daemon e osservare i log
dynastycoind.exe --log-level 1 ^
    2>&1 | findstr /i "Out connections:\|connections\|peers"

rem EXPECTED: "Out connections: 1" o "Connections: out=1, in=0" (o più)
rem NOT EXPECTED: restare a "out=0, in=0" per più di 3 minuti

rem ============================================
rem TEST 3: Verifica sync del wallet
rem ============================================
:: Avviare wallet e controllare che la blockchain height incrementi
:: EXPECTED: "Synchronized with blockchain" dopo qualche minuto
:: EXPECTED: height incrementa nel wallet

rem ============================================
rem TEST 4: Stress test ban system (controllo integrità)
rem ============================================
:: Verificare che il ban system funzioni ancora per peer normali (non seed)
:: Tentare connessione con un peer con wrong network_id
:: EXPECTED: peer non-seed viene ancora bannato normalmente
:: EXPECTED: i seed NON vengono bannati mai
```

---

## NOTES TECNICHE

### Perché `address.host_str()` restituisce l'IP per i seed?

Quando il nodo si connette ai seed via `full_addrs.insert("seed1.dinastycoin.com:37175")`, la risoluzione DNS avviene prima della connessione. Una volta connessi, `context.m_remote_address` contiene l'IP risolto (es. `3.8.236.183`), NON il hostname. Quindi:
- In `block_host()`: `addr.host_str()` = "3.8.236.183"
- In `add_host_fail()`: `address.host_str()` = "3.8.236.183"

Tuttavia, includiamo anche i hostname nella whitelist come safeguard per casi dove l'address non è ancora risolto.

### Inclusione necessaria per `block_subnet()`

La funzione `block_subnet()` usa `epee::string_tools::get_ip_int32_from_string` che è già disponibile nel contesto. Non serve nessun nuovo include.

---

*Documento pronto per revisione. Applicare patch in Act Mode.*
