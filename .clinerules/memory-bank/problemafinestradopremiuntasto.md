Ho due repo locali:
Repo A (baseline OK): Dinastycoin 4.11 (Windows funziona: il daemon lavora e logga normalmente senza input da tastiera)
Repo B (regressione): Dinastycoin tesla369 (Windows: il daemon sembra “fermarsi” e riprende solo quando premo un tasto nella finestra DOS; inoltre set_log 1 e set_log 2 producono pochissimo output, set_log 3 qualcosa ma comunque non “spam” come dovrebbe; spesso resta a 0(out)+0(in) e sembra non fare quasi nulla)
Obiettivo: fare una regression analysis tra A e B per capire cosa è cambiato su Windows relativamente a:
loop principale del daemon / thread di networking
console handler / input handling (stdin, ReadConsole, WaitForSingleObject, ecc.)
logging / flushing / buffering (epee log, el::Logger, console appender)
eventuali cambiamenti in run() / deinit() / core_rpc_server / p2p che possano bloccare l’event loop
Richiesta operativa:
Identifica nel codice dove il daemon su Windows potrebbe essere bloccato in attesa di input o di un evento console (anche indirettamente).
Confronta i punti chiave tra 4.11 e tesla369: file e funzioni (diff ragionato, non solo “git diff” enorme).
Cerca pattern tipici: std::getline, ReadConsoleInput, WaitFor..., GetNumberOfConsoleInputEvents, boost::asio io_context run/stop, sleep, condition_variable, signal_handler, console_handler, ecc.
Verifica se il logging su Windows usa buffering che flush-a solo su input/nuova riga/evento; trova dove viene fatto flush.
Output: elenco dei 3–5 cambiamenti più sospetti (con file+funzione+spiegazione), e una patch proposta minima per riportare il comportamento 4.11 (daemon continua a lavorare e loggare senza premere tasti).
Vincoli:
Non cambiare consenso.
Non “disabilitare” console interattiva; al massimo rendere non bloccante o spostarla su thread separato.
Preferire fix che impattano solo Windows (#ifdef _WIN32).
Suggerimento di ricerca:
Cerca nei due repo le funzioni che gestiscono comandi console (set_log, status, ecc.) e confronta come viene letto l’input.
Cerca dove viene avviato il loop principale del daemon e come viene gestito su Windows.
Inizia producendo una mappa: “entrypoint daemon → init logging → init p2p → start threads → console loop”.