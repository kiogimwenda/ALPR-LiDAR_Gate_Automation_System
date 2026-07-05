// api.ts — typed REST calls to the Drogon backend.
//
// Every helper resolves to {ok, error?} instead of throwing: the
// callers are UI event handlers, and a fetch failure is a state to
// render, not an exception to forget to catch. Backend errors arrive
// as {"error": …} with a meaningful HTTP status (grpc_http.hpp).

import type { Ack, AllowlistEntry, AllowlistEntryInput } from './types';

interface Result<T> {
	ok: boolean;
	error?: string;
	data?: T;
}

async function request<T>(path: string, init?: RequestInit): Promise<Result<T>> {
	try {
		const resp = await fetch(path, {
			headers: { 'Content-Type': 'application/json' },
			...init
		});
		const body = (await resp.json().catch(() => ({}))) as Record<string, unknown>;
		if (!resp.ok) {
			return { ok: false, error: (body.error as string) ?? `HTTP ${resp.status}` };
		}
		return { ok: true, data: body as T };
	} catch (e) {
		return { ok: false, error: e instanceof Error ? e.message : 'network error' };
	}
}

export function issueCommand(gateId: string, kind: string): Promise<Result<{ ack: Ack }>> {
	return request(`/api/gates/${encodeURIComponent(gateId)}/command`, {
		method: 'POST',
		body: JSON.stringify({ kind })
	});
}

export function listAllowlist(): Promise<Result<{ entries: AllowlistEntry[] }>> {
	return request('/api/allowlist');
}

export function upsertAllowlist(
	entry: AllowlistEntryInput
): Promise<Result<{ inserted: number; updated: number }>> {
	return request('/api/allowlist', { method: 'POST', body: JSON.stringify(entry) });
}

export function deleteAllowlist(
	plate: string
): Promise<Result<{ inserted: number; updated: number }>> {
	return request(`/api/allowlist/${encodeURIComponent(plate)}`, { method: 'DELETE' });
}
