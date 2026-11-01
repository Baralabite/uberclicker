export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/app" || url.pathname.startsWith("/app/")) return Response.redirect("https://app.uberclicker.com/#configure", 302);
    return env.ASSETS.fetch(request);
  }
};
