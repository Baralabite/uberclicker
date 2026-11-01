import { betterAuth } from "better-auth";
import { drizzleAdapter } from "better-auth/adapters/drizzle";
import { magicLink } from "better-auth/plugins";
import { drizzle } from "drizzle-orm/d1";
import * as schema from "./schema";
import type { Env } from "./index";

export function auth(env: Env) {
  async function mail(to: string, subject: string, url: string) {
    if (env.EMAIL_ENABLED !== "true") throw new Error("Email sign-in is not available yet");
    await env.EMAIL.send({ from: { email: "login@uberclicker.com", name: "UberClicker" }, to, subject,
      text: `${subject}\n\n${url}\n\nIf you did not request this, ignore this email. Never share this link.` });
  }
  return betterAuth({
    appName: "UberClicker", baseURL: env.APP_ORIGIN, secret: env.BETTER_AUTH_SECRET,
    database: drizzleAdapter(drizzle(env.DB, { schema }), { provider: "sqlite", schema, transaction: false }),
    trustedOrigins: [env.APP_ORIGIN],
    emailAndPassword: { enabled: true, minPasswordLength: 12, requireEmailVerification: true,
      sendResetPassword: async ({ user, url }) => mail(user.email, "Reset your UberClicker password", url), revokeSessionsOnPasswordReset: true },
    emailVerification: { sendOnSignUp: true, sendOnSignIn: true, autoSignInAfterVerification: true,
      sendVerificationEmail: async ({ user, url }) => mail(user.email, "Verify your UberClicker email", url) },
    socialProviders: env.GOOGLE_CLIENT_ID && env.GOOGLE_CLIENT_SECRET ? { google: { clientId: env.GOOGLE_CLIENT_ID, clientSecret: env.GOOGLE_CLIENT_SECRET } } : {},
    account: { accountLinking: { enabled: true, trustedProviders: ["google"] } },
    session: { expiresIn: 60 * 60 * 24 * 7, updateAge: 60 * 60 * 24 },
    advanced: { ipAddress: { ipAddressHeaders: ["cf-connecting-ip"] } },
    plugins: [magicLink({ expiresIn: 600, storeToken: "hashed", sendMagicLink: async ({ email, url }) => mail(email, "Sign in to UberClicker", url) })],
  });
}
