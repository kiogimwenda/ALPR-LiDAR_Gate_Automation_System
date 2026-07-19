// vite.config.ts — dev-server proxy sends /api and /ws to the Drogon
// backend, so development is same-origin and the backend's --cors-dev
// flag stays optional.
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	plugins: [sveltekit()],
	server: {
		proxy: {
			'/api': 'http://127.0.0.1:8080',
			'/ws': {
				target: 'ws://127.0.0.1:8080',
				ws: true
			}
		}
	}
});
