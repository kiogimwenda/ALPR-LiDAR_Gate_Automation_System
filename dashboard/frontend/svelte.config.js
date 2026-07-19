// svelte.config.js — SPA mode per ADR-004: static adapter with an
// index.html fallback so the Drogon backend (or any static server)
// can serve the built app same-origin; client-side routing handles
// the rest.
import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	kit: {
		adapter: adapter({
			fallback: 'index.html'
		})
	}
};

export default config;
