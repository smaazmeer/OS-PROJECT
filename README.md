# Stock Trading System

A comprehensive stock trading system implemented in C using POSIX threads, mutexes, and semaphores for synchronization. Features a modern GTK-based UI with portfolio tracking, profit/loss calculations, and real-time charts.

## Features

- Simulated stock prices that update periodically with price history
- User account with balance and stock holdings
- Buy and sell orders processed asynchronously
- Thread-safe operations using mutexes and semaphores
- Graphical interface with real-time updates and colorful charts
- Portfolio value tracking with detailed P/L calculations
- Holdings table with metrics (quantity, average cost, current price, P/L)
- Tabbed interface: Market, Portfolio, Trading, Charts
- Real-time price charts with historical data visualization

## Building

Run `make` to build the executable `stock_trading`.

## Dependencies

- GCC
- POSIX threads
- GTK+ 3.0

On Ubuntu/Debian: `sudo apt-get install libgtk-3-dev`

## Running

./stock_trading

A window with tabs will open:
- **Market**: Live stock prices for 10 stocks (AAPL, GOOG, MSFT, TSLA, AMZN, NVDA, META, NFLX, BABA, ORCL)
- **Portfolio**: Account summary (balance, portfolio value, total P/L) and holdings table
- **Trading**: Input fields for stock name and quantity, Buy/Sell buttons
- **Charts**: Real-time price charts with colored lines for each stock's price history

All data updates every second with thread-safe operations.

## Project Structure

- `src/stock.h/c`: Stock data with price history tracking
- `src/account.h/c`: Account management with portfolio and P/L calculations
- `src/order.h`: Order structure
- `src/order_queue.h/c`: Thread-safe order queue
- `src/trading.h/c`: Price updater and order processor threads
- `src/ui.h/c`: Graphical UI with tabs, tables, and charts using GTK and Cairo
- `src/main.c`: Main program entry point