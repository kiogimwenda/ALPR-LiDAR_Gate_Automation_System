// SPA mode (ADR-004): no server-side rendering, no prerender — the
// static adapter emits index.html as a fallback and the app hydrates
// entirely client-side against the Drogon API.
export const ssr = false;
export const prerender = false;
