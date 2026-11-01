import { test } from "node:test";
import assert from "node:assert/strict";
import { browserEnvironment, capabilitiesFor, pageFor } from "../src/capabilities";

test("phones never expose USB setup or desktop receiving, including unsupported deep links", () => {
  for (const runtime of ["ios", "android"] as const) for (const webSerial of [false, true]) {
    const caps = capabilitiesFor(runtime, webSerial);
    assert.deepEqual(caps.pages, ["send", "account"]);
    assert.equal(caps.configureDevices, false);
    assert.equal(caps.receiveCloudClicks, false);
    assert.equal(caps.rememberRemoteDestinations, true);
    assert.equal(caps.accountInApp, false);
    assert.equal(pageFor("#configure", caps), "send");
    assert.equal(pageFor("#receive", caps), "send");
  }
});

test("desktop setup stays visible but requires a supported browser to enable controls", () => {
  const desktop = capabilitiesFor("desktop", false);
  assert.equal(desktop.accountInApp, true);
  assert.deepEqual(desktop.pages, ["send", "account", "install", "configure", "receive"]);
  assert.equal(pageFor("", desktop), "receive");
  assert.equal(pageFor("#configure", desktop), "configure");
  const browser = capabilitiesFor("browser", false);
  assert.deepEqual(browser.pages, ["send", "account", "install", "configure"]);
  assert.equal(browser.canConfigureDevices, false);
  assert.equal(pageFor("#configure", browser), "configure");
  assert.equal(pageFor("#receive", browser), "configure");
  assert.deepEqual(capabilitiesFor("browser", true).pages, ["send", "account", "install", "configure"]);
});

test("browser starts with a usable task and keeps channel management deep links", () => {
  const caps = capabilitiesFor("browser", true);
  assert.equal(pageFor("", caps), "configure");
  assert.equal(pageFor("#account", caps), "account");
});


test("mobile browsers never expose setup, including tablets requesting desktop websites", () => {
  for (const env of [
    browserEnvironment("Mozilla/5.0 (iPhone) CriOS/140.0 Mobile", "iPhone", 5),
    browserEnvironment("Mozilla/5.0 (Linux; Android 15) Chrome/140.0", "Linux", 5),
    browserEnvironment("Mozilla/5.0 (Macintosh) Safari/605.1", "MacIntel", 5),
    browserEnvironment("Chrome/140.0", "Linux", 0, true),
  ]) {
    const caps = capabilitiesFor("browser", true, env);
    assert.deepEqual(caps.pages, ["send", "account", "install"]);
    assert.equal(caps.canConfigureDevices, false);
    assert.equal(pageFor("#configure", caps), "send");
  }
});

test("desktop Chrome enables USB; other desktop browsers show disabled setup", () => {
  for (const [ua, supported] of [["Chrome/140.0 Safari/537.36", true], ["Firefox/140.0", false], ["Version/18.0 Safari/605.1", false], ["Chrome/140.0 Edg/140.0", false]] as const) {
    const caps = capabilitiesFor("browser", true, browserEnvironment(ua, "MacIntel", 0));
    assert.equal(caps.configureDevices, true);
    assert.equal(caps.canConfigureDevices, supported);
  }
  assert.equal(capabilitiesFor("desktop", false).canConfigureDevices, true);
});


test("signed clicker links land on the remote on every platform", () => {
  for (const runtime of ["ios", "android", "browser", "desktop"] as const) assert.equal(pageFor("#send?invite=example", capabilitiesFor(runtime, true)), "send");
});

test("Install is available in Companion and the web app, including mobile download access", () => {
  for (const runtime of ["browser", "desktop", "ios", "android"] as const) {
    const caps = capabilitiesFor(runtime, false);
    assert.equal(caps.pages.includes("install"), runtime === "browser" || runtime === "desktop");
    assert.equal(pageFor("#install", caps), runtime === "browser" || runtime === "desktop" ? "install" : caps.home);
  }
});
