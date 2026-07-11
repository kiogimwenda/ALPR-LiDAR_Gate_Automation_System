<script lang="ts">
	import { login } from './auth';

	let username = $state('admin');
	let password = $state('');
	let error = $state<string | null>(null);
	let busy = $state(false);

	async function submit(e: SubmitEvent) {
		e.preventDefault();
		busy = true;
		error = await login(username, password);
		busy = false;
		if (!error) password = '';
	}
</script>

<div class="backdrop">
	<form class="modal" onsubmit={submit}>
		<h2>Admin sign-in</h2>
		<p>This action needs an admin session.</p>
		<label>
			Username
			<input bind:value={username} autocomplete="username" />
		</label>
		<label>
			Password
			<!-- svelte-ignore a11y_autofocus -->
			<input type="password" bind:value={password} autocomplete="current-password" autofocus />
		</label>
		{#if error}<p class="error">{error}</p>{/if}
		<button type="submit" disabled={busy || !password}>{busy ? 'Signing in…' : 'Sign in'}</button>
	</form>
</div>

<style>
	.backdrop {
		position: fixed;
		inset: 0;
		background: rgba(10, 10, 26, 0.55);
		display: grid;
		place-items: center;
		z-index: 100;
	}
	.modal {
		background: #fff;
		border-radius: 8px;
		padding: 1.5rem 1.75rem;
		width: min(20rem, 90vw);
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		box-shadow: 0 12px 40px rgba(0, 0, 0, 0.35);
	}
	h2 {
		margin: 0;
		font-size: 1.05rem;
	}
	p {
		margin: 0;
		font-size: 0.85rem;
		color: #555;
	}
	label {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		font-size: 0.8rem;
		color: #333;
	}
	input {
		padding: 0.45rem 0.6rem;
		border: 1px solid #ccc;
		border-radius: 4px;
		font-size: 0.9rem;
	}
	.error {
		color: #a22;
		font-size: 0.8rem;
	}
	button {
		padding: 0.5rem;
		border: none;
		border-radius: 4px;
		background: #1a1a2e;
		color: #fff;
		font-size: 0.9rem;
		cursor: pointer;
	}
	button:disabled {
		opacity: 0.6;
		cursor: default;
	}
</style>
