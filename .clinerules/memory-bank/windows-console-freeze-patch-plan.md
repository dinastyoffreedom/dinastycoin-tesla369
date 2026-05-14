# 🖥️ WINDOWS CONSOLE FREEZE PATCH PLAN
**Data**: 2026-04-17  
**Repo**: C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369  
**Problema**: Su Windows (GUI/CMD), il daemon si "ferma" e riprende solo premendo un tasto. `set_log 1` e `set_log 2` producono pochissimo output. Solo `set_log 3` produce output ma non abbastanza.  
**Confronto**: Repo A (4.11) funziona correttamente. Repo B (tesla369) è affetto.

---

## 📌 EXECUTIVE SUMMARY

Il problema è causato da **tre root cause distinte**, tutte legate alla gestione della console Windows:

1. **Quick Edit Mode non disabilitato** (`main.cpp`) — causa principale: Windows pausa l'intero processo quando l'utente clicca nella finestra CMD
2. **`WaitForSingleObject` + `std::getline` bloccano su eventi mouse** (`console_handler.h`) — secondaria: la funzione `wait_stdin_data()` tratta eventi mouse/resize come input da tastiera
3. **Log buffering a basso log level** — conseguenza delle cause precedenti

**Fix totale**: 2 file modificati, ~25 righe di codice, solo `#ifdef WIN32`.

---

## 🗺️ MAPPA: Entrypoint Daemon → Console Loop

```
main()                              [src/daemon/main.cpp:126]
  → tools::on_startup()            [common/util.cpp — NON disabilita Quick Edit!]
  → daemonizer::daemonize()
    → t_executor::run_interactive()
      → t_daemon::run(interactive=true)   [src/daemon/daemon.cpp:190]
        ├── core.run()
        ├── rpc.run()               [thread separato]
        ├── rpc_commands.start_handling()   [daemon/daemon.cpp:223]
        │     └── console_handlers_binder::start_handling()
        │           └── m_console_thread (boost::thread separato)
        │                 └── async_console_handler::run()  [console_handler.h:363]
        │                       └── LOOP: m_stdin_reader.get_line()
        │                             ↓
        │                       async_stdin_reader::reader_thread_func()
        │                             └── wait_stdin_data()
        │                                   └── WaitForSingleObject(STD_INPUT_HANDLE, 100)
        │                                   └── std::getline()  ← BLOCCA su eventi mouse!
        └── p2p.run()               [BLOCCA il main thread]
```

---

## 🔴 ROOT CAUSE #1 — Windows Quick Edit Mode (CAUSA PRINCIPALE)

### Cos'è Quick Edit Mode

Su Windows, la finestra CMD ha **Quick Edit Mode** abilitato per default. Quando l'utente:
- Clicca nella finestra CMD (anche accidentalmente)
- Seleziona del testo con il mouse
- La finestra riceve il focus dalla GUI

Windows **mette in pausa TUTTI I WRITE A STDOUT/STDERR** del processo intero. Non solo un thread: l'INTERO PROCESSO.

### Sintomo

- Log visibili → l'utente clicca nella finestra → log si bloccano
- Il P2P continua a girare (non crasha), ma ogni thread che tenta di loggare si blocca in `WriteConsole()`
- L'utente preme un tasto → il write si sblocca → il daemon "riprende" e scarica tutti i log accumulati

### Evidenza nel codice

`src/daemon/main.cpp` (riga 131):
```cpp
tools::on_startup();
// ← QUI manca la disabilitazione di ENABLE_QUICK_EDIT_MODE!
epee::string_tools::set_module_name_and_folder(argv[0]);
```

I soli usi di `SetConsoleMode` nel codebase:
- `src/common/password.cpp` — solo per nascondere input password
- `src/common/util.cpp` → `input_line_win()` — solo per una singola lettura, NON all'avvio

### Perché 4.11 non aveva questo problema

Molto probabilmente perché:
1. 4.11 veniva lanciato da MSYS2/mintty (che non usa la CMD nativa e non ha Quick Edit Mode)
2. OPPURE la GUI di 4.11 aveva un parametro di avvio diverso
3. OPPURE 4.11 aveva il fix di `ENABLE_QUICK_EDIT_MODE` in `on_startup()` che è stato rimosso in tesla369

---

## 🟠 ROOT CAUSE #2 — `WaitForSingleObject` + `std::getline` bloccano su eventi mouse

### File: `contrib/epee/include/console_handler.h`, funzione `wait_stdin_data()`, righe 193-210

```cpp
// CODICE ATTUALE — PROBLEMATICO su Windows:
#else
      while (m_run.load(std::memory_order_relaxed))
      {
        if (m_read_status == state_cancelled)
          return false;

        DWORD retval = ::WaitForSingleObject(::GetStdHandle(STD_INPUT_HANDLE), 100);
        switch (retval)
        {
          case WAIT_FAILED:
            return false;
          case WAIT_OBJECT_0:
            return true;   // ← Ritorna true per QUALSIASI evento console!
                           //   inclusi: MOUSE_EVENT, WINDOW_BUFFER_SIZE_EVENT, FOCUS_EVENT
          default:
            break;
        }
      }
#endif
```

