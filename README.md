<div align="center">

<!-- Banner -->
<img src="https://capsule-render.vercel.app/api?type=waving&color=0:0d1421,50:1d4ed8,100:06b6d4&height=200&section=header&text=Pro%20Trading%20Terminal&fontSize=48&fontColor=ffffff&fontAlignY=38&desc=A%20concurrent%20stock%20trading%20system%20built%20in%20C%20%2B%20React&descAlignY=58&descColor=94a3b8" width="100%"/>

<br/>

<!-- Badges -->
![Language](https://img.shields.io/badge/Backend-C11-blue?style=for-the-badge&logo=c&logoColor=white)
![Framework](https://img.shields.io/badge/Frontend-React%2018-61dafb?style=for-the-badge&logo=react&logoColor=black)
![Build](https://img.shields.io/badge/Build-CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![HTTP](https://img.shields.io/badge/HTTP-Mongoose-orange?style=for-the-badge)
![OS](https://img.shields.io/badge/OS-Windows%20%7C%20Linux-0078D6?style=for-the-badge&logo=windows&logoColor=white)

<br/>

> **A real-time, multi-threaded stock trading platform** with a C backend and React frontend.  
> Demonstrates POSIX threads, mutexes, semaphores, and producer-consumer queues in a working financial system.

<br/>

[🚀 Quick Start](#-quick-start) • [📐 Architecture](#-architecture) • [⚙️ Installation](#%EF%B8%8F-full-installation-guide) • [🖥️ Running](#%EF%B8%8F-running-the-system) • [📡 API Reference](#-api-reference) • [🧵 OS Concepts](#-os-concepts-implemented)

</div>

---

## ✨ Features

<table>
<tr>
<td width="50%">

**📈 Trading Engine**
- Market, Limit, and OCO orders
- Real-time order execution via background thread
- Resource locking prevents double-selling
- Cash reservation for pending limit orders
- Realised + unrealised P/L tracking

</td>
<td width="50%">

**🖥️ Frontend Dashboard**
- Live price watchlist (10 stocks, updates every 500ms)
- TradingView charting widget (real exchange data)
- Portfolio holdings with live P/L bars
- Full order history with status filters
- Toast notifications for order events

</td>
</tr>
<tr>
<td width="50%">

**🧵 OS Concepts (Backend)**
- 3 concurrent POSIX threads
- 5 mutex locks (one per shared resource)
- 2 POSIX semaphores (producer-consumer)
- Circular buffer order queue (100 slots)
- Atomic trade execution (critical sections)

</td>
<td width="50%">

**🔒 Safety Guarantees**
- No double-spending (mutex-protected balance)
- No double-selling (locked share tracking)
- No buffer overflow (semaphore-bounded queue)
- No race conditions (fine-grained locking)
- Thread-safe account persistence to disk

</td>
</tr>
</table>

---

## 📐 Architecture

<div align="center">

<table>
<tr>
<td align="center" width="30%" style="background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:20px;">

**🖥️ React Frontend**<br/>
`localhost:5173`

<br/>

![Watchlist](https://img.shields.io/badge/Watchlist-Live_Prices-3b82f6?style=flat-square)<br/>
![Chart](https://img.shields.io/badge/Chart-TradingView-61dafb?style=flat-square)<br/>
![Orders](https://img.shields.io/badge/Order_Panel-Buy_%2F_Sell-10b981?style=flat-square)<br/>
![Portfolio](https://img.shields.io/badge/Portfolio-Holdings_%26_P%2FL-f59e0b?style=flat-square)<br/>
![History](https://img.shields.io/badge/History-Filled_%2F_Cancelled-8b5cf6?style=flat-square)

<br/>

*Polls every **500ms** via `fetch()`*

</td>
<td align="center" width="8%">

```
  HTTP
◄─────►
fetch()
```

</td>
<td align="center" width="34%" style="background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:20px;">

**⚙️ C Backend — Mongoose**<br/>
`localhost:8000`

<br/>

![](https://img.shields.io/badge/GET-/api/stocks-238636?style=flat-square&logo=circle)<br/>
![](https://img.shields.io/badge/GET-/api/account-238636?style=flat-square&logo=circle)<br/>
![](https://img.shields.io/badge/POST-/api/order-1d4ed8?style=flat-square&logo=circle)<br/>
![](https://img.shields.io/badge/GET-/api/orders-238636?style=flat-square&logo=circle)<br/>
![](https://img.shields.io/badge/GET-/api/history-238636?style=flat-square&logo=circle)<br/>
![](https://img.shields.io/badge/POST-/api/cancel-dc2626?style=flat-square&logo=circle)

<br/>

*Shared: `TradingSystem{}` `Account{}` `Stock[10]{}`*

</td>
<td align="center" width="8%">

```
pthread
───────
shared
memory
```

</td>
<td align="center" width="30%" style="background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:20px;">

**🧵 Background Threads**

<br/>

🟢 **price_updater**<br/>
Updates all 10 stock prices<br/>
every **500ms** via random walk<br/><br/>

🟢 **order_processor**<br/>
Dequeues orders, checks<br/>
LIMIT/OCO triggers every **50ms**<br/><br/>

📦 **OrderQueue**<br/>
Circular buffer — 100 slots<br/>
`sem_t empty` + `sem_t full`<br/>
Producer-Consumer pattern

</td>
</tr>
</table>

</div>


---

## ⚙️ Full Installation Guide

> Follow these steps **in order**. Both the backend and frontend must be running simultaneously for the system to work.

---

### Step 1 — Install Prerequisites

You need **4 tools** installed before anything else. Click each badge to go to the download page.

| Tool | Purpose | Required Version |
|------|---------|-----------------|
| [![MinGW-w64](https://img.shields.io/badge/MinGW--w64-Download-blue?style=flat-square)](https://github.com/niXman/mingw-builds-binaries/releases) | C compiler (`gcc`) + `pthread` on Windows | Latest (POSIX threads build) |
| [![CMake](https://img.shields.io/badge/CMake-Download-064F8C?style=flat-square&logo=cmake)](https://cmake.org/download/) | Build system for the C backend | ≥ 3.10 |
| [![Node.js](https://img.shields.io/badge/Node.js-Download-339933?style=flat-square&logo=node.js&logoColor=white)](https://nodejs.org/) | Runs the React frontend via Vite | ≥ 18.0 |
| [![Git](https://img.shields.io/badge/Git-Download-F05032?style=flat-square&logo=git&logoColor=white)](https://git-scm.com/downloads) | Clone this repository | Any recent version |

<details>
<summary><b>🪟 Windows — Detailed install steps (click to expand)</b></summary>

<br/>

**MinGW-w64 (POSIX threads build)**
1. Go to [mingw-builds-binaries releases](https://github.com/niXman/mingw-builds-binaries/releases)
2. Download the file ending in: `x86_64-...-release-posix-seh-ucrt-...7z`  
   *(must say **posix** — the win32 build does NOT include pthread)*
3. Extract to `C:\mingw64`
4. Add `C:\mingw64\bin` to your system PATH:
   - Search → "Edit the system environment variables" → Environment Variables
   - Under System Variables → `Path` → Edit → New → `C:\mingw64\bin`
5. Open a new terminal and verify:
```
gcc --version
```

**CMake**
1. Download the Windows installer from [cmake.org/download](https://cmake.org/download/)
2. During install: ✅ check **"Add CMake to system PATH for all users"**
3. Verify:
```
cmake --version
```

**Node.js**
1. Download the LTS installer from [nodejs.org](https://nodejs.org/)
2. Run the installer with default options
3. Verify:
```
node --version
npm --version
```

</details>

<details>
<summary><b>🐧 Linux (Ubuntu/Debian) — Detailed install steps (click to expand)</b></summary>

<br/>

```bash
# Update package list
sudo apt update

# Install GCC, CMake, Make, and pthread (included with gcc on Linux)
sudo apt install -y gcc cmake make build-essential

# Install Node.js (via NodeSource for latest LTS)
curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -
sudo apt install -y nodejs

# Verify everything
gcc --version && cmake --version && node --version && npm --version
```

</details>

---

### Step 2 — Clone This Repository

```bash
git clone https://github.com/YOUR_USERNAME/stock-trading-system.git
cd stock-trading-system
```

---

### Step 3 — Build the C Backend

```bash
# Navigate to the backend folder
cd backend

# Create and enter the build directory
mkdir build
cd build

# Configure — Windows (MinGW)
cmake .. -G "MinGW Makefiles"

# Configure — Linux/Mac (standard make)
cmake ..

# Compile
mingw32-make        # Windows
# OR
make                # Linux/Mac
```

> [!TIP]
> If CMake says it cannot find `pthread`, make sure you downloaded the **posix** variant of MinGW, not the win32 variant.

After a successful build you will see `trading_server.exe` (Windows) or `trading_server` (Linux) inside `backend/build/`.

---

### Step 4 — Install Frontend Dependencies

Open a **new terminal** (keep the backend terminal open):

```bash
# From the project root
cd trading-ui

# Install ALL frontend dependencies in one command
npm install
```

> [!NOTE]
> `npm install` reads `package.json` and automatically installs **every** frontend dependency — you do not need to install anything individually. This includes:

| Package | Version | Purpose |
|---------|---------|---------|
| `react` + `react-dom` | 18.x | UI framework |
| `vite` | 5.x | Dev server + bundler |
| `@vitejs/plugin-react` | latest | React support for Vite |
| `tailwindcss` | 4.x | Utility CSS framework |
| `react-ts-tradingview-widgets` | latest | TradingView charting widget |

This may take **1–2 minutes** the first time as it downloads all packages into `trading-ui/node_modules/`.

---

## 🖥️ Running the System

You need **two terminals open at the same time**.

### Terminal 1 — Start the C Backend

```bash
cd backend/build

# Windows
./trading_server.exe

# Linux / Mac
./trading_server
```

You should see:
```
Backend API running on http://localhost:8000
```

The backend starts **3 threads** immediately:
- `price_updater` — randomises stock prices every 500ms
- `order_processor` — checks LIMIT/OCO triggers every 50ms
- HTTP server — serves the REST API

> [!IMPORTANT]  
> Keep this terminal open. If you close it, the frontend will lose connection and prices will stop updating.

---

### Terminal 2 — Start the React Frontend

```bash
cd trading-ui
npm run dev
```

You should see:
```
  VITE v5.x  ready in 300ms

  ➜  Local:   http://localhost:5173/
```

Open **http://localhost:5173** in your browser.

---

### ✅ Verify Everything Is Working

| Check | What to look for |
|-------|-----------------|
| Watchlist populated | 10 stock symbols appear with prices |
| Prices moving | Numbers in the watchlist change every ~500ms |
| TradingView chart loads | Chart appears when you click a stock |
| Place a market buy | Balance decreases, holding appears in Portfolio tab |
| Place a limit order | Order appears in Pending Orders list |
| Order History | Completed orders appear in Portfolio → Order History |

---

## 🔁 Rebuilding After Code Changes

| Changed file | Command needed |
|-------------|---------------|
| Any `.c` or `.h` file | `cd backend/build && mingw32-make` (or `make`) |
| `App.jsx` or `index.css` | Nothing — Vite hot-reloads automatically |
| `CMakeLists.txt` | `cd backend/build && cmake .. -G "MinGW Makefiles" && mingw32-make` |
| `package.json` | `cd trading-ui && npm install` |

---

## 📡 API Reference

All endpoints are served by the C backend on `http://localhost:8000`.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/stocks` | Returns all 10 stocks with live prices |
| `GET` | `/api/account` | Returns balance, holdings, realised/unrealised P/L |
| `GET` | `/api/orders` | Returns all currently pending LIMIT/OCO orders |
| `GET` | `/api/history` | Returns all filled and cancelled orders (newest first) |
| `POST` | `/api/order` | Place a new order (see body below) |
| `POST` | `/api/cancel` | Cancel a pending order by ID |

<details>
<summary><b>POST /api/order — Request Body</b></summary>

<br/>

```json
{
  "symbol":      "AAPL",
  "side":        "BUY",
  "type":        "LIMIT",
  "qty":         10,
  "limit_price": 175.00,
  "stop_price":  0
}
```

| Field | Values | Notes |
|-------|--------|-------|
| `symbol` | `AAPL` `MSFT` `GOOG` `AMZN` `NVDA` `TSLA` `META` `JPM` `V` `NFLX` | Must match exactly |
| `side` | `BUY` \| `SELL` | |
| `type` | `MARKET` \| `LIMIT` \| `OCO` | |
| `qty` | integer > 0 | |
| `limit_price` | float | Take-profit price for OCO; limit price for LIMIT; ignored for MARKET |
| `stop_price` | float | Stop-loss price for OCO SELL; breakout price for OCO BUY |

</details>

<details>
<summary><b>POST /api/cancel — Request Body</b></summary>

<br/>

```json
{
  "order_id": 4271
}
```

</details>

---

## 🧵 OS Concepts Implemented

<table>
<tr>
<th>Concept</th>
<th>POSIX Function</th>
<th>File</th>
<th>Purpose</th>
</tr>
<tr>
<td>🧵 Thread</td>
<td><code>pthread_create()</code></td>
<td><code>main.c</code></td>
<td>3 concurrent threads — price updater, order processor, HTTP server</td>
</tr>
<tr>
<td>🔒 Mutex</td>
<td><code>pthread_mutex_lock/unlock()</code></td>
<td><code>account.c</code> <code>stock.c</code> <code>trading.c</code></td>
<td>5 separate mutexes protect each shared data structure from race conditions</td>
</tr>
<tr>
<td>⏳ Semaphore Wait</td>
<td><code>sem_wait()</code></td>
<td><code>order_queue.c</code></td>
<td>Blocks producer when queue is full; blocks consumer when queue is empty</td>
</tr>
<tr>
<td>🔔 Semaphore Signal</td>
<td><code>sem_post()</code></td>
<td><code>order_queue.c</code></td>
<td>Wakes consumer after enqueue; signals free slot after dequeue</td>
</tr>
<tr>
<td>🔄 Producer-Consumer</td>
<td><code>sem_t empty + full</code></td>
<td><code>order_queue.c</code></td>
<td>HTTP thread produces, Order Processor consumes — zero busy-wait</td>
</tr>
<tr>
<td>⚛️ Atomic Transaction</td>
<td>Critical section</td>
<td><code>trading.c</code></td>
<td>All 4 trade operations (deduct cash, credit shares, update P/L) under one lock</td>
</tr>
<tr>
<td>🔐 Resource Locking</td>
<td><code>lock_shares()</code> <code>reserve_cash()</code></td>
<td><code>account.c</code></td>
<td>Pre-commit resources before enqueue — prevents double-selling and overspending</td>
</tr>
</table>

---

## 🐛 Troubleshooting

<details>
<summary><b>❌ cmake: command not found</b></summary>

CMake is not in your PATH. Re-run the CMake installer and tick "Add to PATH", or add `C:\Program Files\CMake\bin` manually to your system environment variables.

</details>

<details>
<summary><b>❌ cannot find -lpthread</b></summary>

You are using the **win32** build of MinGW, not the **posix** build. Redownload MinGW from the releases page and pick the file with `posix` in the name.

</details>

<details>
<summary><b>❌ Watchlist is empty / "Connecting…"</b></summary>

The C backend is not running or crashed. Check Terminal 1 and make sure you see `Backend API running on http://localhost:8000`. If the terminal closed, restart the backend.

</details>

<details>
<summary><b>❌ TradingView chart does not load</b></summary>

Run `npm install` inside `trading-ui/` to make sure `react-ts-tradingview-widgets` is installed. Also check that your browser has internet access — TradingView loads chart data from their CDN.

</details>

<details>
<summary><b>❌ CORS error in browser console</b></summary>

The backend is not running, or it is running on a different port. Make sure `trading_server.exe` is running and listening on port 8000. The backend adds `Access-Control-Allow-Origin: *` to all responses automatically.

</details>

<details>
<summary><b>⚠️ Red squiggles in VS Code (pthread.h not found)</b></summary>

This is an IntelliSense issue, not a compiler error — your code still compiles and runs fine. To fix it, create `.vscode/c_cpp_properties.json` in your project root:

```json
{
  "configurations": [{
    "name": "Win32",
    "includePath": [
      "${workspaceFolder}/backend/include",
      "C:/mingw64/include",
      "C:/mingw64/x86_64-w64-mingw32/include",
      "C:/mingw64/lib/gcc/x86_64-w64-mingw32/13.2.0/include"
    ],
    "compilerPath": "C:/mingw64/bin/gcc.exe",
    "cStandard": "c11",
    "intelliSenseMode": "windows-gcc-x64"
  }],
  "version": 4
}
```

</details>

---

## 👥 Team

| Name | Student ID |
|------|-----------|
| Muhammad Aazmeer | 24K-0978 |
| Abdullah | 24K-0822 |
| Hassan Ahmed Siddiqui | 24K-1008 |

**Course:** Operating Systems Lab — 4th Semester  
**Institution:** FAST-NUCES

---

<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:06b6d4,50:1d4ed8,100:0d1421&height=120&section=footer" width="100%"/>

*Built with C11 • React 18 • POSIX Threads • Mongoose HTTP*

</div>
