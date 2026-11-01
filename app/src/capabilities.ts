import { Capacitor } from "@capacitor/core";
import { desktop } from "./native";
import { SerialTransport } from "./serial";

export type Runtime = "browser" | "desktop" | "ios" | "android";
export type Page = "account" | "configure" | "receive" | "send" | "install";
export type Capabilities = {
  configureDevices: boolean;
  canConfigureDevices: boolean;
  receiveCloudClicks: boolean;
  sendCloudClicks: boolean;
  accountInApp: boolean;
  rememberRemoteDestinations: boolean;
  pages: Page[];
  home: Page;
};

export function browserEnvironment(userAgent: string, platform: string, touchPoints: number, mobileHint = false) {
  return {
    mobile: mobileHint || /Android|iPhone|iPad|iPod|Mobile/i.test(userAgent) || (platform === "MacIntel" && touchPoints > 1),
    chrome: /Chrome\//.test(userAgent) && !/Edg\/|OPR\//.test(userAgent),
  };
}

export function capabilitiesFor(runtime: Runtime, webSerial: boolean, browser = { mobile: false, chrome: true }): Capabilities {
  const configureDevices = runtime === "desktop" || (runtime === "browser" && !browser.mobile);
  const canConfigureDevices = runtime === "desktop" || (configureDevices && webSerial && browser.chrome);
  const receiveCloudClicks = runtime === "desktop";
  const phone = runtime === "ios" || runtime === "android";
  return {
    configureDevices, canConfigureDevices, receiveCloudClicks, sendCloudClicks: true,
    accountInApp: runtime === "browser" || runtime === "desktop", rememberRemoteDestinations: phone,
    pages: ["send", "account", ...(runtime === "browser" || runtime === "desktop" ? ["install" as const] : []), ...(configureDevices ? ["configure" as const] : []), ...(receiveCloudClicks ? ["receive" as const] : [])],
    home: receiveCloudClicks ? "receive" : configureDevices ? "configure" : "send",
  };
}

export function pageFor(hash: string, capabilities: Capabilities): Page {
  const requested = hash.replace(/^#/, "").split("?")[0] as Page;
  return capabilities.pages.includes(requested) ? requested : capabilities.home;
}

const platform = Capacitor.getPlatform();
const browser = typeof navigator === "undefined" ? { mobile: false, chrome: false } : browserEnvironment(navigator.userAgent, navigator.platform, navigator.maxTouchPoints, (navigator as Navigator & { userAgentData?: { mobile: boolean } }).userAgentData?.mobile);
export const capabilities = capabilitiesFor(desktop ? "desktop" : platform === "ios" || platform === "android" ? platform : "browser", SerialTransport.supported(), browser);
