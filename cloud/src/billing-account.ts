import { DurableObject } from "cloudflare:workers";
import type { Env } from "./index";
import { checkout, syncBilling, revokeDestination } from "./billing";

// Serialize a customer's checkout and webhook work so concurrent requests cannot create two subscriptions or overwrite newer billing state.
export class BillingAccount extends DurableObject<Env> {
  async sync(customer: string): Promise<void> {
    return this.ctx.blockConcurrencyWhile(() => syncBilling(this.env, customer));
  }
  async revoke(user: string, room: string): Promise<void> {
    return this.ctx.blockConcurrencyWhile(() => revokeDestination(this.env, user, room));
  }
  async checkout(user: { id: string; email: string }, interval: string, room: string): Promise<string | null> {
    return this.ctx.blockConcurrencyWhile(() => checkout(this.env, user, interval, room));
  }
}
