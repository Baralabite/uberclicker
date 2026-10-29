import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import Ajv2020 from "ajv/dist/2020.js";
import {
  encodeFrames,
  Reassembler,
  DeviceSession,
  type Transport,
  type Config,
} from "../src/protocol";
const fixture = JSON.parse(
  readFileSync(
    new URL("../../contracts/fixtures/config-default.json", import.meta.url),
    "utf8",
  ),
) as Config;
const schema = JSON.parse(
  readFileSync(
    new URL("../../contracts/config.schema.json", import.meta.url),
    "utf8",
  ),
);
const validate = new Ajv2020({ strict: true }).compile(schema);
test("shared readable fixture validates and contains no secrets", () => {
  assert.ok(validate(fixture), JSON.stringify(validate.errors));
  assert.doesNotMatch(JSON.stringify(fixture), /password|token|pairKey/);
  const bad = structuredClone(fixture);
  (bad as any).password = "private";
  assert.equal(validate(bad), false);
});
test("cross-language frame fixture is byte exact", () => {
  const vector = JSON.parse(
    readFileSync(
      new URL(
        "../../contracts/fixtures/config-conformance.json",
        import.meta.url,
      ),
      "utf8",
    ),
  );
  assert.equal(
    Buffer.from(encodeFrames(vector.request, 0, 512)[0]).toString("hex"),
    vector.frameHex,
  );
});
test("all ATT sizes roundtrip multi-byte fragmented messages", () => {
  const q = { v: 2, id: 1, op: "validate", text: "é🦘".repeat(800) };
  for (const size of [20, 23, 64, 185, 512]) {
    const r = new Reassembler();
    let result: unknown;
    for (const f of encodeFrames(q, 5, size)) result = r.feed(f, 5, 100);
    assert.deepEqual(result, q);
  }
});
test("reject oversized, malformed, reordered, duplicate, interleaved, stale and invalid UTF8 fragments", () => {
  assert.throws(() => encodeFrames({ a: "x".repeat(8192) }, 0));
  assert.throws(() => encodeFrames({}, 0, 8));
  const fs = encodeFrames({ a: "test".repeat(100) }, 8);
  for (const alter of [
    "order",
    "duplicate",
    "stream",
    "timeout",
    "header",
    "size",
  ]) {
    const r = new Reassembler();
    r.feed(fs[0], 8, 100);
    assert.throws(() => {
      if (alter === "order") r.feed(fs[2], 8, 200);
      if (alter === "duplicate") r.feed(fs[0], 8, 200);
      if (alter === "stream") {
        const f = fs[1].slice();
        f[3] = 9;
        r.feed(f, 8, 200);
      }
      if (alter === "timeout") r.feed(fs[1], 8, 10100);
      if (alter === "header") r.feed(new Uint8Array([1, 2]), 8);
      if (alter === "size") {
        const f = fs[1].slice();
        f[7] = 33;
        r.feed(f, 8);
      }
    });
    const good = new Reassembler();
    assert.deepEqual(good.feed(encodeFrames({ ok: true }, 1, 512)[0], 1), {
      ok: true,
    });
  }
  const invalid = encodeFrames("x", 1, 512)[0];
  invalid[9] = 255;
  assert.throws(() => new Reassembler().feed(invalid, 1));
});
test("Apply validates before commit and binds expected revision", async () => {
  const calls: any[] = [];
  const transport: Transport = {
    request: async (op, args) => {
      calls.push({ op, args });
      if (op === "begin")
        return { session: "1234567890abcdef", config: fixture };
      if (op === "read") return { config: { ...fixture, revision: 1 } };
      return {};
    },
    disconnect: async () => {},
  };
  const d = new DeviceSession(transport),
    draft = await d.begin();
  draft.profiles[0].name = "My show";
  await d.apply(draft, { wifi: { op: "keep" }, pairs: [] });
  assert.deepEqual(
    calls.map((c) => c.op),
    ["begin", "validate", "apply", "read"],
  );
  assert.equal(calls[2].args.expectedRevision, 0);
  assert.equal(calls[1].args.config.profiles[0].name, "My show");
  assert.equal(d.session, undefined);
  assert.equal(d.config!.revision, 1);
});
test("validation failure never sends apply; Apply failure never retries automatically", async () => {
  for (const failure of ["validate", "apply"]) {
    const calls: string[] = [];
    const d = new DeviceSession({
      request: async (op) => {
        calls.push(op);
        if (op === "begin")
          return { session: "1234567890abcdef", config: fixture };
        if (op === failure)
          throw Error(failure === "validate" ? "INVALID" : "PERSIST_FAILED");
        return {};
      },
      disconnect: async () => {},
    });
    const c = await d.begin();
    await assert.rejects(() => d.apply(c, { wifi: { op: "keep" }, pairs: [] }));
    assert.equal(
      calls.filter((c) => c === "apply").length,
      failure === "apply" ? 1 : 0,
    );
    if (failure === "apply") assert.equal(d.session, undefined);
  }
});
test("cancel sends session; disconnect discards edit and readable state", async () => {
  let args: unknown;
  const d = new DeviceSession({
    request: async (op, a) => {
      args = a;
      return op === "begin"
        ? { session: "1234567890abcdef", config: fixture }
        : {};
    },
    disconnect: async () => {},
  });
  await d.begin();
  await d.cancel();
  assert.deepEqual(args, { session: "1234567890abcdef" });
  await d.begin();
  d.lost();
  assert.equal(d.session, undefined);
  assert.equal(d.config, undefined);
});
test("claim rejects malformed physical code and malformed owner token", async () => {
  let calls = 0;
  const d = new DeviceSession({
    request: async () => {
      calls++;
      return { token: "short" };
    },
    disconnect: async () => {},
  });
  await assert.rejects(() => d.claim("123"));
  assert.equal(calls, 0);
  await assert.rejects(() => d.claim("123456"));
  assert.equal(calls, 1);
});