### Il problema

`WaitForSingleObject` su `STD_INPUT_HANDLE` ritorna `WAIT_OBJECT_0` per **qualsiasi** record nella coda input console:

| Tipo evento | Causa |
|---|---|
| `KEY_EVENT` | Tastiera → OK, è quello che vogliamo |
| `MOUSE_EVENT` | Click/movimento mouse con Quick Edit → PROBLEMA |
| `WINDOW_BUFFER_SIZE_EVENT` | Ridimensionamento finestra → PROBLEMA |
| `FOCUS_EVENT` | Focus change → PROBLEMA |
| `MENU_EVENT` | Menu di sistema → PROBLEMA |

**Flusso del bug**:
1. L'utente clicca nella finestra → `MOUSE_EVENT` nella coda input
2. `WaitForSingleObject` ritorna `WAIT_OBJECT_0`
3. `wait_stdin_data()` ritorna `true`
4. `reader_thread_func()` chiama `std::getline(std::cin, line)` (riga 240)
5. `std::getline` si blocca perché non c'è input da tastiera (solo eventi mouse)
6. Il reader thread è bloccato → `m_response_cv` non viene notificata
7. `get_line()` nel console thread aspetta `m_response_cv.wait(lock)` → BLOCCATO
8. Comandi `set_log N` dalla console interattiva non funzionano

---

## 🟡 ROOT CAUSE #3 — Log buffering a basso log level

**Spiegazione**: A `set_log 1`/`set_log 2`:
- Ci sono pochi messaggi di log (uno ogni ~5-30 secondi)
- Se il processo è pausato da Quick Edit Mode, i pochi messaggi vengono bufferizzati
- Il buffer si svuota solo quando il processo riprende (tasto premuto)
- **Risultato**: nessun output visibile per minuti

A `set_log 3`:
- Messaggi così frequenti (~10/s) che il buffer si riempie e si svuota continuamente
- I log appaiono anche se leggermente ritardati

---

## 🔧 PATCH PROPOSTE

### 🔴 PATCH A — Disabilita Quick Edit Mode (CRITICA)

**File**: `src/daemon/main.cpp`  
**Posizione**: Dopo `tools::on_startup()` (riga 131), PRIMA di qualsiasi altra cosa  
**Impatto**: Risolve il freeze principale del daemon su Windows

```diff
  tools::on_startup();

+ #ifdef WIN32
+   // Disable Quick Edit Mode: prevents Windows from pausing the ENTIRE PROCESS
+   // when the user clicks in the console window (common issue when launched from GUI).
+   // Without this, any click in the CMD window blocks all log output and p2p threads.
+   {
+     HANDLE hStdinQE = GetStdHandle(STD_INPUT_HANDLE);
+     if (hStdinQE != INVALID_HANDLE_VALUE) {
+       DWORD modeQE = 0;
+       if (GetConsoleMode(hStdinQE, &modeQE)) {
+         modeQE &= ~ENABLE_QUICK_EDIT_MODE;
+         modeQE |= ENABLE_EXTENDED_FLAGS; // necessario per applicare la maschera
+         SetConsoleMode(hStdinQE, modeQE);
+       }
+     }
+   }
+ #endif

  epee::string_tools::set_module_name_and_folder(argv[0]);
```

---

### 🟠 PATCH B — Filtra eventi non-tastiera in `wait_stdin_data()` (ALTA)

**File**: `contrib/epee/include/console_handler.h`  
**Posizione**: Blocco `#else` di `wait_stdin_data()` (righe 193-210)  
**Impatto**: Risolve il blocco di `std::getline` su eventi mouse/focus

