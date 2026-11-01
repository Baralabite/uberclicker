import {test} from 'node:test';
import assert from 'node:assert/strict';
import {SerialTransport} from '../src/serial';
test('serial frames direct requests, ignores boot logs, correlates replies and closes', async () => {
  let input!: ReadableStreamDefaultController<Uint8Array>;
  const sent: any[]=[];let lost=0,closed=0;
  const port={
    open: async (options:any)=>assert.equal(options.baudRate,115200),
    readable:new ReadableStream<Uint8Array>({start(c){input=c;}}),
    writable:new WritableStream<Uint8Array>({write(bytes){
      const line=new TextDecoder().decode(bytes);assert.ok(line.startsWith('UC:'));assert.ok(line.endsWith('\n'));
      const q=JSON.parse(line.slice(3));sent.push(q);
      const reply=`Boot log\nUC:null\nUC:{broken\nUC:{"v":2,"id":999,"ok":true,"result":{}}\nUC:${JSON.stringify({v:2,id:q.id,ok:true,result:{deviceId:'aabbccddeeff'}})}\n`;
      input.enqueue(new TextEncoder().encode(reply.slice(0,17)));input.enqueue(new TextEncoder().encode(reply.slice(17)));
    }}),
    close:async()=>{assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);++closed;},
  };
  const original=Object.getOwnPropertyDescriptor(globalThis,'navigator');
  Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});
  try{
    const t=await SerialTransport.discover(()=>++lost);
    assert.equal((await t.request('status')).deviceId,'aabbccddeeff');
    assert.deepEqual(sent.map(q=>q.op),['status']);
    await t.disconnect();assert.equal(lost,1);assert.equal(closed,1);
    await assert.rejects(t.request('read'),/Reconnect/);
  }finally{if(original)Object.defineProperty(globalThis,'navigator',original);else delete (globalThis as any).navigator;}
});


async function serialFixture() {
  let input!: ReadableStreamDefaultController<Uint8Array>;
  const requests: Record<string, unknown>[] = [];
  let closed = 0, lost = 0;
  const port = {
    open: async () => {},
    readable: new ReadableStream<Uint8Array>({ start(c) { input = c; } }),
    writable: new WritableStream<Uint8Array>({ write(bytes) {
      requests.push(JSON.parse(new TextDecoder().decode(bytes).slice(3)));
    } }),
    close: async () => {
      assert.equal(port.readable.locked, false);
      assert.equal(port.writable.locked, false);
      closed++;
    },
  };
  const original = Object.getOwnPropertyDescriptor(globalThis, "navigator");
  Object.defineProperty(globalThis, "navigator", {
    configurable: true, value: { serial: { requestPort: async () => port } },
  });
  const transport = await SerialTransport.discover(() => lost++);
  return {
    transport, requests, input,
    send: (reply: unknown) => input.enqueue(new TextEncoder().encode(`UC:${JSON.stringify(reply)}\n`)),
    async cleanup() {
      try {
        await Promise.all([transport.disconnect(), transport.disconnect()]);
        assert.equal(closed, 1);
        assert.equal(lost, 1);
      } finally {
        if (original) Object.defineProperty(globalThis, "navigator", original);
        else delete (globalThis as any).navigator;
      }
    },
  };
}

test("serial rejects device errors and malformed correlated replies without inventing success", async () => {
  const f = await serialFixture();
  try {
    for (const reply of [
      { ok: false, error: { code: "SESSION_EXPIRED" } },
      { ok: "yes", result: {} },
      { ok: true, result: [] },
    ]) {
      const request = f.transport.request("read");
      const rejected = assert.rejects(request, /SESSION_EXPIRED|Invalid serial response/);
      await new Promise<void>((resolve) => setImmediate(resolve));
      f.send({ v: 2, id: f.requests.at(-1)!.id, ...reply });
      await rejected;
    }
  } finally { await f.cleanup(); }
});

test("serial rejects pending requests on unplug and releases the port once", async () => {
  const f = await serialFixture();
  try {
    const request = f.transport.request("begin");
    const rejected = assert.rejects(request, /disconnected/);
    await assert.rejects(f.transport.request("read"), /wait/);
    f.input.close();
    await rejected;
  } finally { await f.cleanup(); }
});

test("serial bounds UTF-8 uploads and complete oversized replies", async () => {
  const f = await serialFixture();
  try {
    await assert.rejects(f.transport.request("validate", { text: "🦘".repeat(2100) }), /too large/);
    assert.equal(f.requests.length, 0);
    const request = f.transport.request("status");
    const rejected = assert.rejects(request, /too large/);
    f.send({ v: 2, id: 2, ok: true, result: { text: "🦘".repeat(2100) } });
    await rejected;
  } finally { await f.cleanup(); }
});
