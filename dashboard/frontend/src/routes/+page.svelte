<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import EventRow from '$lib/EventRow.svelte';
	import GateTile from '$lib/GateTile.svelte';
	import { connect, disconnect, feed, gates, streamConnected, upstream, wsConnected } from '$lib/live';

	onMount(connect);
	onDestroy(disconnect);
</script>

<header>
	<h1>Gate Dashboard</h1>
	<div class="badges">
		<span class="badge" class:ok={$wsConnected}>backend {$wsConnected ? 'live' : 'offline'}</span>
		<span class="badge" class:ok={$streamConnected}>event stream {$streamConnected ? 'up' : 'down'}</span>
		<span class="badge" class:ok={$upstream}>server {$upstream ? 'up' : 'down'}</span>
	</div>
</header>

<main>
	<section>
		<h2>Gates</h2>
		{#if Object.keys($gates).length === 0}
			<p class="empty">No gate telemetry yet — waiting for the field controllers (or a gate-sim fleet).</p>
		{:else}
			<div class="tiles">
				{#each Object.entries($gates) as [id, snapshot] (id)}
					<GateTile gateId={id} {snapshot} />
				{/each}
			</div>
		{/if}
	</section>

	<section>
		<h2>Live events</h2>
		{#if $feed.length === 0}
			<p class="empty">Nothing yet. Events appear here the moment the server publishes them.</p>
		{:else}
			<table>
				<tbody>
					{#each $feed as ev (ev.eventId)}
						<EventRow {ev} />
					{/each}
				</tbody>
			</table>
		{/if}
	</section>
</main>

<style>
	:global(body) {
		margin: 0;
		font-family: system-ui, sans-serif;
		background: #f5f6f8;
		color: #212121;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 0.8rem 1.5rem;
		background: #1a1a2e;
		color: #fff;
	}
	h1 {
		font-size: 1.1rem;
		margin: 0;
	}
	.badges {
		display: flex;
		gap: 0.5rem;
	}
	.badge {
		font-size: 0.75rem;
		padding: 0.2rem 0.6rem;
		border-radius: 999px;
		background: #7a2b2b;
	}
	.badge.ok {
		background: #2b7a3f;
	}
	main {
		padding: 1rem 1.5rem;
		display: grid;
		gap: 1.5rem;
	}
	h2 {
		font-size: 0.9rem;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		color: #555;
	}
	.tiles {
		display: flex;
		flex-wrap: wrap;
		gap: 0.8rem;
	}
	table {
		border-collapse: collapse;
		width: 100%;
		background: #fff;
		border-radius: 8px;
	}
	.empty {
		color: #888;
		font-style: italic;
	}
</style>
