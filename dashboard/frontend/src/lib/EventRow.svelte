<script lang="ts">
	import type { DashEvent } from './types';

	let { ev }: { ev: DashEvent } = $props();

	function summary(e: DashEvent): string {
		switch (e.type) {
			case 'decision':
				return `${e.decision?.verdict} ${e.decision?.matchedPlate || '(no plate)'} — ${e.decision?.reason ?? ''}`;
			case 'telemetry':
				return `${e.telemetry?.state} (seq ${e.telemetry?.seq})`;
			case 'fault':
				return `${e.fault?.severity} ${e.fault?.code}: ${e.fault?.description ?? ''}`;
			case 'ota':
				return `${e.ota?.phase} ${e.ota?.bytesTotal ? Math.round(((e.ota?.bytesReceived ?? 0) / e.ota.bytesTotal) * 100) + '%' : ''}`;
			case 'command':
				return `${e.command?.kind} by ${e.command?.actor || '?'}`;
			case 'ack':
				return e.ack?.completed
					? e.ack.success
						? `✓ ${e.ack.commandId} → ${e.ack.stateAfter}`
						: `✗ ${e.ack?.commandId}: ${e.ack?.error}`
					: `received ${e.ack?.commandId}`;
			default:
				return '';
		}
	}

	const gate = $derived(
		ev.telemetry?.gateId ?? ev.decision?.gateId ?? ev.fault?.gateId ?? ev.command?.gateId ?? ''
	);
	const time = $derived(ev.event ? new Date(ev.event.tsMs).toLocaleTimeString() : '');
	const bad = $derived(
		ev.type === 'fault' || (ev.type === 'ack' && ev.ack?.completed && !ev.ack.success)
	);
</script>

<tr class:bad>
	<td class="t">{time}</td>
	<td class="type">{ev.type}</td>
	<td class="gate">{gate}</td>
	<td>{summary(ev)}</td>
</tr>

<style>
	tr.bad td {
		color: #c62828;
	}
	td {
		padding: 0.25rem 0.6rem;
		border-bottom: 1px solid #eee;
		font-size: 0.85rem;
	}
	.t {
		white-space: nowrap;
		color: #666;
	}
	.type {
		font-weight: 600;
		text-transform: uppercase;
		font-size: 0.7rem;
		letter-spacing: 0.05em;
	}
	.gate {
		font-weight: 600;
	}
</style>
