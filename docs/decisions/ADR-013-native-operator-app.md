# ADR-013: Native Operator App (.NET MAUI) Replaces the Web Dashboard

## Status
Accepted — supersedes [ADR-004](ADR-004-frontend-framework.md) (SvelteKit SPA).

## Date
2026-10-17

## Context

The operator surface was a SvelteKit single-page app served by
gate-dashboard. Three things changed:

- The project owner directed a **native, multi-platform app** for
  Android, iOS, macOS and Windows: guards carry phones and tablets, and
  site offices run Windows PCs and Macs.
- The interaction bar is Apple's WWDC motion principles: press feedback
  on touch-down, interruptible springs, direct manipulation with
  rubber-banding, velocity handoff, Reduce Motion, and size-dependent
  typography. The owner's design brief summarises them (the
  "apple-design" skill, 17 principles). Haptics and the OS Reduce Motion
  setting are native APIs, and a browser tab reaches them poorly or not
  at all.
- The required feature set grew to match a production ALPR console
  (MeruLPR, in service on site):
  - dashboard KPIs and charts;
  - a live feed;
  - reports and advanced search with CSV export;
  - dwell time and occupancy;
  - unreadable-plate review;
  - alerts with acknowledgement;
  - users with roles;
  - gate configuration;
  - data retention.

  That needed a backend history store regardless of the client.

### Options

1. **Keep the SPA, add a PWA shell.** It is cheapest. However, iOS PWAs
   have no haptics API and only CSS media queries for Reduce Motion, and
   there is no store or managed distribution.
2. **Flutter.** Truly native rendering, but a second language (Dart)
   beside a C++/C# stack, and Windows desktop is less mature.
3. **.NET MAUI.** One C# codebase for all four targets, with native
   controls and native APIs (haptics, keychain, share sheet,
   accessibility). Visual Studio is the team's IDE. Logic can live in a
   plain .NET library that is unit-tested on Linux CI.

## Decision

**.NET MAUI**, in `app/`:

| Project | Role |
|---|---|
| `GateControl.Core` (net10.0) | API + WebSocket clients, models, view models (CommunityToolkit.Mvvm), spring physics, display rules. No UI dependency; unit-tested. |
| `GateControl.App` (MAUI) | Pages, the motion engine on views, sheets, charts (Microsoft.Maui.Graphics), platform services. |
| `GateControl.Core.Tests` | xUnit: physics, API contract, view models, display rules. |

The app talks only to **gate-dashboard's HTTP + WebSocket API** (JWT,
roles). gate-server's gRPC stays inside the server, so the existing
security posture holds: mTLS, per-RPC roles (ADR-011 / access control),
and the dashboard as the single client-facing surface.

gate-dashboard gains a **HistoryStore** (SQLite) that persists every
decision, fault and command from the stream. It derives alerts, holds
users, roles, gate configuration and settings, and answers the
reporting endpoints.

## Consequences

- `dashboard/frontend/` (the SvelteKit SPA) and the `--www` serving path
  are removed. The browser is no longer an operator client. The API
  remains reachable for integrations.
- Release builds need the platform toolchains:
  - Android and Windows build on Windows;
  - iOS and Mac Catalyst need a Mac with Xcode for packaging and
    signing. CI builds Android and Windows (`.github/workflows/app.yml`);
    an Apple build needs a macOS runner and signing assets.
- Plain-HTTP LAN deployments are allowed through platform network
  policy:
  - Android: cleartext traffic;
  - iOS / macOS: `NSAllowsLocalNetworking`.

  Anything beyond the site LAN should put gate-dashboard behind TLS.
- Roles are enforced server-side. The app only hides controls a role
  cannot use, and never relies on hiding for security.
