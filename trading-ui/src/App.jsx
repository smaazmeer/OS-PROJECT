import React, { useState, useEffect, useCallback } from 'react';
import { AdvancedRealTimeChart } from "react-ts-tradingview-widgets";

const API = 'http://localhost:8000';

const TV_SYMBOL_MAP = {
  AAPL: 'NASDAQ:AAPL', MSFT: 'NASDAQ:MSFT', GOOG: 'NASDAQ:GOOG',
  AMZN: 'NASDAQ:AMZN', NVDA: 'NASDAQ:NVDA', TSLA: 'NASDAQ:TSLA',
  META: 'NASDAQ:META', JPM:  'NYSE:JPM',     V:    'NYSE:V',
  NFLX: 'NASDAQ:NFLX',
};

// ─── UI primitives ────────────────────────────────────────────────────────
const Card = ({ children, title, className = '', action }) => (
  <div className={`bg-[#111827] border border-[#2e374d] rounded-lg flex flex-col overflow-hidden ${className}`}>
    {title && (
      <div className="border-b border-[#2e374d] px-3 py-2 flex-shrink-0 flex items-center justify-between">
        <h2 className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">{title}</h2>
        {action}
      </div>
    )}
    <div className="flex-grow overflow-hidden flex flex-col">{children}</div>
  </div>
);

const Badge = ({ children, color }) => {
  const c = {
    green:  'bg-green-900/40 text-green-400 border border-green-800',
    red:    'bg-red-900/40 text-red-400 border border-red-800',
    yellow: 'bg-yellow-900/40 text-yellow-400 border border-yellow-800',
    blue:   'bg-blue-900/40 text-blue-400 border border-blue-800',
    gray:   'bg-gray-800 text-gray-400 border border-gray-700',
  };
  return (
    <span className={`px-1.5 py-0.5 rounded text-[10px] font-semibold ${c[color] || c.gray}`}>
      {children}
    </span>
  );
};

const StatCard = ({ label, value, sub, color = 'white' }) => (
  <div className="bg-[#111827] border border-[#2e374d] rounded-lg p-4">
    <div className="text-[10px] text-gray-500 uppercase tracking-wider mb-1">{label}</div>
    <div className={`text-xl font-bold font-mono ${color}`}>{value}</div>
    {sub && <div className="text-[11px] text-gray-500 mt-0.5">{sub}</div>}
  </div>
);

const Toast = ({ msg, type }) =>
  msg ? (
    <div className={`fixed bottom-5 right-5 z-50 px-4 py-3 rounded-lg shadow-2xl text-sm font-semibold
        border ${type === 'error'
          ? 'bg-red-950 border-red-700 text-red-300'
          : 'bg-green-950 border-green-700 text-green-300'}`}>
      {msg}
    </div>
  ) : null;

// ─── Mini sparkline (SVG, no deps) ────────────────────────────────────────
const Sparkline = ({ values, color }) => {
  if (!values || values.length < 2) return null;
  const w = 80, h = 28;
  const min = Math.min(...values), max = Math.max(...values);
  const range = max - min || 1;
  const pts = values.map((v, i) => {
    const x = (i / (values.length - 1)) * w;
    const y = h - ((v - min) / range) * h;
    return `${x},${y}`;
  }).join(' ');
  return (
    <svg width={w} height={h} viewBox={`0 0 ${w} ${h}`}>
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.5" strokeLinejoin="round" />
    </svg>
  );
};

