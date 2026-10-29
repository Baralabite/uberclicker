# UberClicker

An open-source, noncommercial LoRa presentation clicker project built around the Elecrow ThinkNode M5, released under the [MIT license](LICENSE). MIT permits commercial reuse; noncommercial describes this project’s operation. Device screens remain under design review. The website design is approved for public deployment; product features remain planned unless explicitly identified as validated.

- [Device interface proposal](docs/device-interface.md) and [interactive screen preview](device-preview.html)
- [Product plan and delivery gates](docs/product-plan.md)
- [Configuration, mobile app, and reset architecture](docs/configuration-architecture.md)
- [Website](https://baralabite.github.io/uberclicker/) (deployment verification pending), [prototype source](website/dist/index.html), and [website design notes](docs/website-design.md)
- [Deployment and custom-domain guide](docs/deployment.md)
- [Public repository](https://github.com/Baralabite/uberclicker)

The intended product supports a LoRa transmitter/receiver pair and standalone transmitter outputs for OSC, MIDI, UDP, and TCP. It removes GPS, uses quiet e-paper screens, and offers local configuration without an account. Native mobile apps, browser configuration, firmware releases, a USB browser flasher, and a printable charging dock remain implementation work.

The separate `gathermesh-lora` source project is a reference, is not included here, and remains untouched. Existing receiver BLE keyboard behavior is the starting point; USB keyboard support on the M5 hardware has not been validated.

To review the website locally, run from this repository’s root:

```sh
python3 -m http.server 8000 --directory website/dist
```

Then open [the local preview](http://localhost:8000). The device preview can be opened directly from its HTML file.

**Next gate:** review and approve the device interface. Website deployment is authorized; verification is pending. Firmware, mobile/browser configuration apps, firmware release CI, the functional USB flasher, and the measured charging dock remain pending. Custom-domain DNS has not been changed.
