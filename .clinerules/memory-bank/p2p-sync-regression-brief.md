Sto facendo una regression analysis urgente tra due codebase Dinastycoin:

Repo A (stabile): C:\msys64tris\home\dinastyoffreedom2\dinastycoin4.11  
Repo B (problematico): C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369

# Obiettivo: ripristinare in Repo B il comportamento stabile di Repo A su Windows (daemon + GUI), senza rompere Linux.
Sintomi osservati in Repo B:
Errore consenso causato da checkpoint errato: in checkpoints.cpp un ADD_CHECKPOINT2(height, hash, cumulative_difficulty) aveva cumulative difficulty NON monotona (es. a height 1522077 era più bassa della precedente), causando rifiuti tipo “does not have enough proof of work” e fork/stallo.
Voglio: aggiungere protezioni nel codice o tooling per impedire checkpoint non monotoni + individuare dove Repo B genera/usa cumulative difficulty nei checkpoint.
Su Windows il daemon spesso resta a 0(out)+0(in) se avviato da GUI/bootstrapping; da CLI si connette.
Voglio: capire differenza tra avvio GUI e CLI (flag passati, default, offline/proxy, bind ip/port, out-peers).
Avvio headless su Windows: errore bind porta 37175 già in uso (ok, gestibile), ma poi altro errore: Network address not supported quando usa DNS seed/fallback.
Voglio: trovare nel codice dove vengono parse/gestite le network addresses (IPv4/IPv6/other) e perché su Windows lancia eccezione. Confronta con Repo A.
Anche forzando --add-exclusive-node IPv4, il sync entra in loop:
get_next_needed_pruning_stripe: want height 1 ... bc_height 1
We do not have the stripe required to download another block, pausing
no next span found, going back to download
kicking idle peer ... expecting 2007
Failed to lock m_sync_lock, going back to download Questo sembra un bug in pruning/stripe selection o handshake pruning, soprattutto su Windows.
Voglio: identificare la differenza tra Repo A e Repo B in:
cryptonote_protocol_handler.inl (sync state machine, m_sync_lock, span selection)
net_node.inl (p2p loop, timeouts, idle kick, levin commands 2006/2007)
gestione pruning seed/stripe e condizioni “we do not have the stripe required…”
Proponi una patch minima per far ripartire il sync su Windows come in Repo A.
Cosa devi produrre:
A) Una lista dei file e funzioni coinvolte con confronto A vs B (diff ragionato, non solo elenco).
B) 2-3 ipotesi tecniche con evidenze (righe di codice) per ciascun sintomo.
C) Una patch proposta (o più patch alternative) con priorità: “fix minimo per ripristinare 4.11 behavior”.
D) Un piano di test riproducibile su Windows (comandi di avvio, flag, expected logs).
Vincoli:
Non cambiare parametri di consenso a caso: se tocchi difficulty/pruning deve essere compatibile con la chain esistente.
Evita soluzioni “disabilita tutto”: voglio un fix vero, non un workaround permanente.
Se trovi che il bug è introdotto da un commit specifico, indicalo.
Inizia leggendo i log pattern sopra e cercando nel codice i messaggi esatti:
“Network address not supported”
“We do not have the stripe required to download another block, pausing”
“get_next_needed_pruning_stripe”
“kicking idle peer … expecting 2007”
“Failed to lock m_sync_lock”
“Difficulty drift detected”
“does not have enough proof of work” Poi fai il confronto tra Repo A e Repo B nelle aree corrispondenti.
Output richiesto: prima una “Executive summary” (10 righe), poi dettagli tecnici e patch.