// ─────────────────────────────────────────────────────────────────────────
export default function App() {
  const [stocks,      setStocks]      = useState([]);
  const [account,     setAccount]     = useState(null);
  const [orders,      setOrders]      = useState([]);
  const [history,     setHistory]     = useState([]);
  const [activeStock, setActiveStock] = useState('AAPL');
  const [priceHistory, setPriceHistory] = useState({}); // symbol → last 30 prices

  const [mainTab,  setMainTab]  = useState('Trading Area');
  const [orderTab, setOrderTab] = useState('Market');
  const [histTab,  setHistTab]  = useState('All');

  const [qty,        setQty]        = useState(1);
  const [limitPrice, setLimitPrice] = useState('');
  const [stopPrice,  setStopPrice]  = useState('');

  const [toast,   setToast]   = useState({ msg: '', type: '' });
  const [loading, setLoading] = useState(false);

  const livePrice = stocks.find(s => s.name === activeStock)?.price ?? 0;

  const notify = (msg, type = 'success') => {
    setToast({ msg, type });
    setTimeout(() => setToast({ msg: '', type: '' }), 3500);
  };

  // ─── Polling ─────────────────────────────────────────────────────────
  const fetchAll = useCallback(async () => {
    try {
      const [sRes, aRes, oRes, hRes] = await Promise.all([
        fetch(`${API}/api/stocks`),
        fetch(`${API}/api/account`),
        fetch(`${API}/api/orders`),
        fetch(`${API}/api/history`),
      ]);
      if (sRes.ok) {
        const s = await sRes.json();
        setStocks(s);
        // Build rolling price history for sparklines
        setPriceHistory(prev => {
          const next = { ...prev };
          s.forEach(st => {
            const arr = next[st.name] || [];
            next[st.name] = [...arr, st.price].slice(-30);
          });
          return next;
        });
      }
      if (aRes.ok) setAccount(await aRes.json());
      if (oRes.ok) setOrders(await oRes.json());
      if (hRes.ok) setHistory(await hRes.json());
    } catch { /* backend not running yet */ }
  }, []);

  useEffect(() => {
    fetchAll();
    const id = setInterval(fetchAll, 500);
    return () => clearInterval(id);
  }, [fetchAll]);

  // ─── Place order ──────────────────────────────────────────────────────
  const executeTrade = async (side) => {
    if (qty <= 0) return notify('Quantity must be > 0', 'error');
    if (orderTab !== 'Market' && !limitPrice)
      return notify('Enter a limit / take-profit price', 'error');
    if (orderTab === 'OCO' && !stopPrice)
      return notify('Enter a stop-loss price for OCO', 'error');

    const body = {
      symbol:      activeStock,
      side,
      type:        orderTab.toUpperCase(),
      qty,
      // price      = take-profit (OCO SELL) or dip-buy (OCO BUY) or limit (LIMIT)
      limit_price: parseFloat(limitPrice) || 0,
      // stop_price = stop-loss (OCO SELL) or breakout (OCO BUY)
      stop_price:  parseFloat(stopPrice)  || 0,
    };

    setLoading(true);
    try {
      const res  = await fetch(`${API}/api/order`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const data = await res.json();
      if (res.ok) {
        notify(`✓ ${side} ${activeStock} ×${qty} placed (id #${data.order_id})`, 'success');
        fetchAll();
      } else {
        notify(data.message || 'Order rejected', 'error');
      }
    } catch { notify('Cannot reach backend', 'error'); }
    finally   { setLoading(false); }
  };

  const cancelOrder = async (orderId) => {
    try {
      const res  = await fetch(`${API}/api/cancel`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ order_id: orderId }),
      });
      const data = await res.json();
      notify(data.message || 'Cancel sent', res.ok ? 'success' : 'error');
      fetchAll();
    } catch { notify('Cannot reach backend', 'error'); }
  };

  const effectivePrice = orderTab === 'Market' ? livePrice : (parseFloat(limitPrice) || 0);
  const totalValue     = effectivePrice * qty;
  const pendingOrders  = orders.filter(o => o.status === 'PENDING');

  // Filtered history
  const filteredHistory = histTab === 'All'      ? history
    : histTab === 'Filled'    ? history.filter(o => o.status === 'FILLED')
    : history.filter(o => o.status === 'CANCELLED');

  // Portfolio summary stats
  const totalPL        = account?.total_pl        ?? 0;
  const unrealisedPL   = account?.unrealised_pl   ?? 0;
  const realisedPL     = account?.realised_pl     ?? 0;
  const portfolioValue = account?.portfolio_value ?? 0;
  const cashAvailable  = account?.available       ?? 0;
  const totalBalance   = account?.balance         ?? 0;
  const initialBalance = account?.initial_balance ?? 100000;
  const overallReturn  = initialBalance > 0
    ? ((portfolioValue - initialBalance) / initialBalance) * 100 : 0;

  // ─── Render ───────────────────────────────────────────────────────────
  return (
    <div className="h-screen w-screen bg-[#0a0e17] text-white flex flex-col overflow-hidden font-sans text-xs">

      {/* ── NAV ── */}
      <div className="h-12 border-b border-[#2e374d] bg-[#111827] flex items-center justify-between px-4 flex-shrink-0">
        <div className="flex items-center gap-8">
          <span className="text-sm font-bold text-blue-400 tracking-tight">⬡ Pro Terminal</span>
          <nav className="flex gap-5 text-gray-400 font-medium h-12 items-end">
            {['Trading Area', 'Portfolio & History'].map(t => (
              <button key={t} onClick={() => setMainTab(t)}
                className={`pb-3 transition-colors text-xs ${mainTab === t
                  ? 'text-blue-400 border-b-2 border-blue-500'
                  : 'hover:text-gray-200'}`}>{t}</button>
            ))}
          </nav>
        </div>
        {account && (
          <div className="flex gap-5 text-gray-300 text-[11px]">
            <span>Cash <b className="text-white font-mono">${cashAvailable.toFixed(2)}</b></span>
            <span>Portfolio <b className="text-white font-mono">${portfolioValue.toFixed(2)}</b></span>
            <span className={totalPL >= 0 ? 'text-green-400' : 'text-red-400'}>
              Total P/L <b className="font-mono">{totalPL >= 0 ? '+' : ''}${totalPL.toFixed(2)}</b>
            </span>
            <span className={realisedPL >= 0 ? 'text-green-300' : 'text-red-300'}>
              Realised <b className="font-mono">{realisedPL >= 0 ? '+' : ''}${realisedPL.toFixed(2)}</b>
            </span>
          </div>
        )}
      </div>

      {/* ══════════════ PORTFOLIO TAB ══════════════════════════════════════ */}
      {mainTab === 'Portfolio & History' ? (
        <div className="flex-grow overflow-y-auto bg-[#0a0e17]">
          <div className="p-5 space-y-5 max-w-7xl mx-auto">

            {/* ── Summary stat cards ── */}
            <div className="grid grid-cols-3 gap-3">
              <StatCard
                label="Total Portfolio Value"
                value={`$${portfolioValue.toFixed(2)}`}
                sub={`Started with $${initialBalance.toFixed(2)}`}
                color="text-white"
              />
              <StatCard
                label="Available Cash"
                value={`$${cashAvailable.toFixed(2)}`}
                sub={`Reserved: $${(totalBalance - cashAvailable).toFixed(2)}`}
                color="text-blue-400"
              />
              <StatCard
                label="Overall Return"
                value={`${overallReturn >= 0 ? '+' : ''}${overallReturn.toFixed(2)}%`}
                sub={`$${(portfolioValue - initialBalance).toFixed(2)} net`}
                color={overallReturn >= 0 ? 'text-green-400' : 'text-red-400'}
              />
            </div>
            <div className="grid grid-cols-3 gap-3">
              <StatCard
                label="Total P/L (Realised + Open)"
                value={`${totalPL >= 0 ? '+' : ''}$${totalPL.toFixed(2)}`}
                sub="Closed trades + open positions"
                color={totalPL >= 0 ? 'text-green-400' : 'text-red-400'}
              />
              <StatCard
                label="Realised P/L"
                value={`${realisedPL >= 0 ? '+' : ''}$${realisedPL.toFixed(2)}`}
                sub="From fully closed positions"
                color={realisedPL >= 0 ? 'text-green-400' : 'text-red-400'}
              />
              <StatCard
                label="Unrealised P/L"
                value={`${unrealisedPL >= 0 ? '+' : ''}$${unrealisedPL.toFixed(2)}`}
                sub={`${account?.holdings?.length ?? 0} open · ${pendingOrders.length} pending`}
                color={unrealisedPL >= 0 ? 'text-green-400' : 'text-red-400'}
              />
            </div>

            {/* ── Holdings table ── */}
            <Card title="Active Holdings">
              <div className="grid px-4 py-2 text-gray-500 border-b border-[#2e374d] font-semibold"
                style={{ gridTemplateColumns: '80px 60px 60px 90px 90px 90px 1fr 70px' }}>
                <span>Symbol</span>
                <span className="text-right">Shares</span>
                <span className="text-right">Locked</span>
                <span className="text-right">Avg Cost</span>
                <span className="text-right">Live Price</span>
                <span className="text-right">Value</span>
                <span className="text-right pr-4">P/L</span>
                <span className="text-right">7d</span>
              </div>
              {(!account?.holdings || account.holdings.length === 0) && (
                <div className="p-8 text-center text-gray-600">
                  No open positions — place a buy order to get started
                </div>
              )}
              {account?.holdings?.map(h => {
                const cur   = h.current_price ?? h.avg_cost;
                const pl    = h.pl ?? (cur - h.avg_cost) * h.qty;
                const plPct = h.avg_cost > 0 ? ((cur - h.avg_cost) / h.avg_cost) * 100 : 0;
                const spark = priceHistory[h.symbol] || [];
                const barW  = Math.min(100, Math.abs(plPct) * 4);
                return (
                  <div key={h.symbol}
                    className="grid px-4 py-3 border-b border-[#2e374d] hover:bg-[#1e293b] items-center"
                    style={{ gridTemplateColumns: '80px 60px 60px 90px 90px 90px 1fr 70px' }}>
                    <span className="font-bold text-white">{h.symbol}</span>
                    <span className="text-right font-mono">{h.qty}</span>
                    <span className={`text-right font-mono ${h.locked > 0 ? 'text-yellow-400' : 'text-gray-600'}`}>
                      {h.locked}
                    </span>
                    <span className="text-right font-mono text-gray-400">${h.avg_cost?.toFixed(2)}</span>
                    <span className="text-right font-mono text-white">${cur.toFixed(2)}</span>
                    <span className="text-right font-mono text-gray-300">
                      ${(cur * h.qty).toFixed(2)}
                    </span>
                    {/* P/L with mini bar */}
                    <div className="pr-4">
                      <div className={`text-right font-mono text-[11px] ${pl >= 0 ? 'text-green-400' : 'text-red-400'}`}>
                        {pl >= 0 ? '+' : ''}${pl.toFixed(2)}
                        <span className="ml-1 text-[10px] opacity-70">
                          ({plPct >= 0 ? '+' : ''}{plPct.toFixed(2)}%)
                        </span>
                      </div>
                      <div className="mt-1 h-1 bg-[#2e374d] rounded overflow-hidden">
                        <div
                          className={`h-full rounded ${pl >= 0 ? 'bg-green-500' : 'bg-red-500'}`}
                          style={{ width: `${barW}%` }}
                        />
                      </div>
                    </div>
                    {/* Sparkline */}
                    <div className="flex justify-end">
                      <Sparkline
                        values={spark}
                        color={spark.length > 1 && spark[spark.length-1] >= spark[0]
                          ? '#4ade80' : '#f87171'}
                      />
                    </div>
                  </div>
                );
              })}
            </Card>

            {/* ── Pending orders ── */}
            {pendingOrders.length > 0 && (
              <Card title="Pending Orders">
                <div className="grid px-4 py-2 text-gray-500 border-b border-[#2e374d] font-semibold"
                  style={{ gridTemplateColumns: '60px 70px 60px 60px 60px 90px 90px 1fr' }}>
                  <span>#ID</span>
                  <span>Symbol</span>
                  <span>Side</span>
                  <span>Type</span>
                  <span className="text-right">Qty</span>
                  <span className="text-right">Limit/TP</span>
                  <span className="text-right">Stop</span>
                  <span className="text-right">Action</span>
                </div>
                {pendingOrders.map(o => (
                  <div key={o.order_id}
                    className="grid px-4 py-2.5 border-b border-[#2e374d] hover:bg-[#1e293b] items-center"
                    style={{ gridTemplateColumns: '60px 70px 60px 60px 60px 90px 90px 1fr' }}>
                    <span className="text-gray-600 font-mono">#{o.order_id}</span>
                    <span className="font-bold">{o.symbol}</span>
                    <Badge color={o.type === 'BUY' ? 'green' : 'red'}>{o.type}</Badge>
                    <Badge color={o.kind === 'OCO' ? 'yellow' : 'gray'}>{o.kind}</Badge>
                    <span className="text-right font-mono">{o.qty}</span>
                    <span className="text-right font-mono text-gray-300">${o.price?.toFixed(2)}</span>
                    <span className="text-right font-mono text-gray-500">
                      {o.stop_price > 0 ? `$${o.stop_price?.toFixed(2)}` : '—'}
                    </span>
                    <div className="text-right">
                      <button onClick={() => cancelOrder(o.order_id)}
                        className="text-red-400 hover:text-red-300 hover:bg-red-900/30 px-2 py-0.5 rounded text-[11px] font-semibold transition">
                        Cancel
                      </button>
                    </div>
                  </div>
                ))}
              </Card>
            )}

            {/* ── Order history ── */}
            <Card
              title="Order History"
              action={
                <div className="flex gap-1">
                  {['All', 'Filled', 'Cancelled'].map(t => (
                    <button key={t} onClick={() => setHistTab(t)}
                      className={`px-2 py-0.5 rounded text-[10px] font-semibold transition
                        ${histTab === t
                          ? 'bg-blue-600 text-white'
                          : 'text-gray-500 hover:text-gray-300'}`}>
                      {t}
                    </button>
                  ))}
                </div>
              }
            >
              <div className="grid px-4 py-2 text-gray-500 border-b border-[#2e374d] font-semibold"
                style={{ gridTemplateColumns: '60px 70px 60px 60px 60px 90px 90px 80px' }}>
                <span>#ID</span>
                <span>Symbol</span>
                <span>Side</span>
                <span>Type</span>
                <span className="text-right">Qty</span>
                <span className="text-right">Price</span>
                <span className="text-right">Stop</span>
                <span className="text-right">Status</span>
              </div>

              <div className="overflow-y-auto" style={{ maxHeight: '340px' }}>
                {filteredHistory.length === 0 && (
                  <div className="p-8 text-center text-gray-600">
                    No {histTab.toLowerCase()} orders yet
                  </div>
                )}
                {filteredHistory.map((o, idx) => (
                  <div key={`${o.order_id}-${idx}`}
                    className="grid px-4 py-2.5 border-b border-[#2e374d] hover:bg-[#1e293b] items-center"
                    style={{ gridTemplateColumns: '60px 70px 60px 60px 60px 90px 90px 80px' }}>
                    <span className="text-gray-600 font-mono">#{o.order_id}</span>
                    <span className="font-bold">{o.symbol}</span>
                    <Badge color={o.type === 'BUY' ? 'green' : 'red'}>{o.type}</Badge>
                    <Badge color={o.kind === 'OCO' ? 'yellow' : 'gray'}>{o.kind}</Badge>
                    <span className="text-right font-mono">{o.qty}</span>
                    <span className="text-right font-mono text-gray-300">
                      {o.price > 0 ? `$${o.price.toFixed(2)}` : 'MKT'}
                    </span>
                    <span className="text-right font-mono text-gray-500">
                      {o.stop_price > 0 ? `$${o.stop_price.toFixed(2)}` : '—'}
                    </span>
                    <div className="text-right">
                      <Badge color={
                        o.status === 'FILLED'    ? 'green' :
                        o.status === 'CANCELLED' ? 'red'   : 'yellow'
                      }>
                        {o.status}
                      </Badge>
                    </div>
                  </div>
                ))}
              </div>
            </Card>
          </div>
        </div>

      ) : (
        /* ══════════════ TRADING AREA TAB ══════════════════════════════════ */
        <div className="flex-grow grid gap-2 p-2 overflow-hidden"
          style={{ gridTemplateColumns: '240px 1fr 300px' }}>

          {/* ── WATCHLIST ── */}
          <Card className="h-full">
            <div className="px-3 py-2 border-b border-[#2e374d] text-[10px] font-semibold text-gray-400 uppercase tracking-wider">
              Watchlist
            </div>
            <div className="overflow-y-auto flex-grow">
              {stocks.length === 0 && (
                <div className="p-4 text-center text-gray-600">Connecting…</div>
              )}
              {stocks.map(s => {
                const hist    = priceHistory[s.name] || [];
                const prev    = hist.length > 1 ? hist[hist.length - 2] : s.price;
                const chg     = prev > 0 ? ((s.price - prev) / prev) * 100 : 0;
                const holding = account?.holdings?.find(h => h.symbol === s.name);
                return (
                  <div key={s.name}
                    onClick={() => setActiveStock(s.name)}
                    className={`px-3 py-2.5 cursor-pointer border-l-2 transition-colors
                      ${activeStock === s.name
                        ? 'bg-[#1e293b] border-blue-500'
                        : 'border-transparent hover:bg-[#1e293b]/50'}`}>
                    <div className="flex justify-between items-start">
                      <div>
                        <div className="font-bold text-gray-100">{s.name}</div>
                        {holding && (
                          <div className="text-[10px] text-gray-500">{holding.qty} shares</div>
                        )}
                      </div>
                      <div className="text-right">
                        <div className="font-mono text-gray-100">${s.price.toFixed(2)}</div>
                        <div className={`text-[10px] font-mono ${chg >= 0 ? 'text-green-400' : 'text-red-400'}`}>
                          {chg >= 0 ? '▲' : '▼'} {Math.abs(chg).toFixed(2)}%
                        </div>
                      </div>
                    </div>
                    <div className="mt-1">
                      <Sparkline values={hist}
                        color={hist.length > 1 && hist[hist.length-1] >= hist[0]
                          ? '#4ade80' : '#f87171'} />
                    </div>
                  </div>
                );
              })}
            </div>
          </Card>

          {/* ── TRADINGVIEW CHART ── */}
          <Card className="h-full relative p-0 overflow-hidden">
            <AdvancedRealTimeChart
              key={activeStock}
              symbol={TV_SYMBOL_MAP[activeStock] || `NASDAQ:${activeStock}`}
              theme="dark"
              autosize
              hide_side_toolbar={false}
              allow_symbol_change={false}
              interval="1"
              style="1"
            />
          </Card>

          {/* ── ORDER PANEL ── */}
          <Card title="Place Order" className="h-full">
            <div className="p-3 space-y-3 flex-grow overflow-y-auto">

              {/* Live price */}
              <div className="bg-[#0a0e17] border border-[#2e374d] rounded p-3 text-center">
                <div className="text-[10px] text-gray-500 uppercase tracking-wider mb-0.5">
                  {activeStock} — Backend Price
                </div>
                <div className="text-2xl font-bold font-mono">${livePrice.toFixed(2)}</div>
              </div>

              {/* Order type tabs */}
              <div className="flex bg-[#0a0e17] border border-[#2e374d] rounded p-0.5">
                {['Market', 'Limit', 'OCO'].map(t => (
                  <button key={t} onClick={() => setOrderTab(t)}
                    className={`flex-1 py-1.5 text-[11px] font-semibold rounded transition
                      ${orderTab === t ? 'bg-blue-600 text-white' : 'text-gray-400 hover:text-white'}`}>
                    {t}
                  </button>
                ))}
              </div>

              {/* OCO explanation */}
              {orderTab === 'OCO' && (
                <div className="bg-yellow-900/20 border border-yellow-800/40 rounded p-2 text-[10px] text-yellow-400 leading-relaxed">
                  <b>OCO SELL:</b> Take-profit price (above) + Stop-loss price (below).<br/>
                  <b>OCO BUY:</b> Dip-buy price (below) + Breakout price (above).
                </div>
              )}

              {/* Limit / Take-profit price */}
              {(orderTab === 'Limit' || orderTab === 'OCO') && (
                <div className="flex justify-between items-center bg-[#1f2937] border border-[#2e374d] px-3 py-2 rounded">
                  <span className="text-gray-400">
                    {orderTab === 'OCO' ? 'Take-Profit / Dip-Buy' : 'Limit Price'}
                  </span>
                  <input type="number" value={limitPrice}
                    onChange={e => setLimitPrice(e.target.value)}
                    placeholder="0.00"
                    className="bg-transparent text-right outline-none text-white font-mono w-28" />
                </div>
              )}

              {/* Stop-loss price (OCO only) */}
              {orderTab === 'OCO' && (
                <div className="flex justify-between items-center bg-[#1f2937] border border-[#2e374d] px-3 py-2 rounded">
                  <span className="text-gray-400">Stop-Loss / Breakout</span>
                  <input type="number" value={stopPrice}
                    onChange={e => setStopPrice(e.target.value)}
                    placeholder="0.00"
                    className="bg-transparent text-right outline-none text-white font-mono w-28" />
                </div>
              )}

              {/* Quantity */}
              <div>
                <div className="text-[10px] text-gray-500 uppercase tracking-wider mb-1">Quantity</div>
                <div className="flex bg-[#1f2937] border border-[#2e374d] rounded items-center">
                  <button onClick={() => setQty(q => Math.max(1, q - 1))}
                    className="px-4 py-2 text-gray-400 hover:text-white text-base">−</button>
                  <input type="number" value={qty}
                    onChange={e => setQty(Math.max(1, parseInt(e.target.value) || 1))}
                    className="w-full bg-transparent text-center font-mono outline-none py-2" />
                  <button onClick={() => setQty(q => q + 1)}
                    className="px-4 py-2 text-gray-400 hover:text-white text-base">+</button>
                </div>
              </div>

              {/* Totals row */}
              <div className="space-y-1">
                <div className="flex justify-between px-1">
                  <span className="text-gray-500">Est. Total</span>
                  <span className="font-bold font-mono">${totalValue.toFixed(2)}</span>
                </div>
                {account && (
                  <div className="flex justify-between px-1">
                    <span className="text-gray-500">Available Cash</span>
                    <span className="text-gray-300 font-mono">${cashAvailable.toFixed(2)}</span>
                  </div>
                )}
                {account?.holdings?.find(h => h.symbol === activeStock) && (
                  <div className="flex justify-between px-1">
                    <span className="text-gray-500">Shares Held</span>
                    <span className="text-gray-300 font-mono">
                      {account.holdings.find(h => h.symbol === activeStock)?.qty ?? 0}
                    </span>
                  </div>
                )}
              </div>

              {/* BUY / SELL */}
              <div className="grid grid-cols-2 gap-3 pt-1">
                <button onClick={() => executeTrade('BUY')} disabled={loading}
                  className="bg-green-700 hover:bg-green-600 disabled:opacity-50 py-3 rounded font-bold transition">
                  BUY
                </button>
                <button onClick={() => executeTrade('SELL')} disabled={loading}
                  className="bg-red-700 hover:bg-red-600 disabled:opacity-50 py-3 rounded font-bold transition">
                  SELL
                </button>
              </div>

              {/* Inline pending orders */}
              {pendingOrders.length > 0 && (
                <div>
                  <div className="text-[10px] text-gray-500 uppercase tracking-wider mb-1 mt-1">
                    Active Orders ({pendingOrders.length})
                  </div>
                  <div className="space-y-1 max-h-36 overflow-y-auto">
                    {pendingOrders.map(o => (
                      <div key={o.order_id}
                        className="flex items-center justify-between bg-[#1f2937] px-2 py-1.5 rounded border border-[#2e374d]">
                        <span className="flex items-center gap-1.5 min-w-0">
                          <Badge color={o.type === 'BUY' ? 'green' : 'red'}>{o.type}</Badge>
                          <Badge color={o.kind === 'OCO' ? 'yellow' : 'gray'}>{o.kind}</Badge>
                          <span className="text-gray-300 truncate">{o.symbol} ×{o.qty}</span>
                          <span className="text-gray-500 font-mono">${o.price?.toFixed(0)}</span>
                        </span>
                        <button onClick={() => cancelOrder(o.order_id)}
                          className="text-red-400 hover:text-red-300 ml-1 flex-shrink-0">✕</button>
                      </div>
                    ))}
                  </div>
                </div>
              )}
            </div>
          </Card>
        </div>
      )}

      <Toast msg={toast.msg} type={toast.type} />
    </div>
  );
}