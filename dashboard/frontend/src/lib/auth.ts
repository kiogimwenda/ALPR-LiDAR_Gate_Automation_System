// auth.ts — the browser half of 4.10.2's JWT admin auth.
//
// The token lives in sessionStorage (survives a reload, dies with
// the tab — deliberately not localStorage: an admin session on a
// guard-booth machine shouldn't outlive the browsing session).
// api.ts attaches it to every request and flips `authRequired` on
// any 401, which pops the login modal; when the backend runs with
// auth disabled no 401 ever arrives and none of this renders.

import { writable } from 'svelte/store';

const TOKEN_KEY = 'gate.jwt';
const USER_KEY = 'gate.user';

const storage = typeof sessionStorage === 'undefined' ? undefined : sessionStorage;

export const authUser = writable<string | null>(storage?.getItem(USER_KEY) ?? null);
export const authRequired = writable(false);

export function token(): string | null {
	return storage?.getItem(TOKEN_KEY) ?? null;
}

export function onUnauthorized(): void {
	storage?.removeItem(TOKEN_KEY);
	storage?.removeItem(USER_KEY);
	authUser.set(null);
	authRequired.set(true);
}

export async function login(username: string, password: string): Promise<string | null> {
	try {
		const resp = await fetch('/api/auth/login', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ username, password })
		});
		const body = (await resp.json().catch(() => ({}))) as Record<string, unknown>;
		if (!resp.ok) {
			return (body.error as string) ?? `HTTP ${resp.status}`;
		}
		storage?.setItem(TOKEN_KEY, body.token as string);
		storage?.setItem(USER_KEY, body.user as string);
		authUser.set(body.user as string);
		authRequired.set(false);
		return null; // no error
	} catch (e) {
		return e instanceof Error ? e.message : 'network error';
	}
}

export function logout(): void {
	storage?.removeItem(TOKEN_KEY);
	storage?.removeItem(USER_KEY);
	authUser.set(null);
}
