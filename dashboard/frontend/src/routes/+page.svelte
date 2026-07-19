<script lang="ts">
	import EventRow from '$lib/EventRow.svelte';
	import GateTile from '$lib/GateTile.svelte';
	import { feed, gates } from '$lib/live';
</script>

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
