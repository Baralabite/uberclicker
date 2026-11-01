import type { CapacitorConfig } from "@capacitor/cli";
const config: CapacitorConfig = {
  appId: "com.uberclicker.app",
  appName: "UberClicker",
  webDir: "dist",
  ios: { contentInset: "never" },
  plugins: { SystemBars: { insetsHandling: "css", style: "LIGHT" } },
};
export default config;
