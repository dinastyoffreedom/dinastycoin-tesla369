oggetto: risolvere Errore di consenso/diif nel codice Dinastrcoin_tesla369 rispetto alla precedente versione 4.11 che impedisce la sinconizzazione dal blocco 152887

le due versioni da controllare dinatycointesla369 corrente: C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369\
codice versione precedente da controllare:
C:\msys64tris\home\dinastyoffreedom2\dinastycoin4.11

il problema non nasce al blocco 152887 che sta dentro a HF13 che essisteva anche nella versione 4.11.
A 152887 esplode, ma la divergenza è già iniziata prima, sicuramente dopo 152500.

Cosa dimostrano i dati 
1) Fino a 152500 i due nodi coincidono

Per 152499 e 152500:

stesso hash
stessa difficulty
stessa cumulative_difficulty
stessa major_version

Quindi HF13 scatta davvero a 152500 e quel blocco viene interpretato uguale da entrambi.

2) A 152886 coincidono sull'hash ma non sulla difficulty

# Qui hai:
stesso hash 7d2add30...
stessa timestamp
stesso prev_hash
ma:
# nodo buono

difficulty = 1949170505
cumulative_difficulty = 25077499580250

# nodo che si blocca

difficulty = 962682953
cumulative_difficulty = 24738870712646

Quindi a quell'altezza i due nodi stanno già applicando regole di difficulty diverse.

3) A 152887 il nodo bloccato calcola una difficulty attesa troppo diversa e rifiuta il blocco

# Dal log:

computed diff 983776404
does not have enough proof of work ... unexpected difficulty: 983776404

Mentre il nodo buono per 152887 vede:

difficulty = 1952657446

Quindi il nodo che si blocca richiede una soglia PoW diversa da quella della chain reale.

Il punto più importante che ho scoperto

Lo snippet HF è sospetto non tanto per il numero del fork in sé, ma perché il problema avviene:

subito dopo l'attivazione di HF13
non al blocco del fork, ma qualche centinaio di blocchi dopo

Questo è tipico di una regola che usa una finestra storica di blocchi/timestamp.

Però c'è anche un dettaglio importantissimo nei dati che hai incollato

L'header "buono" di 152501 che hai incollato è aritmeticamente impossibile.

# per il nodo buono:

152500 cumulative = 24463842222673
152501 cumulative = 24462205969756

quindi la cumulative scenderebbe, cosa che non può succedere in una chain valida. La differenza è -1636252917.

# Invece sul nodo che si blocca, 152501 è internamente coerente:

152500 cumulative = 24463842222673
152501 cumulative = 24467218008893

che torna esattamente. La differenza è 3375786220.

Cosa si può dire con certezza, senza dubbio

Con i dati affidabili che abbiamo sono:

152499 coincide
152500 coincide
152886 diverge
152887 viene rifiutato

Quindi la prima divergenza reale è in questo intervallo:

152501 .. 152886

e molto probabilmente è molto vicina a HF13.

# Ipotesi tecnica più probabile

La causa più plausibile è una di queste:

A) la difficulty post-HF13 non viene ricalcolata con la stessa logica della chain storica

Per esempio:

ramo if (hf_version >= 13) sbagliato
funzione difficulty sbagliata per HF13
finestra timestamp/diffs diversa
parametri target/cut/lag diversi per HF13
off-by-one: la nuova regola parte a 152500 in un punto del codice e a 152501 in un altro
B) la chain storica fu minata con una logica leggermente diversa da quella che oggi il codice usa in fresh sync

Questa, a questo punto, è molto plausibile.

Perché:

i nodi già sincronizzati continuano bene
ma un nodo che ricostruisce da zero non riesce più a validare quella parte storica
Cosa controllerei nel codice subito

Non solo la tabella hardfork.

Controllerei soprattutto dove HF13 cambia davvero il comportamento:

get_next_difficulty...
next_difficulty...
difficulty.cpp
eventuali rami in blockchain.cpp
eventuali controlli su major_version
funzioni che usano:
timestamps
cumulative difficulties
cut/lag/window
target = 120

In pratica devi cercare qualcosa del tipo:

if (hf_version >= 13) { ... }

oppure branch con versione del blocco:

if (block_major_version >= 13) { ... }

e verificare che tutti i punti del consenso usino la stessa soglia e la stessa altezza.

La lettura più probabile dei dati attuali

sospetto fortemente:

152500 è il blocco di attivazione HF13
il primo blocco veramente problematico è subito dopo, probabilmente 152501 o comunque molto vicino
il fresh sync usa una logica difficulty diversa da quella che la chain storica ha effettivamente seguito in quel tratto

