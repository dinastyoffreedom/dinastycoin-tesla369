Repo: C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369
Contesto: abbiamo già applicato patch su:
src/cryptonote_core/blockchain.cpp (recalculate_difficulties sempre)
src/checkpoints/checkpoints.cpp (cum_diff svuotate con "", DNS seeds mainnet disabilitati dns_urls = {})
src/cryptonote_protocol/cryptonote_protocol_handler.inl (idle peer negative score => drop_connection)
Problema attuale (Windows + GUI): nel log compare:
Host 3.8.236.183 blocked.
Host 35.176.51.4 blocked.
Host 3.9.137.162 blocked. Questi sono i seed ufficiali (seed1/seed2/seed3). Quando vengono bloccati, il daemon resta a 0(out)+0(in) e non sincronizza.
Ho individuato in src/p2p/net_node.inl il codice che aggiunge a m_blocked_hosts e stampa:
MCLOG_CYAN(..., "Host " << host_str << " blocked.");
Richiesta:
Trova la funzione esatta che esegue il block (firma, file, e tutte le call-site).
Identifica perché viene chiamata per i seed (reason: misbehavior, timeout, handshake fail, score, etc.), includendo le righe di log che dovrebbero precedere “Host X blocked”.
Implementa una patch minima per garantire che i seed ufficiali NON vengano mai bloccati:
whitelist per hostname: seed1.dinastycoin.com, seed2.dinastycoin.com, seed3.dinastycoin.com, seed4.dinastycoin.com
whitelist per IP: 3.8.236.183, 35.176.51.4, 3.9.137.162, (aggiungi anche seed4 se presente nel codice)
la whitelist deve applicarsi sia a block host che block subnet, e deve evitare anche l’eviction dalla peerlist/anchor.
Output: diff patch pronta da applicare + spiegazione breve + piano test su Windows (log atteso: niente “Host X blocked” per i seed, connessioni >0, sync procede).
Vincoli:
Non cambiare consenso.
Non disabilitare il sistema di ban in generale: solo eccezione per seed ufficiali.
Se possibile, logga un messaggio tipo “Seed whitelisted, skipping block: host=...”.
Inizia cercando nel codice la stringa esatta: "Host " << host_str << " blocked." e risali alla funzione e ai call-site.