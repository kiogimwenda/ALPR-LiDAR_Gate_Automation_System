<script lang="ts">
	import { onMount } from 'svelte';
	import { deleteAllowlist, listAllowlist, upsertAllowlist } from '$lib/api';
	import { VEHICLE_CLASSES, type AllowlistEntry } from '$lib/types';

	let entries = $state<AllowlistEntry[]>([]);
	let loading = $state(true);
	let error = $state('');

	// Add-entry form.
	let plate = $state('');
	let ownerName = $state('');
	let ownerUnit = $state('');
	let notes = $state('');
	let classes = $state<string[]>([]);
	let saving = $state(false);
	let formError = $state('');

	async function refresh(): Promise<void> {
		loading = true;
		const r = await listAllowlist();
		if (r.ok && r.data) {
			entries = r.data.entries;
			error = '';
		} else {
			error = r.error ?? 'failed to load';
		}
		loading = false;
	}

	async function add(e: SubmitEvent): Promise<void> {
		e.preventDefault();
		saving = true;
		formError = '';
		const r = await upsertAllowlist({
			plate: plate.trim().toUpperCase(),
			ownerName: ownerName.trim(),
			ownerUnit: ownerUnit.trim(),
			notes: notes.trim(),
			allowedClasses: classes
		});
		if (r.ok) {
			plate = ownerName = ownerUnit = notes = '';
			classes = [];
			await refresh();
		} else {
			formError = r.error ?? 'failed to save';
		}
		saving = false;
	}

	async function remove(p: string): Promise<void> {
		const r = await deleteAllowlist(p);
		if (r.ok) {
			await refresh();
		} else {
			error = r.error ?? 'failed to delete';
		}
	}

	function toggleClass(c: string): void {
		classes = classes.includes(c) ? classes.filter((x) => x !== c) : [...classes, c];
	}

	onMount(refresh);
</script>

<main>
	<section>
		<h2>Add plate</h2>
		<form onsubmit={add}>
			<input placeholder="Plate (e.g. KDA123X)" bind:value={plate} required />
			<input placeholder="Owner name" bind:value={ownerName} />
			<input placeholder="Unit" bind:value={ownerUnit} class="narrow" />
			<input placeholder="Notes" bind:value={notes} />
			<div class="classes">
				{#each VEHICLE_CLASSES as c (c)}
					<label>
						<input
							type="checkbox"
							checked={classes.includes(c)}
							onchange={() => toggleClass(c)}
						/>
						{c.toLowerCase()}
					</label>
				{/each}
			</div>
			<div class="form-foot">
				<button disabled={saving || !plate.trim()}>Add</button>
				<span class="hint">no classes selected = any class allowed</span>
				{#if formError}<span class="error">{formError}</span>{/if}
			</div>
		</form>
	</section>

	<section>
		<h2>Allowlist</h2>
		{#if loading}
			<p class="empty">Loading…</p>
		{:else if error}
			<p class="error">{error}</p>
		{:else if entries.length === 0}
			<p class="empty">No plates yet.</p>
		{:else}
			<table>
				<thead>
					<tr><th>Plate</th><th>Owner</th><th>Unit</th><th>Classes</th><th>Notes</th><th>Added by</th><th></th></tr>
				</thead>
				<tbody>
					{#each entries as e (e.plate)}
						<tr>
							<td class="plate">{e.plate}</td>
							<td>{e.ownerName}</td>
							<td>{e.ownerUnit}</td>
							<td>{e.allowedClasses.length ? e.allowedClasses.join(', ').toLowerCase() : 'any'}</td>
							<td>{e.notes}</td>
							<td>{e.addedBy}</td>
							<td><button class="danger" onclick={() => remove(e.plate)}>Remove</button></td>
						</tr>
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
	form {
		background: #fff;
		border: 1px solid #ddd;
		border-radius: 8px;
		padding: 1rem;
		display: grid;
		gap: 0.6rem;
		max-width: 640px;
	}
	input:not([type='checkbox']) {
		padding: 0.4rem 0.6rem;
		border: 1px solid #ccc;
		border-radius: 6px;
		font-size: 0.9rem;
	}
	.classes {
		display: flex;
		flex-wrap: wrap;
		gap: 0.3rem 1rem;
		font-size: 0.85rem;
	}
	.form-foot {
		display: flex;
		align-items: center;
		gap: 0.8rem;
	}
	button {
		font-size: 0.85rem;
		padding: 0.35rem 1.1rem;
		border: 1px solid #bbb;
		border-radius: 6px;
		background: #fafafa;
		cursor: pointer;
	}
	button:hover:not(:disabled) {
		background: #eee;
	}
	button:disabled {
		opacity: 0.5;
		cursor: default;
	}
	button.danger {
		color: #c62828;
		border-color: #e3b3b3;
	}
	.hint {
		font-size: 0.75rem;
		color: #888;
	}
	.error {
		color: #c62828;
		font-size: 0.85rem;
	}
	.empty {
		color: #888;
		font-style: italic;
	}
	table {
		border-collapse: collapse;
		width: 100%;
		background: #fff;
		border-radius: 8px;
	}
	th {
		text-align: left;
		font-size: 0.75rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
		color: #777;
		padding: 0.4rem 0.6rem;
		border-bottom: 2px solid #eee;
	}
	td {
		padding: 0.35rem 0.6rem;
		border-bottom: 1px solid #eee;
		font-size: 0.85rem;
	}
	.plate {
		font-weight: 700;
	}
</style>
