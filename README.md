# MeshCore POTA Gateway

You hike into a park, get the mast up, and the phone says no service. Without a spot, the pileup crawls. SOTAMAT means stopping the activation to run FT8 on the HF rig. APRS and Winlink gateways take their own setup. Asking the first hunter to spot you only works if they stay on frequency.

This gateway is the other path. A field operator uses a separate LoRa board and the MeshCore app, and posts one line into a room:

```text
SPOT KB3CMT US-0618 14.285 SSB
```

That post travels over the off-grid LoRa mesh to a gateway parked where there is internet: a house, an EOC, or a vehicle hotspot. The gateway HTTPS-POSTs the spot to [pota.app](https://pota.app). The HF radio stays on the air.

Independent tool from KB3CMT / [Get On The Air](https://getontheair.app/meshcore-pota/). Not affiliated with Parks on the Air or with MeshCore. Confirm every spot on [pota.app](https://pota.app).

Firmware bugs and gateway behavior: [open an issue](https://github.com/KB3CMT/meshcore-pota-gateway/issues). Protocol and stock-firmware bugs belong on [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore/issues).

## How a spot moves

```text
activator phone + companion  --LoRa-->  repeaters already on the mesh  --LoRa-->  this room  --Wi-Fi-->  api.pota.app
```

Two different jobs:

| Who | What they run |
|---|---|
| Activator in the park | A MeshCore companion (Bluetooth to the phone app is the usual pair). They log into the room and post a `SPOT` line. |
| Gateway host | A room-server board at the internet edge. It queues the line and POSTs it. Leave **repeat off**. This board is a destination, not a flood repeater. |

The room is a MeshCore BBS. Add it as a contact, log in with the room password, and post plain text. A `#pota` prefix on that same line is optional. There is no shared-channel secret.

A room post is encrypted to **one** room identity. A second gateway on the same frequency cannot decrypt it, so it will not double-post a spot it merely hears on the air.

## Post a spot

Log in with the **room** password. Then send:

```text
SPOT CALL PARK FREQ MODE comments
```

```text
SPOT KB3CMT US-0618 14.285 SSB Ohiopyle
SPOT W1AW K-1234 14285 CW
#pota SPOT W1AW US-1234 14.285 SSB
```

`K-1234` is rewritten to `US-1234`. Frequency may be MHz with a decimal (`14.285`, `146.52`) or integer kHz (`14285`). One park per line. A comma-separated 2-fer is rejected; post each park on its own line.

The callsign is required. `SPOT US-1234 14.285 SSB` is not a valid line.

WWFF and SOTA use the same room, and they POST to [parksnpeaks.org](https://parksnpeaks.org/api/) only after the gateway host has saved a valid ParksnPeaks user and API key:

```text
SPOT WWFF W1AW KFF-0001 7.144 SSB CQ WWFF
SPOT SOTA W1AW/P W1/GM-001 14.285 SSB CQ SOTA
```

POTA always goes to `api.pota.app`. With no ParksnPeaks key, WWFF and SOTA lines stay in the room log and are not forwarded.

## What the gateway will forward

Checks run **before** a spot is queued for HTTPS. The line still stays in the room log. A rejection is serial `[POTA] rejected …` and does not count against the rate limits. Rotating the room password is a way of maintaining who may post to the service.

| Check | Rule |
|---|---|
| Callsign | 3–15 characters, `A–Z` `0–9`, up to two `/`, at least one letter and one digit (`W1AW`, `W1AW/P`, `KH6/W1AW`) |
| POTA reference | 1–3 letters, hyphen, 4 or 5 digits (`US-1234`) |
| WWFF reference | prefix plus `FF-` and 4 digits (`KFF-0001`) |
| SOTA reference | association, `/`, 2-character region, `-`, 3 digits (`W1/GM-001`) |
| Frequency | 100 kHz through 1300 MHz |
| Mode | 2–8 letters or digits (`SSB`, `CW`, `FT8`) |
| Duplicate | Same program, callsign, reference, frequency, and mode inside 5 minutes |
| Per activator | 3 queued spots in any 10 minutes |
| Per node | 3 queued spots in any 10 minutes from the same room login (mesh public key) |
| Whole gateway | 20 queued spots in any hour (POTA, WWFF, and SOTA share this cap) |
| Block list | Admin `pota block CALL`. Up to 16 callsigns, kept across reboot. `W1AW` also matches `W1AW/P` and `KH6/W1AW` |

Two operators on different logged-in nodes each get their own 3-spot / 10-minute node cap. Rotating activator callsigns from one login does not bypass it. The per-activator cap still applies across nodes.

While Wi-Fi is down the outbound queue holds 6 spots. A newer `SPOT` displaces the oldest only when that bound is hit (serial `[POTA] queue full, dropped oldest …`). TLS runs off the LoRa loop, so the mesh keeps running during the POST.

## Way of service

One named live gateway per mesh edge is preferred. Park it where the internet already is. Leave repeat off. Keep the spare powered off. Share the room password with trusted operators, out of band, and rotate that password to revoke it.

| Practice | Why |
|---|---|
| Stand it up when activators have LoRa and no cell | Cell, pota.app, APSPOT, or SOTAMAT already cover the park otherwise |
| Name it so the job is obvious (`BSR-POTA`, `CLUB-POTA`) | Operators need one room to log into |
| Change the room password and the admin password before the first advert | Compile defaults exist so the board boots. They are not club secrets |
| Leave `repeat` off | Daily floods stay on the repeaters you already run. This node is the BBS plus the internet edge |
| One live room; spare named `…-POTA-SPARE` and powered off | Two live rooms on the same mesh double-spot only if someone logs into both and posts twice |
| Put it at a house, EOC, or vehicle hotspot | Wi-Fi on these boards can desense the LoRa radio. It is a poor choice for the only hop in the middle of the mesh |
| Keep the room password off web pages, Discord, and social posts | The password is the access control |

Skip it when you only need a normal repeater or a plain room, when you cannot supervise the room, or when another live `…-POTA` room is already the one operators use.

Operate the LoRa side under the amateur rules that apply to you. The gateway's Wi-Fi is an unlicensed internet path. This tool does not grant any Parks on the Air privilege. Full operator card: [getontheair.app guide](https://getontheair.app/guide/software/meshcore-pota/).

## Flash and first setup

Published image: `v1.17.1+` on branch `build/pota-all` (`a852559f`).

- Browser flasher, zip, and checklist: [getontheair.app/meshcore-pota](https://getontheair.app/meshcore-pota/)
- Boards: Heltec V3, Heltec V4, Heltec V4 R8, XIAO ESP32-S3 + SX1262
- Command reference: [examples/pota_gateway/SETUP.md](examples/pota_gateway/SETUP.md)

From this branch:

```bash
pio run -e Heltec_v3_room_server_pota -t upload -t monitor
pio run -e heltec_v4_room_server_pota -t upload -t monitor
pio run -e heltec_v4_r8_room_server_pota -t upload -t monitor
pio run -e Xiao_S3_WIO_room_server_pota -t upload -t monitor
```

Compile-default radio settings in upstream MeshCore are often EU (`869.618` MHz, bandwidth `62.5`, spreading factor `8`). Copy `get radio` from a working node on your mesh, `set radio` those values, and reboot, or the room will not be heard.

Before the first `advert`: set the name, change both passwords, confirm repeat is off. If the board has no saved Wi-Fi, it opens the setup AP `MeshCore-POTA-Gateway` at `http://192.168.4.1`. A phone hotspot is enough; leave cellular data on.

USB dry run, no second radio:

```text
room.post SPOT KB3CMT US-0001 14.235 SSB Testing MeshCore Gateway
```

Serial should show `[POTA] Queued` and then `[POTA] OK HTTP 200`. Then check pota.app.

Other roles from this same tree (companion, repeater, plain room server) are on the [test builds](https://getontheair.app/meshcore-test/) page. Flash a `_pota` image only on the gateway.

## Help test

This is new, and it needs operators with a spare board and a dead-cell park in range of a mesh. Around Southwestern Pennsylvania that includes places like Ohiopyle and the Monongahela National Forest, where the cell bars disappear and a gateway at a house or a weekend event can still see the mesh.

Useful reports: a spot that should have been accepted and was not, a spot that should have been rejected and was posted, queue behavior when Wi-Fi drops, and the anti-abuse limits under a real activation. File those on [Issues](https://github.com/KB3CMT/meshcore-pota-gateway/issues).

## This repository

This is KB3CMT's fork of [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore), published from `build/pota-all`. Upstream MeshCore is the mesh protocol and the stock firmware: [docs.meshcore.io](https://docs.meshcore.io), clients at [meshcore.io](https://meshcore.io).

| Topic | Where |
|---|---|
| Gateway firmware, queue, spot format, anti-abuse | [Issues on this repo](https://github.com/KB3CMT/meshcore-pota-gateway/issues) |
| MeshCore itself | [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore/issues) |
| Flash and operator card | [getontheair.app/meshcore-pota](https://getontheair.app/meshcore-pota/) |
| Other contact | [kb3cmt@gmail.com](mailto:kb3cmt@gmail.com) |

MeshCore is MIT licensed. See [license.txt](license.txt).

73, Curtis KB3CMT
