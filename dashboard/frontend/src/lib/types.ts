// types.ts — the presentation schema, mirrored from the backend.
//
// The authoritative contract is dashboard/backend's json_mapping
// (pinned by tests/dashboard_api/); these types transcribe it for the
// compiler. Field names are camelCase, enums are proto short names,
// timestamps arrive as {ts: ISO-8601, tsMs: epoch millis}.

export interface Stamp {
	ts: string;
	tsMs: number;
}

export type GateState =
	| 'UNKNOWN'
	| 'CLOSED'
	| 'OPENING'
	| 'OPEN'
	| 'CLOSING'
	| 'FAULT'
	| 'LOCKDOWN'
	| string; // out-of-range enums render as the raw integer

export interface Telemetry {
	gateId: string;
	sent?: Stamp;
	state: GateState;
	limitOpen: boolean;
	limitClosed: boolean;
	beamClear: boolean;
	uptimeSec: number;
	rssiDbm: number;
	freeHeapBytes: number;
	minFreeHeap: number;
	supplyVoltage: number;
	coreTempC: number;
	fwVersion: string;
	seq: number;
}

export interface Decision {
	decisionId: string;
	decided?: Stamp;
	gateId: string;
	verdict: string;
	denyReason: string;
	matchedPlate: string;
	matchedClass: string;
	confidence: number;
	reason: string;
	actor: string;
}

export interface Fault {
	faultId: string;
	occurred?: Stamp;
	gateId: string;
	severity: string;
	code: string;
	description: string;
	context: Record<string, string>;
}

export interface Ota {
	commandId: string;
	bytesReceived: number;
	bytesTotal: number;
	phase: string;
}

export interface Command {
	commandId: string;
	issued?: Stamp;
	gateId: string;
	kind: string;
	actor: string;
}

export interface Ack {
	commandId: string;
	received?: Stamp;
	completedAt?: Stamp;
	completed: boolean;
	success: boolean;
	error: string;
	stateAfter: GateState;
}

export interface DashEvent {
	eventId: number;
	event?: Stamp;
	type: 'decision' | 'telemetry' | 'fault' | 'ota' | 'command' | 'ack' | 'unknown';
	decision?: Decision;
	telemetry?: Telemetry;
	fault?: Fault;
	ota?: Ota;
	command?: Command;
	ack?: Ack;
}

export interface GateSnapshot {
	telemetry: Telemetry;
	lastEvent?: Stamp;
}

export interface StatusResponse {
	gates: Record<string, GateSnapshot>;
	lastEventId: number;
	streamConnected: boolean;
	upstream: boolean;
}

export interface TimeWindow {
	startMinute: number;
	endMinute: number;
	daysMask: number;
}

export interface AllowlistEntry {
	plate: string;
	allowedClasses: string[];
	timeWindows: TimeWindow[];
	ownerName: string;
	ownerUnit: string;
	validFrom?: Stamp;
	validUntil?: Stamp;
	notes: string;
	addedBy: string;
	added?: Stamp;
}

// The POST body accepted by /api/allowlist (bridge fills the rest).
export interface AllowlistEntryInput {
	plate: string;
	ownerName?: string;
	ownerUnit?: string;
	notes?: string;
	allowedClasses?: string[];
	timeWindows?: TimeWindow[];
}

// Order matches the proto's VehicleClass enum (UNKNOWN omitted:
// "empty = any class allowed" is the UI default instead).
export const VEHICLE_CLASSES = [
	'PEDESTRIAN',
	'BICYCLE',
	'MOTORCYCLE',
	'SEDAN',
	'SUV',
	'PICKUP',
	'VAN',
	'TRUCK'
] as const;