```diff
  #else
-       while (m_run.load(std::memory_order_relaxed))
-       {
-         if (m_read_status == state_cancelled)
-           return false;
-
-         DWORD retval = ::WaitForSingleObject(::GetStdHandle(STD_INPUT_HANDLE), 100);
-         switch (retval)
-         {
-           case WAIT_FAILED:
-             return false;
-           case WAIT_OBJECT_0:
-             return true;
-           default:
-             break;
-         }
-       }
+       while (m_run.load(std::memory_order_relaxed))
+       {
+         if (m_read_status == state_cancelled)
+           return false;
+
+         HANDLE hConIn = ::GetStdHandle(STD_INPUT_HANDLE);
+         DWORD retval = ::WaitForSingleObject(hConIn, 100);
+         switch (retval)
+         {
+           case WAIT_FAILED:
+             return false;
+           case WAIT_OBJECT_0:
+           {
+             // Filter out non-keyboard events (mouse clicks, resize, focus changes)
+             // WaitForSingleObject returns WAIT_OBJECT_0 for ANY console event,
+             // but std::getline only reads keyboard input. Without filtering,
+             // getline blocks when mouse events are in the queue.
+             DWORD nEvents = 0;
+             if (!::GetNumberOfConsoleInputEvents(hConIn, &nEvents) || nEvents == 0)
+               break;
+             INPUT_RECORD irBuf[64];
+             DWORD nRead = 0;
+             if (::PeekConsoleInputA(hConIn, irBuf, (nEvents < 64 ? nEvents : 64), &nRead))
+             {
+               bool hasKey = false;
+               for (DWORD i = 0; i < nRead; i++)
+               {
+                 if (irBuf[i].EventType == KEY_EVENT
+                     && irBuf[i].Event.KeyEvent.bKeyDown
+                     && irBuf[i].Event.KeyEvent.wVirtualKeyCode != 0)
+                 {
+                   hasKey = true;
+                   break;
+                 }
+               }
+               if (!hasKey)
+               {
+                 // Scarta tutti gli eventi non-tastiera
+                 ::FlushConsoleInputBuffer(hConIn);
+                 break; // Torna al while loop (100ms timeout)
+               }
+             }
+             return true;
+           }
+           default:
+             break;
+         }
+       }
  #endif
```

---

## 📊 RIEPILOGO MODIFICHE

| # | File | Modifica | Righe | Priorità |
|---|---|---|---|---|
| A | `src/daemon/main.cpp` | Disabilita `ENABLE_QUICK_EDIT_MODE` all'avvio | +13 | 🔴 CRITICA |
| B | `contrib/epee/include/console_handler.h` | Filtra eventi non-tastiera in `wait_stdin_data()` | +25 | 🟠 ALTA |

**Impatto consenso**: ✅ zero — solo I/O console Windows  
**Compatibilità Linux/macOS**: ✅ tutto sotto `#ifdef WIN32` / `#if defined(WIN32)`  
**Compatibilità daemon non-interattivo**: ✅ `GetConsoleMode` fallisce silenziosamente se non c'è console

---

## 🧪 PIANO DI TEST SU WINDOWS

```batch
rem ============================================
rem TEST 1: Verifica che il daemon non si ferma su click
rem ============================================
cd /d C:\msys64tris\home\dinastyoffreedom2\dinastycoin-tesla369\build\release\bin
start /wait cmd /c "dinastycoind.exe --log-level 2 2>&1"

rem PROCEDURA:
rem 1. Avviare il daemon
rem 2. Attendere 10 secondi per vedere log in output
rem 3. CLICCARE nella finestra CMD
rem 4. Attendere 10 secondi
rem EXPECTED CON PATCH: i log continuano ad apparire anche dopo il click
rem NOT EXPECTED: log si bloccano, daemon si ferma

rem ============================================
rem TEST 2: Verifica output log livelli 1 e 2
rem ============================================
dinastycoind.exe --log-level 1 2>&1 | findstr /i "p2p\|connecting\|sync\|peer"

rem EXPECTED CON PATCH: messaggi compaiono ogni ~5-10 secondi
rem NOT EXPECTED: nessun output per minuti

rem ============================================
rem TEST 3: Verifica comando set_log dalla console
rem ============================================
:: Avviare daemon in modalità interattiva
:: Digitare: set_log 1
:: EXPECTED: "Log level set to 1" e output log visibili
:: NOT EXPECTED: freeze dopo aver digitato il comando

rem ============================================
rem TEST 4: Avvio da GUI (senza Quick Edit Mode)
rem ============================================
:: Avviare il daemon tramite la GUI di Dinastycoin
:: EXPECTED: log visibili nella finestra CMD associata
:: EXPECTED: connessioni outgoing > 0 dopo 60 secondi
:: NOT EXPECTED: finestra CMD apparentemente vuota o congelata
```

---

## NOTE TECNICHE

### Perché `ENABLE_EXTENDED_FLAGS` è necessario?

Su Windows Vista e successivi, il flag `ENABLE_QUICK_EDIT_MODE` fa parte della "extended flags" group. Per modificarlo, è necessario impostare contemporaneamente `ENABLE_EXTENDED_FLAGS`, altrimenti la maschera viene ignorata.

### Differenza tra avvio da GUI e da CLI

- **Da CLI (MSYS2/mintty)**: mintty non è una console Win32 nativa → `GetConsoleMode` fallisce → Quick Edit Mode non esiste → il daemon funziona normalmente
- **Da GUI (launcher)**: la GUI apre una CMD window nativa Win32 con Quick Edit Mode abilitato di default → il daemon si congela al primo click

### `FlushConsoleInputBuffer` è sicuro da usare?

Sì: svuota la coda di input console solo degli eventi non-tastiera. Al ritorno del `while`, `WaitForSingleObject` riparte con il timeout di 100ms e attenderà il prossimo evento. Non c'è perdita di input da tastiera perché prima di fare `Flush` controlliamo che NON ci siano eventi tastiera in coda.

---

*Documento pronto per revisione. Applicare patch A e B in Act Mode.*
