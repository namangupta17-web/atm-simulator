# ATM Simulator (C++ OOP Mini Project)

An ATM simulator written in C++ using Object-Oriented Programming, with a web frontend (HTML/CSS/JS) that talks to a C++ backend over HTTP.

## Features
- Customer: check balance, deposit, withdraw, change PIN, mini statement
- Card blocks after 3 wrong PIN attempts
- Daily withdrawal limit (resets every day) and per-transaction limit
- Admin panel: view accounts, unblock cards, add customers, set daily limits
- Web UI styled like a real ATM, with an on-screen keypad

## OOP concepts used
| Concept | Where |
|---|---|
| Encapsulation | PIN, balance and admin password are private |
| Abstraction | `Person` and `Transaction` are abstract classes |
| Inheritance | `User`, `Admin` extend `Person`; `Deposit`, `Withdrawal`, `PinChange` extend `Transaction` |
| Polymorphism | `User::perform()` runs any `Transaction`; `role()` differs per class |

## Files
- `atm_server.cpp` - C++ backend (HTTP server, no external libraries)
- `index.html` - web frontend
- `atm_simulator.cpp` - original console-only version

## Run (web version)
```bash
g++ -std=c++14 atm_server.cpp -o atm_server        # Windows: add -lws2_32
./atm_server
```
Then open http://localhost:8080

## Demo logins
| Card | PIN |
|---|---|
| 1111 | 1234 |
| 2222 | 4321 |
| 3333 | 1111 |

Admin: username `admin`, password `admin123` (demo only).

## Run (console version)
```bash
g++ -std=c++14 atm_simulator.cpp -o atm
./atm
```

Data is stored in memory only, so it resets when the server restarts.
