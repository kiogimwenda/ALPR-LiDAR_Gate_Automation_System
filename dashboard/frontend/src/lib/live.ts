// live.ts — the dashboard's connection to the backend.
//
// One WebSocket to /ws/events with automatic reconnect (1 s → 15 s
// backoff); on every (re)connect the stores are re-seeded from
// GET /api/status, so the UI converges to the truth even after a
// backend restart — the same resume-not-gap philosophy the backend
// applies to its own upstream stream. Everything downstream of here
// is plain Svelte stores, so components stay dumb.

import { writable } from 'svelte/store';
import type { DashEvent, GateSnapshot, StatusResponse } from './types';

export const gates = writable<Record<string, GateSnapshot>>({});
export const feed = writable<DashEvent[]>([]);
export const wsConnected = writable(false);
export const streamConnected = writable(false);
export const upstream = writable(false);

const FEED_LIMIT = 200;

let socket: WebSocket | null = null;
let backoffMs = 1000;
let stopped = false;

async function seed(): Promise<void> {
	try {
		const resp = await fetch('/api/status');
		if (!resp.ok) return;
		const status = (await resp.json()) as StatusResponse;
		gates.set(status.gates ?? {});
		streamConnected.set(status.streamConnected);
		upstream.set(status.upstream);
	} catch {
		// Backend unreachable — the reconnect loop keeps trying.
	}
}

function ingest(ev: DashEvent): void {
	feed.update((list) => {
		const next = [ev, ...list];
		return next.length > FEED_LIMIT ? next.slice(0, FEED_LIMIT) : next;
	});
	if (ev.type === 'telemetry' && ev.telemetry) {
		const t = ev.telemetry;
		gates.update((g) => ({ ...g, [t.gateId]: { telemetry: t, lastEvent: ev.event } }));
	}
}

function openSocket(): void {
	if (stopped) return;
	const proto = location.protocol === 'https:' ? 'wss' : 'ws';
	socket = new WebSocket(`${proto}://${location.host}/ws/events`);

	socket.onopen = () => {
		wsConnected.set(true);
		backoffMs = 1000;
		void seed(); // re-converge after any gap
	};
	socket.onmessage = (msg) => {
		try {
			ingest(JSON.parse(msg.data as string) as DashEvent);
		} catch {
			// Malformed frame — drop it, keep the stream.
		}
	};
	socket.onclose = () => {
		wsConnected.set(false);
		streamConnected.set(false);
		if (!stopped) {
			setTimeout(openSocket, backoffMs);
			backoffMs = Math.min(backoffMs * 2, 15000);
		}
	};
	socket.onerror = () => socket?.close();
}

export function connect(): void {
	stopped = false;
	void seed();
	openSocket();
}

export function disconnect(): void {
	stopped = true;
	socket?.close();
}
