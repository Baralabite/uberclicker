import { checkDevice, sha256 } from './validate.mjs';

// All bytes must have passed verifyRelease before this object is constructed.
export class FlashSession {
  constructor(release, loader, transport) {
    Object.assign(this, { ...release, loader, transport, ready: false });
  }
  async connect(mode, eraseConsent) {
    if (!['preserve', 'erase'].includes(mode) || (mode === 'erase' && !eraseConsent)) throw new Error('Full erase needs explicit consent.');
    await this.loader.main();
    checkDevice(this.loader.chip.CHIP_NAME, await this.loader.detectFlashSize());
    if (mode === 'preserve') {
      const table = this.manifest.images.find(i => i.file === 'partitions.bin');
      const current = await this.loader.readFlash(table.offset, table.size);
      if (await sha256(current) !== table.sha256) throw new Error('Partition layout differs. Preserve mode cannot proceed. Review full erase consequences before changing mode.');
    }
    this.mode = mode;
    this.ready = true;
  }
  async write(reportProgress) {
    if (!this.ready) throw new Error('Connect and verify compatibility first.');
    this.ready = false;
    await this.loader.writeFlash({ fileArray: this.files, flashMode: 'keep', flashFreq: 'keep',
      flashSize: 'keep', eraseAll: this.mode === 'erase', compress: true, reportProgress });
    for (const image of this.manifest.images) {
      const bytes = await this.loader.readFlash(image.offset, image.size);
      if (await sha256(bytes) !== image.sha256) throw new Error(`Read-back verification failed: ${image.file}. Reconnect and reinstall.`);
    }
    await this.loader.after('hard_reset');
  }
  invalidate() { this.ready = false; }
  async disconnect() {
    this.invalidate();
    await this.transport.disconnect();
  }
}