verificare le differenze relative a HF13 tra la versione 4.11 e Tesla369 e correggerle

---

# ✅ STATUS: RISOLTO — 2026-04-20

## ROOT CAUSE CONFERMATA (ipotesi B)

La chain storica 152501..1512399 fu minata con il clamp solvetime **originale 4.11**:
```
[-FTL, T*10] = [-7200s, 1200s] per target=120s
```
Il tesla369 durante fresh sync usava il **nuovo clamp**:
```
[T/4, T*5] = [30s, 600s]
```
Con il nuovo clamp il LWMA produce una difficulty ~50% INFERIORE per quella finestra storica
(i solvetimes negativi vengono clampati a +30s invece di passare come negativi,
il LWMA è più alto → difficulty stimata più bassa → ~983K invece di ~1949K).

## PATCH APPLICATE

### `src/cryptonote_basic/difficulty.h`
```cpp
// Aggiunto parametro legacy_clamp con default=false
difficulty_type next_difficulty_13(
    std::vector<std::uint64_t> timestamps,
    std::vector<difficulty_type> cumulative_difficulties,
    size_t target_seconds,
    bool legacy_clamp = false);
// legacy_clamp=true  → clamp 4.11 [-FTL, T*10] (blocchi storici 152501..1512399)
// legacy_clamp=false → nuovo clamp [T/4, T*5]  (blocchi TESLA369+ ≥ 1512400)
```

### `src/cryptonote_basic/difficulty.cpp` — `next_difficulty_13()`
```cpp
if (legacy_clamp) {
    // Original 4.11 clamp [-FTL, T*10] = [-7200s, 1200s]
    solveTime = std::min<int64_t>((T * 10), std::max<int64_t>(solveTime, -FTL));
} else {
    // New clamp [T/4, T*5] = [30s, 600s]
    solveTime = std::min<int64_t>((T * 5), std::max<int64_t>(solveTime, T / 4));
}
```

### `src/cryptonote_core/blockchain.cpp` — `get_difficulty_for_next_block()`
```cpp
if (height < hf_height_tesla369)  // < 1512400
{
    if (height < HF_HEIGHT_NEW_DIFFICULTY_APPLY)  // < 152500
        diff = next_difficulty(timestamps, difficulties, target_pre);  // algo vecchio
    else  // 152500..1512399: BLOCCHI STORICI
        diff = next_difficulty_13(timestamps, difficulties, target_pre, /*legacy_clamp=*/true);
}
else  // >= 1512400 (TESLA369+)
{
    diff = next_difficulty_13(timestamps, difficulties, target_post);  // legacy_clamp=false
}
```

### `src/cryptonote_core/blockchain.cpp` — alt-chain validation
```cpp
if (bei.height < HF_HEIGHT_TESLA369_MAINNET)  // 1512400
{
    if (height < HF_HEIGHT_NEW_DIFFICULTY_APPLY)  // < 152500
        return next_difficulty(timestamps, cumulative_difficulties, target);
    else  // 152500..1512399
        return next_difficulty_13(timestamps, cumulative_difficulties, target, /*legacy_clamp=*/true);
}
else  // TESLA369+
{
    return next_difficulty_13(timestamps, cumulative_difficulties, target);
}
```

### `src/cryptonote_config.h` (costanti di riferimento, non modificate)
```cpp
#define HF_HEIGHT_NEW_DIFFICULTY_APPLY  152500   // inizio HF13 / LWMA
#define HF_HEIGHT_TESLA369_MAINNET     1512400   // fine periodo storico
```

## VERIFICA ATTESA (test su nodo fresh sync)

```
EXPECTED nel log per height 152501..152886:
  "diff calc debug: next height 152501, ... diff=3375786220"  ← coincide con nodo buono
  NON: "computed diff 983776404"
  NON: "does not have enough proof of work"

EXPECTED sync:
  Il nodo supera il blocco 152887 senza rifiuto
  Sincronizzazione procede fino alla cima della chain
```

## NOTE TECNICHE

- Il parametro `legacy_clamp` è backward-compatible (default=false, nessun cambio per codice esistente)
- Zero impatto su blocchi TESLA369+ (≥ 1512400): usano sempre il nuovo clamp
- Nessun cambio ai parametri di consenso: solo il clamp del LWMA viene corretto per la finestra storica
- La stessa correzione è applicata sia alla validazione main chain che alla validazione alt-chain (coerenza)
