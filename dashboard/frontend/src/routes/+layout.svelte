<script lang="ts">
	import { page } from '$app/state';
	import { onDestroy, onMount } from 'svelte';
	import { authRequired, authUser, logout } from '$lib/auth';
	import { connect, disconnect, streamConnected, upstream, wsConnected } from '$lib/live';
	import LoginModal from '$lib/LoginModal.svelte';

	let { children } = $props();

	// The live connection belongs to the layout: every page shares the
	// stores, and navigation must not tear the socket down.
	onMount(connect);
	onDestroy(disconnect);
</script>

<header>
	<div class="left">
		<h1>Gate Dashboard</h1>
		<nav>
			<a href="/" class:active={page.url.pathname === '/'}>Monitor</a>
			<a href="/allowlist" class:active={page.url.pathname === '/allowlist'}>Allowlist</a>
		</nav>
	</div>
	<div class="badges">
		<span class="badge" class:ok={$wsConnected}>backend {$wsConnected ? 'live' : 'offline'}</span>
		<span class="badge" class:ok={$streamConnected}>event stream {$streamConnected ? 'up' : 'down'}</span>
		<span class="badge" class:ok={$upstream}>server {$upstream ? 'up' : 'down'}</span>
		{#if $authUser}
			<button class="badge user" onclick={logout} title="sign out">{$authUser} ✕</button>
		{/if}
	</div>
</header>

{#if $authRequired}
	<LoginModal />
{/if}

{@render children()}

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
	.left {
		display: flex;
		align-items: baseline;
		gap: 1.5rem;
	}
	h1 {
		font-size: 1.1rem;
		margin: 0;
	}
	nav {
		display: flex;
		gap: 1rem;
	}
	nav a {
		color: #aab;
		text-decoration: none;
		font-size: 0.9rem;
	}
	nav a.active {
		color: #fff;
		font-weight: 600;
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
	.badge.user {
		background: #2b4a7a;
		border: none;
		color: #fff;
		font-family: inherit;
		cursor: pointer;
	}
</style>
