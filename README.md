# 🍽️ Restaurant Simulation

A multithreaded simulation of a restaurant kitchen, written in **C** and **Bash**, built for the Operating Systems laboratory course (Project 2026‑5).

Customers, waiters, and cooks are each modelled as independent threads that coordinate through shared data structures and synchronization primitives. The restaurant earns or loses points based on how quickly dishes are served, how clean the kitchen is kept, and whether customers leave satisfied.

![Language](https://img.shields.io/badge/language-C-blue)
![Shell](https://img.shields.io/badge/scripts-Bash-green)
![Build](https://img.shields.io/badge/build-Make-orange)
![Platform](https://img.shields.io/badge/platform-Ubuntu%2024.04-lightgrey)

---

## Overview

The simulation runs three kinds of worker threads plus a main coordinator:

- **Customers** spawn at random intervals (up to a maximum room capacity), place a random order from the menu, and wait. Each has a *patience* level; if it runs out before all dishes arrive, they leave unhappy.
- **Waiters** read active orders, dispatch dishes to the least-busy cooks, deliver finished dishes back to customers, and entertain waiting customers to nudge their patience up or down.
- **Cooks** pull dishes from their personal queue, acquire the required kitchen resources, cook, then either clean the resources or pile them up dirty in the sink.
- **Main thread** loads parameters, initializes shared state, spawns all threads, prints periodic status reports, and tears everything down cleanly at the end.

All timed operations (cooking, cleaning, patience decay, spawn intervals) are scaled by a configurable `GAME_SPEED` factor.


## Requirements

- A Linux environment (tested on **Ubuntu 24.04**)
- `gcc` with POSIX threads support
- `make`
- The C standard math library (`-lm`, linked automatically)

No external dependencies beyond the C standard library and `pthread`.

## Build & run

The project uses a `Makefile` with three targets:

| Target        | What it does                                                              |
| ------------- | ------------------------------------------------------------------------ |
| `make build`  | Compiles the sources into the `restaurant` executable (`-Wall -Wextra -pthread -lm`) |
| `make clean`  | Removes the executable and `/tmp/restaurant.pid`                         |
| `make run`    | Builds, then launches the simulation through `bootstrap.sh`              |

Quick start:

```bash
make build      # compile
make run        # validate config and start the simulation
```

You can pass overrides to the bootstrapper through the `ARGS` variable:

```bash
make run ARGS="--num-cooks=5 --game-speed=20"
```

Or launch the bootstrapper directly:

```bash
./bootstrap.sh                          # uses ./.env
./bootstrap.sh --env-file=custom.env    # use a different env file
./bootstrap.sh --num-cooks=5            # override a single parameter
```

`bootstrap.sh` validates the environment and CSV files, applies any command-line overrides, then `exec`s the `restaurant` binary (which must already be compiled — run `make build` first).

## Configuration (`.env`)

The simulation reads its parameters from a `.env` file in the working directory. It must contain **exactly these 8 parameters**, one per line:

```bash
NUM_COOKS=4
NUM_WAITERS=2
MAX_CUSTOMERS=10
TOTAL_CUSTOMERS=30
MENU_FILE=menu.csv
RESOURCES_FILE=resources.csv
GAME_SPEED=10
RANDOM_SEED=42
```

| Parameter         | Meaning                                                                                  |
| ----------------- | ---------------------------------------------------------------------------------------- |
| `NUM_COOKS`       | Number of cook threads                                                                    |
| `NUM_WAITERS`     | Number of waiter threads                                                                  |
| `MAX_CUSTOMERS`   | Maximum customers in the restaurant at once (room capacity)                               |
| `TOTAL_CUSTOMERS` | Total customers that will visit during the run                                            |
| `MENU_FILE`       | Path to the menu CSV                                                                      |
| `RESOURCES_FILE`  | Path to the resources CSV                                                                 |
| `GAME_SPEED`      | Time-scale factor: `1` = real time, `>1` = faster, `<1` = slower (all `usleep`s divided by it) |
| `RANDOM_SEED`     | Seed for reproducible runs                                                                |


## Data files

### `menu.csv`

```
name,price,time,requirements
```

- **name** — dish name (e.g. `Carbonara`)
- **price** — price in dollars (e.g. `12`)
- **time** — cooking time in minutes (e.g. `15`)
- **requirements** — resources separated by `;`, with an optional quantity after `:`
  e.g. `pot;pan;burner:2` → one pot, one pan, two burners

### `resources.csv`

```
resource,quantity,clean_time
```

- **resource** — resource name (e.g. `burner`)
- **quantity** — how many exist in the kitchen (e.g. `4`)
- **clean_time** — minutes needed to clean it after use (e.g. `1`)

Both files are structurally validated by `bootstrap.sh` (column count, numeric fields, requirement format) before launch.

## Live status reports

While the simulation is running, the main thread prints a **lightweight periodic report** (score, customers currently inside, spawn progress).

For a **full snapshot on demand**, run:

```bash
./status.sh
```

This reads the PID from `/tmp/restaurant.pid` and sends `SIGUSR1` to the process. The signal handler then prints a complete dump including the score, customers inside / left unserved, spawn progress, the queue length of each cook, and the availability of every kitchen resource.

## Error codes

Error codes are shared in value between the C side (`error_codes.h`) and the Bash scripts:

| Code | Name                    | Meaning                                             |
| ---- | ----------------------- | --------------------------------------------------- |
| 1    | `ERR_FILE_NOT_FOUND`    | A required file does not exist                       |
| 2    | `ERR_INVALID_PARSING`   | A CSV or `.env` file is malformed                   |
| 3    | `ERR_ENV_MISSING`       | A required environment variable is absent            |
| 4    | `ERR_ALLOC_FAILED`      | A `malloc`/`calloc` returned `NULL`                 |
| 5    | `ERR_SYS_FAILURE`       | A system call failed (pthread, semaphore, open, …)  |
| 6    | `ERR_NOT_READABLE`      | A file exists but is not readable                    |
| 7    | `ERR_NOT_VALID_VALUE`   | A parameter has an out-of-range or wrong-type value  |
| 8    | `ERR_PROCESS_NOT_EXIST` | The stored PID no longer refers to a running process |

---