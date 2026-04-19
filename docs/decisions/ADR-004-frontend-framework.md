# ADR-004: Frontend Framework — SvelteKit

## Status
Accepted

## Date
2026-04-19

## Context

The dashboard frontend is a web UI showing real-time gate state, recent events, per-stage latency charts, system resource usage (CPU, RAM, GPU via NVML), and gate override controls. It connects to the Drogon backend via REST + WebSocket. Served on LAN only.

### Options Evaluated

1. **React** (Meta)
   - Largest ecosystem, most hiring pool
   - Virtual DOM — overhead for real-time updates
   - Requires additional state management (Redux, Zustand)
   - Heavy bundle size for a simple dashboard

2. **SvelteKit** (Vercel)
   - Compiles to vanilla JS — no virtual DOM, smaller bundle
   - Reactive by default — WebSocket updates render instantly
   - Built-in routing, SSR (can disable for SPA mode)
   - Simple state management with Svelte stores
   - Excellent for real-time dashboards
   - Smaller ecosystem but sufficient for this use case

3. **Vue 3** (Evan You)
   - Composition API is clean
   - Good middle ground between React and Svelte
   - Larger bundle than Svelte, smaller than React

### Ranking

**Option 2 (SvelteKit) is best** because:
- Real-time reactivity without a virtual DOM is ideal for a WebSocket-driven dashboard
- Smallest bundle size — important since this runs on LAN with potentially slow internal WiFi
- Simplest code for the features we need (live charts, event tables, override buttons)
- No state management library needed — Svelte stores handle WebSocket state trivially

**Option 3 (Vue 3) is the fallback** if we need a larger component ecosystem.

## Decision

Use **SvelteKit** in SPA mode for the dashboard frontend.

## Consequences

- Frontend source lives in `dashboard/frontend/`
- Built with `vite` + SvelteKit, output to `dashboard/frontend/dist/`
- Drogon serves the built static assets
- WebSocket connection managed via a Svelte store
- Charts via a lightweight library (e.g., Chart.js or uPlot)
