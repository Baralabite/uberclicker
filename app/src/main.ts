import { capabilities, pageFor, type Page } from "./capabilities";
import { mountSetup } from "./setup";
import { mountAccount } from "./account";
import { mountReceiver } from "./receiver";
import { mountSender } from "./sender";
import "./style.css";
import { mountManual } from "./manual";

const screens: Record<Page, { label: string; className: string; mount: (root: HTMLDivElement) => void }> = {
  account: { label: "Cloud", className: "", mount: mountAccount },
  configure: { label: "Configure", className: "device-setup", mount: mountSetup },
  receive: { label: "Listen", className: "cloud-receiver", mount: mountReceiver },
  send: { label: "Cue", className: "", mount: mountSender },
};

function appChrome(publicView = false) {
  return `<div class="app-chrome"><header><a class="brand" href="${publicView ? "/" : ""}#${capabilities.home}"><svg class="brand-logo" viewBox="0 0 256 256" aria-hidden="true"><rect width="256" height="256" rx="52" fill="#2c50c8"/><path d="M112 68 44 128l68 60zm32 0v120l68-60z" fill="#fff"/></svg><span>UberClicker</span></a><button id="account-avatar" class="account-avatar" type="button" popovertarget="account-menu" aria-label="Account menu">U</button><div id="account-menu" class="account-popover" popover>${publicView ? '<a href="/#account">Open account</a>' : ""}</div></header>
  <nav class="app-nav" aria-label="UberClicker">${capabilities.pages.map(page => `<a href="${publicView ? "/" : ""}#${page}"${publicView && page === "account" ? ' aria-current="page"' : ""}>${screens[page].label}</a>`).join("")}</nav></div>`;
}

function mountApp() {
  const root = document.querySelector<HTMLDivElement>("#app")!;
  document.body.classList.toggle("desktop-layout", capabilities.configureDevices);
  root.innerHTML = `${appChrome()}
  ${capabilities.pages.map(page => `<div id="page-${page}" class="${screens[page].className}" hidden></div>`).join("")}<dialog id="profile-dialog" class="account-dialog" aria-labelledby="profile-title"></dialog>`;
  for (const page of capabilities.pages) screens[page].mount(root.querySelector<HTMLDivElement>(`#page-${page}`)!);

  function navigate() {
    const page = pageFor(location.hash, capabilities);
    const invite = page === "send" ? new URLSearchParams(location.hash.split("?")[1] ?? "").get("invite") : null;
    if (location.hash && location.hash !== `#${page}`) history.replaceState(null, "", `#${page}`);
    for (const name of capabilities.pages) {
      root.querySelector<HTMLElement>(`#page-${name}`)!.hidden = name !== page;
      const link = root.querySelector<HTMLAnchorElement>(`.app-nav a[href="#${name}"]`)!;
      if (name === page) { link.setAttribute("aria-current", "page"); link.scrollIntoView({ block: "nearest", inline: "nearest" }); }
      else link.removeAttribute("aria-current");
    }
    document.body.classList.toggle("sender-mode", page === "send");
    window.scrollTo(0, 0);
    window.dispatchEvent(new CustomEvent("uberclicker:navigate", { detail: { invite } }));
  }
  window.addEventListener("hashchange", navigate);
  navigate();

}
const manual = /^\/manual\/([a-f0-9]{32})\/?$/.exec(location.pathname);
if (manual) {
  const root = document.querySelector<HTMLDivElement>("#app")!;
  root.innerHTML = `${appChrome(true)}<div id="page-manual"></div>`;
  const avatar = root.querySelector<HTMLButtonElement>("#account-avatar")!;
  const menu = root.querySelector<HTMLElement>("#account-menu")!;
  menu.onbeforetoggle = () => { const bounds = avatar.getBoundingClientRect(); menu.style.top = `${bounds.bottom + 8}px`; menu.style.right = `${Math.max(16, innerWidth - bounds.right)}px`; };
  mountManual(root.querySelector<HTMLDivElement>("#page-manual")!, manual[1]);
}
else mountApp();