test("runtime schema rejects bad device state and Unicode byte overflow", async () => {
  const { checkedConfig } = await import("../src/protocol");
  const missing = structuredClone(fixture);
  missing.activeProfile = 3;
  assert.throws(() => checkedConfig(missing));
  const network = structuredClone(fixture);
  network.profiles[0].output = "tcp";
  assert.throws(() => checkedConfig(network));
  const unicode = structuredClone(fixture);
  unicode.profiles[0].name = "🦘".repeat(12);
  assert.throws(() => checkedConfig(unicode));
  const d = new DeviceSession({
    request: async () => ({
      schemaVersion: 3,
      protocolVersion: 2,
      deviceId: "aabbccddeeff",
    }),
    disconnect: async () => {},
  });
  await assert.rejects(() => d.refresh());
});
test("published protocol docs stay identical", () => {
  assert.equal(
    readFileSync(
      new URL("../../contracts/config-protocol.md", import.meta.url),
      "utf8",
    ),
    readFileSync(
      new URL("../../docs/configuration-protocol.md", import.meta.url),
      "utf8",
    ),
  );
});

test("manual LAN transport refuses plaintext, embedded credentials and ambiguous URLs", async () => {
  const { protectedLanUrl, NativeLanTransport } = await import(
    "../src/transport"
  );
  assert.equal(
    protectedLanUrl("https://clicker.example.net:8443/"),
    "https://clicker.example.net:8443/config/v1",
  );
  for (const url of [
    "http://192.168.1.2",
    "https://user:password@example.net",
    "https://example.net/?token=secret",
    "https://example.net/#token",
    "file:///config",
  ])
    assert.throws(() => protectedLanUrl(url));
  assert.throws(() => new NativeLanTransport("https://example.net"));
});
test("browser credentials stay in memory and missing Web Bluetooth has a truthful fallback", async () => {
  const { credentials, bleSupported } = await import("../src/transport");
  Object.defineProperty(globalThis, "localStorage", {
    configurable: true,
    get() {
      throw Error("Persistent plaintext storage must not be accessed");
    },
  });
  Object.defineProperty(globalThis, "window", {
    configurable: true,
    value: { isSecureContext: true },
  });
  Object.defineProperty(globalThis, "navigator", {
    configurable: true,
    value: {},
  });
  assert.equal(bleSupported(), false);
  Object.defineProperty(globalThis, "navigator", {
    configurable: true,
    value: { bluetooth: {} },
  });
  assert.equal(bleSupported(), true);
  (globalThis.window as any).isSecureContext = false;
  assert.equal(bleSupported(), false);
  await credentials.set("test-device", "a".repeat(64));
  assert.equal(await credentials.get("test-device"), "a".repeat(64));
  await credentials.remove("test-device");
  assert.equal(await credentials.get("test-device"), undefined);
  delete (globalThis as any).window;
  delete (globalThis as any).localStorage;
});

