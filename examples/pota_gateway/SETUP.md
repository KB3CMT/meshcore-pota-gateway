# MeshCore POTA Gateway — Setup and First Spot

Public flash + zip: **https://getontheair.app/meshcore-pota/**  
Guide: **https://getontheair.app/guide/software/meshcore-pota/**

This Heltec V3 runs **room-server firmware with a POTA internet gateway**. Field operators log into the room over LoRa and post a `SPOT` line. The room queues that line and HTTPS-POSTs it to `https://api.pota.app/spot` on Wi-Fi. TLS runs on a background task so the LoRa stack keeps running.

It is **not** a group chat channel named `#pota`. MeshCore rooms are a BBS: you add the room as a contact, log in, and post text. Optional prefix `#pota SPOT …` is accepted in that post body; there is no shared-channel secret to configure.

Use **one live gateway** on a mesh. A second flashed board is a spare, not a second listener for the same post. See [Two gateway nodes](#two-gateway-nodes).

---

## What you need

- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
- USB cable and PlatformIO (`pio`) on the build machine
- This tree: MeshCore v1.17.1 with `examples/pota_gateway/`
- A phone or laptop for the Wi-Fi portal (`MeshCore-POTA-Gateway` / `192.168.4.1`)
- A MeshCore client (official app + companion radio, or another mesh node) **or** USB serial for a dry run
- The **same LoRa radio settings** as the rest of your mesh (frequency, bandwidth, spreading factor, coding rate)

Firmware compile defaults in this tree are **EU** (`869.618 MHz`, BW `62.5`, SF `8`). A US club mesh will not hear the room until you set radio params to match. Copy them from an existing repeater: `get radio`.

---

## 1. Flash

From the MeshCore repo root, with the Heltec on USB:

```bash
pio run -e Heltec_v3_room_server -t upload -t monitor
```

Wait for a serial line like:

```text
Room ID: A1B2C3D4...
[POTA] Wi-Fi STA starting; portal MeshCore-POTA-Gateway if no network in 12s
```

Leave the monitor open. Useful commands (USB serial, 115200 baud, send with Enter):

| Command | Purpose |
|---|---|
| `get radio` | Show `freq,bw,sf,cr` |
| `set radio 910.525,62.5,8,5` | Match mesh, then reboot (example — use *your* values) |
| `get name` | Advertised room name |
| `set name BSR-POTA` | Rename so operators can find it |
| `password YourAdminSecret` | Admin CLI password (default `password`) |
| `set guest.password YourRoomSecret` | Room login password (default `hello`) |
| `get repeat` | Must stay `off` for daily mesh (see below) |
| `pota` | Wi-Fi / queue status (`blk=` is the block-list size) |
| `pota block W1AW` | Drop later spots for that callsign (admin CLI) |
| `pota unblock W1AW` | Remove one callsign from the block list |
| `pota blocks` | List blocked callsigns |
| `advert` | Flood advert so clients can discover the room |
| `room.post SPOT …` | USB dry-run spot (no second node) |

After `set radio …` send `reboot`. After name or password changes, send `advert`.

---

## 2. Room setup (do this once)

These are the only room items that matter. Defaults are compiled in; change them before the room is public.

1. **Radio** — `get radio` on a working node of the same mesh, then `set radio <freq>,<bw>,<sf>,<cr>` on this Heltec and reboot.
2. **Name** — `set name BSR-POTA` (or similar). Clients see this in the advert. Default is `Heltec Room`.
3. **Room password** — `set guest.password …`. Operators use this to log in and post. Default `hello`.
4. **Admin password** — `password …`. Remote CLI / admin login. Default `password`.
5. **Repeat** — leave **off**. Stock room-server starts with forwarding disabled (`set repeat off`). This node is a BBS + internet gateway, not a repeater. Turning repeat on would rebroadcast other people’s floods and add airtime on a mesh that already has repeaters.
6. **Location** (optional) — `set lat …` / `set lon …` if you want the advert to show a park or EOC grid.

Read-only / wrong-password visitors cannot post. Guests with no room password are rejected unless you later `set allow.read.only on` (still cannot post spots).

---

## 3. Wi-Fi (internet path)

The radio comes up first. Wi-Fi is non-blocking.

- If saved credentials work, serial shows `[POTA] Wi-Fi <ip>` then `[PNP] portal http://<ip>/`. The Heltec OLED shows that DHCP address (and `192.168.4.1` while the first-setup AP is up). Open **`http://<that-ip>/`** on the same Wi-Fi (port 80) to set the ParksnPeaks user ID + API key. The page shows whether a key is **set**, never the key itself, and does **not** change Wi-Fi, so it will not hang the radio or USB serial. WWFF/SOTA spotting stays **off** until both look valid. Enter `OFF` in the key field to disable. A blank key on save keeps the stored key.
- If not, after about **25 seconds** the Heltec opens AP **`MeshCore-POTA-Gateway`**. Join it, open **`http://192.168.4.1`**, pick the field hotspot (phone tethering is fine), optionally fill ParksnPeaks, save, then leave that AP so the Heltec can join the hotspot. The SoftAP stops when STA is up. Cellular data on the phone must stay on. TLS connects by hostname `api.pota.app` (CloudFront SNI). Connecting by raw IP fails.
- `pota` on serial reports `WiFi <ip> q=0 busy=0 pnp=on|off`, or `portal 192.168.4.1`, or `WiFi down`.

Spots queue until Wi-Fi is up (up to 6 waiting: a newer `SPOT` drops the oldest only when the queue is full, with a serial log line). HTTPS does not run inside the LoRa receive callback.

Re-open the portal later by clearing saved Wi-Fi (erase NVS / `erase` then reflash identity if you also want a new Room ID — `erase` wipes prefs; use only if you mean it).

---

## 4. Join the room from the mesh

On a companion / phone / second node that is already on the same frequency:

1. Wait for or request an advert (`advert` on the gateway, or scan in the app).
2. Add **BSR-POTA** (or whatever you named it) as a **room / contact**.
3. Log in with the **room password** (not the admin password).
4. Send a **plain text post** (not a group-channel message):

```text
SPOT W1AW US-1234 14.285 SSB Test Activation
```

Accepted variants:

```text
SPOT W1AW K-1234 14.285 SSB Test Activation
#pota SPOT W1AW US-1234 14285 SSB Test Activation
SPOT WWFF W1AW KFF-0001 7.144 SSB CQ WWFF
SPOT SOTA W1AW/P W1/GM-001 14.285 SSB CQ SOTA
```

`K-1234` is rewritten to `US-1234` on **POTA** lines. A reference with `FF-` is treated as WWFF; a reference with `/` is treated as SOTA. Frequency may be MHz with a decimal (`14.285`, `146.52`) or integer kHz (`14285`). Mode is uppercased. Comments after mode are optional. One park reference per line (a comma-separated 2-fer is rejected).

The gateway drops a line **before** HTTPS when any of these fail. The text still stays in the room BBS. Serial shows `[POTA] rejected …`. Rejected lines do not count toward the rate limits.

| Check | Rule |
|---|---|
| Callsign | 3–15 characters, `A–Z` `0–9` and up to two `/`, at least one letter and one digit (`W1AW`, `W1AW/P`, `KH6/W1AW`) |
| POTA reference | 1–3 letters, hyphen, 4 or 5 digits (`US-1234`) |
| WWFF reference | prefix plus `FF-` and 4 digits (`KFF-0001`, `VKFF-1234`) |
| SOTA reference | association, `/`, 2-character region, `-`, 3 digits (`W1/GM-001`) |
| Frequency | 100 kHz through 1300 MHz after conversion |
| Mode | 2–8 letters or digits (`SSB`, `CW`, `FT8`) |
| Duplicate | Same program, callsign, reference, frequency, and mode inside 5 minutes |
| Per callsign | 3 queued spots per activator in any 10 minutes |
| Per node | 3 queued spots per logged-in room node in any 10 minutes |
| Whole gateway | 20 queued spots in any hour (POTA, WWFF, and SOTA share this cap) |
| Block list | Admin `pota block CALL`. `W1AW` also matches `W1AW/P` and `KH6/W1AW`. Up to 16 callsigns, kept in flash across reboot. USB serial or an admin login; the open Wi-Fi page cannot edit it. |

POTA always POSTs to pota.app. WWFF/SOTA POST to [parksnpeaks.org](https://parksnpeaks.org/api/) **only if** a valid PnP user + API key is saved on the portal (`pota` shows `pnp=on`). Get the key from your ParksnPeaks user options page. No key → those lines stay in the room BBS and serial says skipped.

Serial on the gateway should show:

```text
[POTA] Queued W1AW US-1234 14285 SSB
[POTA] POST {"activator":"W1AW",...}
[POTA] OK HTTP 200 W1AW @ US-1234 14285 SSB
```

Then check [pota.app](https://pota.app).

### Dry run with only the Heltec

USB serial, no second radio:

```text
pota
room.post SPOT W1AW US-1234 14.285 SSB USB dry run
```

Same queue and HTTP path as a live room post.

---

## 5. How this sits on the broader mesh

```text
  [phones / companion nodes]  --LoRa-->  [existing repeaters]  --LoRa-->  [this room + Wi-Fi]  -->  api.pota.app
                                          (daily mesh)                    (BBS, repeat OFF)
```

- **Repeaters keep doing daily traffic.** This firmware does not replace a repeater. Floods and DMs between other nodes still bounce on the boxes that already have `repeat on`.
- **The room is a destination**, not a hop. Login and posts are **encrypted to this node’s public key**. Neighbors may RF-hear the packets; they cannot decrypt them. Repeaters may forward those packets *toward this room* the same way they forward any other datagram. That is normal MeshCore, not a special POTA channel.
- **Airtime.** A spot is one short text post plus ACKs, same as any other room message. Wi-Fi/TLS is off the LoRa core. The gateway advertises like any room (zero-hop on boot, flood advert on a long interval). That is the only extra RF besides spots.
- **Wi-Fi on a Heltec V3** can slightly desense the SX1262 while the radio is transmitting on 2.4 GHz. Park the gateway at the internet edge (EOC, vehicle with a hotspot, house with Wi-Fi), not as the only RF path in the middle of the mesh.
- **Do not enable `set repeat on`** on the POTA Heltec unless you have deliberately decided this board is *also* a repeater. Mixing “internet gateway” and “busy flood repeater” on one ESP32 with Wi-Fi up is how you disrupt daily operation.

Normal room chat still works. POTA `SPOT` lines go to pota.app. WWFF/SOTA lines go to parksnpeaks.org only when `pnp=on`. Other posts stay in the BBS.

---

## Two gateway nodes

**They will not both “hear” one spot and double-post it.**

A room post is a **peer datagram for one identity**. If an operator logs into Room A and posts `SPOT …`, only Room A’s `storePost()` runs. Room B, even if it is ten feet away with the same firmware and the same Wi-Fi, cannot decrypt Room A’s traffic. RF “hearing” is not enough.

| Setup | What happens |
|---|---|
| One live POTA room, rest of mesh is repeaters / companions | One POST to POTA. This is the intended layout. |
| Two POTA rooms, operator logs into **one** | Only that room posts. The other is idle. |
| Two POTA rooms, operator logs into **both** and posts the same line twice | Two HTTP POSTs. pota.app shows a re-spot (same activator/park/freq, two timestamps). |
| USB `room.post` on both serial consoles | Same: two POSTs. |
| Second board flashed but not logged into, Wi-Fi off or portal only | No POTA traffic. Fine as a cold spare. |

Recommendations:

1. Run **one** internet-connected room named clearly (`BSR-POTA`). Tell operators that is the spotting room.
2. Keep the second Heltec as a **configured spare** (same radio settings, different name e.g. `BSR-POTA-SPARE`, Wi-Fi saved, room password set). Power it when the primary dies. Do not have operators log into both.
3. If you want geographic redundancy (two parks / two EOCs on **different meshes**), two live gateways are fine: they never see each other’s encrypted posts.
4. If you put two live rooms on the **same** mesh, give them different names and treat them as two BBSes. Spots are not automatically de-duplicated. POTA will accept both; hunters will see a double spot if someone posts to both.

There is no mesh-wide election and no “first node to hear it wins.” Encryption binds the post to the room you logged into.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Clients never see the room | Radio params not matching (`get radio`); send `advert`; check name |
| Login times out | Wrong **room** password; you used admin password |
| Post appears in the room but no `[POTA] Queued` | Line is not `SPOT …` / `#pota SPOT …`; guest/read-only cannot post |
| `[POTA] Queued` then `waiting for Wi-Fi` | Portal or hotspot; run `pota`; join `MeshCore-POTA-Gateway` |
| HTTP `401` / `403` | `api.pota.app/spot` rejected the post (session/JWT), or PnP API key/user is wrong |
| `[PNP] skipped` / not queued | No valid ParksnPeaks key; open `http://<sta-ip>/` (OLED shows the DHCP address) |
| `[POTA] rejected shape` | Call, reference, frequency, or mode failed the shape check |
| `[POTA] rejected duplicate` | Same spot inside 5 minutes |
| `[POTA] rejected call-rate` | That activator already has 3 spots in 10 minutes |
| `[POTA] rejected hourly` | Gateway already queued 20 spots this hour |
| `[POTA] rejected blocked` | Callsign is on `pota blocks` |
| HTTP `400` then `dropping spot` | Upstream rejected the park/freq/call after the local checks |
| Mesh feels busier after install | `get repeat` must be `off`; don’t flood-advert in a loop |

---

## First-spot checklist

1. Flash `Heltec_v3_room_server`, serial shows Room ID and `[POTA]`.
2. `set radio` to the club mesh; `reboot`.
3. `set name`, `set guest.password`, `password` (admin); `set repeat off`; `advert`.
4. Phone joins `MeshCore-POTA-Gateway` if needed; `pota` shows an IP.
5. USB: `room.post SPOT W1AW US-1234 14.285 SSB First spot` **or** log into the room from a client and post the same line.
6. Serial `OK HTTP 200` and the spot on pota.app.
