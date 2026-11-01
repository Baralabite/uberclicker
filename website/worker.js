export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/flash" || url.pathname === "/flash/") return Response.redirect("https://app.uberclicker.com/#install", 302);
    if (url.pathname === "/api/country") return Response.json({ country: request.cf?.country ?? null }, { headers: { "Cache-Control": "no-store" } });
    if (url.pathname === "/app" || url.pathname.startsWith("/app/")) return Response.redirect("https://app.uberclicker.com/#configure", 302);
    return env.ASSETS.fetch(request);
  }
};