test("expired validation and failed Cancel release the local edit for recovery", async () => {
  const d = new DeviceSession({
    request: async (op) => {
      if (op === "begin")
        return { session: "1234567890abcdef", config: fixture };
      throw Error("SESSION_EXPIRED");
    },
    disconnect: async () => {},
  });
  const c = await d.begin();
  await assert.rejects(() => d.apply(c, { wifi: { op: "keep" }, pairs: [] }));
  assert.equal(d.session, undefined);
  await d.begin();
  await assert.rejects(() => d.cancel());
  assert.equal(d.session, undefined);
});

test("v2 topology has separate management targets and cue selection with unique bounded peers", async () => {
  const { checkedConfig } = await import("../src/protocol");
  const c = JSON.parse(
    readFileSync(
      new URL("../../contracts/fixtures/config-multi-rx.json", import.meta.url),
      "utf8",
    ),
  ) as Config;
  assert.equal(checkedConfig(c).peers.length, 2);
  assert.equal(c.activePeer, 0);
  const duplicate = structuredClone(c);
  duplicate.peers[1].deviceId = duplicate.peers[0].deviceId;
  assert.throws(() => checkedConfig(duplicate));
  const rx = structuredClone(c);
  rx.role = "rx";
  assert.throws(() => checkedConfig(rx));
  const many = structuredClone(c);
  many.peers = Array.from({ length: 9 }, (_, i) => ({
    ...c.peers[0],
    deviceId: i.toString(16).padStart(12, "0"),
    pairId: i.toString(16).padStart(16, "0"),
  }));
  assert.throws(() => checkedConfig(many));
});
test("receiver proxy keeps identity, failures and mutations isolated from cue selection", async () => {
  const { ReceiverTransport } = await import("../src/protocol");
  const calls: any[] = [];
  const parent: Transport = {
    request: async (op, args) => {
      calls.push({ op, args });
      return {
        deviceId: "112233445566",
        response: { v: 2, id: 9, ok: true, result: { revision: 12 } },
      };
    },
    disconnect: async () => {
      throw Error("Must not disconnect shared Tx");
    },
  };
  const remote = new ReceiverTransport(parent, "112233445566");
  assert.deepEqual(
    await remote.request("apply", {
      session: "1234567890abcdef",
      expectedRevision: 11,
    }),
    { revision: 12 },
  );
  assert.equal(calls[0].op, "receiver");
  assert.deepEqual(calls[0].args, {
    deviceId: "112233445566",
    request: { op: "apply", session: "1234567890abcdef", expectedRevision: 11 },
  });
  await assert.rejects(() => remote.request("claim", { code: "123456" }));
  await remote.disconnect();
  const wrong = new ReceiverTransport(
    {
      request: async () => ({
        deviceId: "223344556677",
        response: { v: 2, id: 9, ok: true, result: {} },
      }),
      disconnect: async () => {},
    },
    "112233445566",
  );
  await assert.rejects(() => wrong.request("read"));
  const offline = new ReceiverTransport(
    {
      request: async () => ({
        deviceId: "112233445566",
        response: {
          v: 2,
          id: 9,
          ok: false,
          error: { code: "REMOTE_UNAVAILABLE" },
        },
      }),
      disconnect: async () => {},
    },
    "112233445566",
  );
  await assert.rejects(() => offline.request("read"), /REMOTE_UNAVAILABLE/);
});

