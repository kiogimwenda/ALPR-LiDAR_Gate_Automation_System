<script lang="ts">
	import type { GateSnapshot } from './types';

	let { gateId, snapshot }: { gateId: string; snapshot: GateSnapshot } = $props();

	const stateColors: Record<string, string> = {
		CLOSED: '#2e7d32',
		OPEN: '#1565c0',
		OPENING: '#f9a825',
		CLOSING: '#f9a825',
		FAULT: '#c62828',
		LOCKDOWN: '#6a1b9a',
		UNKNOWN: '#616161'
	};

	const color = $derived(stateColors[snapshot.telemetry.state] ?? '#616161');
	const ageSec = $derived(
		snapshot.lastEvent ? Math.max(0, Math.round((Date.now() - snapshot.lastEvent.tsMs) / 1000)) : null
	);
	const stale = $derived(ageSec !== null && ageSec > 10);
</script>

<div class="tile" class:stale style="--state-color: {color}">
	<div class="head">
		<span class="name">{gateId}</span>
		<span class="state">{snapshot.telemetry.state}</span>
	</div>
	<div class="facts">
		<span title="limit switches">
			⊣ {snapshot.telemetry.limitClosed ? 'closed' : snapshot.telemetry.limitOpen ? 'open' : 'mid'}
		</span>
		<span title="safety beam" class:alert={!snapshot.telemetry.beamClear}>
			{snapshot.telemetry.beamClear ? '◦ beam clear' : '● BEAM BLOCKED'}
		</span>
	</div>
	<div class="meta">
		<span>fw {snapshot.telemetry.fwVersion || '—'}</span>
		<span>up {Math.floor(snapshot.telemetry.uptimeSec / 3600)}h{Math.floor((snapshot.telemetry.uptimeSec % 3600) / 60)}m</span>
		<span>{ageSec === null ? 'no data' : stale ? `${ageSec}s ago ⚠` : `${ageSec}s ago`}</span>
	</div>
</div>

<style>
	.tile {
		border: 1px solid #ddd;
		border-left: 6px solid var(--state-color);
		border-radius: 8px;
		padding: 0.75rem 1rem;
		background: #fff;
		min-width: 220px;
	}
	.tile.stale {
		opacity: 0.6;
	}
	.head {
		display: flex;
		justify-content: space-between;
		align-items: baseline;
		margin-bottom: 0.4rem;
	}
	.name {
		font-weight: 600;
	}
	.state {
		color: var(--state-color);
		font-weight: 700;
		font-size: 0.9rem;
	}
	.facts {
		display: flex;
		gap: 1rem;
		font-size: 0.85rem;
		margin-bottom: 0.4rem;
	}
	.alert {
		color: #c62828;
		font-weight: 700;
	}
	.meta {
		display: flex;
		gap: 0.8rem;
		font-size: 0.75rem;
		color: #666;
	}
</style>