test("durable Apply ID is saved before send and resolved after reconnect without replay", async () => {
  const { ReceiverTransport } = await import("../src/protocol");
  let persisted: string | undefined;
  const trace: string[] = [];
  let applies = 0;
  const config = { ...structuredClone(fixture), role: "rx" as const };
  const parent: Transport = {
    request: async (_op, args) => {
      const request = args!.request as any;
      let result: any = {};
      trace.push(request.op);
      if (request.op === "begin")
        result = { session: "1234567890abcdef", config };
      if (request.op === "apply") {
        applies++;
        assert.equal(persisted, request.transactionId);
        throw Error("REMOTE_TIMEOUT");
      }
      if (request.op === "result")
        result = {
          state: "applied",
          transactionId: request.transactionId,
          revision: 1,
        };
      if (request.op === "read")
        result = { config: { ...config, revision: 1 } };
      return {
        deviceId: "112233445566",
        response: { v: 2, id: 1, ok: true, result },
      };
    },
    disconnect: async () => {},
  };
  const journal = {
    load: async () => persisted,
    save: async (id: string) => {
      trace.push("persist");
      persisted = id;
    },
    clear: async () => {
      persisted = undefined;
    },
  };
  const first = new DeviceSession(
    new ReceiverTransport(parent, "112233445566"),
    journal,
  );
  const c = await first.begin();
  await assert.rejects(() =>
    first.apply(c, { wifi: { op: "keep" }, pairs: [] }),
  );
  assert.match(persisted!, /^[0-9a-f]{32}$/);
  assert.ok(trace.indexOf("persist") < trace.indexOf("apply"));
  const reconnected = new DeviceSession(
    new ReceiverTransport(parent, "112233445566"),
    journal,
  );
  await reconnected.restorePending();
  await assert.rejects(() => reconnected.begin(), /Resolve/);
  assert.equal((await reconnected.resolveApply()).state, "applied");
  assert.equal(applies, 1);
  assert.equal(persisted, undefined);
  assert.equal(reconnected.config!.revision, 1);
});
test("unknown or mismatched receipts never invent success or replay Apply", async () => {
  const { ReceiverTransport } = await import("../src/protocol");
  let saved: string | undefined = "e".repeat(32);
  let bad = false;
  const calls: string[] = [];
  const transport: Transport = {
    request: async (_op, args) => {
      const r = args!.request as any;
      calls.push(r.op);
      return {
        deviceId: "112233445566",
        response: {
          v: 2,
          id: 1,
          ok: true,
          result: {
            state: bad ? "applied" : "unknown",
            transactionId: bad ? "f".repeat(32) : r.transactionId,
            revision: 2,
          },
        },
      };
    },
    disconnect: async () => {},
  };
  const session = new DeviceSession(
    new ReceiverTransport(transport, "112233445566"),
    {
      load: async () => saved,
      save: async (id) => {
        saved = id;
      },
      clear: async () => {
        saved = undefined;
      },
    },
  );
  await session.restorePending();
  assert.equal((await session.resolveApply()).state, "unknown");
  assert.equal(saved, "e".repeat(32));
  bad = true;
  await assert.rejects(() => session.resolveApply(), /unknown/);
  assert.equal(saved, "e".repeat(32));
  assert.deepEqual(calls, ["result", "result"]);
  await session.discardPending();
  assert.equal(saved, undefined);
});

test("removing receiver membership never silently retargets cues", async () => {
  const { removeReceiver } = await import("../src/protocol");
  const c = JSON.parse(
    readFileSync(
      new URL("../../contracts/fixtures/config-multi-rx.json", import.meta.url),
      "utf8",
    ),
  ) as Config;
  assert.throws(
    () => removeReceiver(structuredClone(c), c.peers[0].deviceId),
    /explicitly/,
  );
  c.peers.push({
    ...c.peers[0],
    deviceId: "334455667788",
    pairId: "2122232425262728",
    name: "Third",
  });
  c.peers.push({
    ...c.peers[0],
    deviceId: "445566778899",
    pairId: "3132333435363738",
    name: "Fourth",
  });
  c.activePeer = 2;
  removeReceiver(c, "112233445566");
  assert.equal(c.peers[c.activePeer].deviceId, "334455667788");
  assert.equal(c.activePeer, 1);
});


test("updates only requests a local physical screen after authorization and edit resolution", async () => {
  const calls: string[] = [];
  let accepted = true;
  const d = new DeviceSession({
    request: async (op) => {
      calls.push(op);
      return { requested: accepted };
    },
    disconnect: async () => {},
  });
  await assert.rejects(() => d.openUpdateSession(), /physical setup/);
  d.config = structuredClone(fixture);
  d.status = {
    deviceId: "aabbccddeeff", boardId: "thinknode_m5", firmwareVersion: "0.1.0",
    schemaVersion: 2, protocolVersion: 2, revision: 0, setup: true, editing: false,
    claimed: true, capabilities: { ble_config: true, lan_config: false,
      setup_hotspot: false, outputs: [], ota_session: false },
  };
  await assert.rejects(() => d.openUpdateSession(), /physical setup/);
  d.status.capabilities.ota_session = true;
  d.session = "1234567890abcdef";
  await assert.rejects(() => d.openUpdateSession(), /current edit/);
  d.session = undefined;
  d.pendingApply = "a".repeat(32);
  await assert.rejects(() => d.openUpdateSession(), /current edit/);
  d.pendingApply = undefined;
  assert.deepEqual(calls, []);
  accepted = false;
  await assert.rejects(() => d.openUpdateSession(), /not accepted/);
  accepted = true;
  await d.openUpdateSession();
  assert.deepEqual(calls, ["updates", "updates"]);
  d.status.setup = false;
  await assert.rejects(() => d.openUpdateSession(), /physical setup/);
  assert.equal(calls.length, 2);
});
