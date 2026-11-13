# EContact: setting it up

The README's front page links here; this is the whole of it.

## Getting started in full

Download the latest release from
**[Releases](https://github.com/whomper/EContact/releases/latest)**: each file is there on its own,
and all of them together in `ECONTACT-x.y.zip`.

| File | What it is |
|---|---|
| `ECONTACT.ACC` | the accessory, for every ST, STE, TT and Falcon |
| `ECONTACT.PRG` | the same as `ECONTACT.ACC`, to start from the desktop, with a menu bar |
| `TLS.OVL` | the Direct connection (TLS on a TT or Falcon, RSA and curves on the DSP), loaded only when used; an ST leaves it on the disk |
| `CACERT.PEM`, `ROOTS.DAT` | the root certificates for a Direct connection |
| `PIC.OVL` | a photo's colours worked out on a Falcon's DSP (the AERA family's file, as `TLS.OVL`), loaded on a TT or Falcon; without it the same photos, drawn slower |
| `ECONTACT.HYP` | the user guide for ST-Guide or EHypView: in EContact's folder, the Help key opens it |
| `ECONTACT.REF` | its pages' names, for ST-Guide's index (`ALL.REF`): goes with the .HYP |
| `ECONTACT.RSC`, `ECONTNV.RSC`, `ECONTACT.DEF`, `ECONTACT.ICN`, `ECONTMSK.ICN` | the desktop icon (on their own, and in the zip's `icons/` folder): `ECONTACT.RSC` (black and white for TOS 1 and 2; 256 colours, 16 and black and white for the colour desktops), `ECONTNV.RSC` (the same, its 256 colours on NVDI's own palette), `ECONTACT.DEF` (its names, for resource editors), `ECONTACT.ICN` and its mask `ECONTMSK.ICN` |
| `ECONTACT-x.y.zip` | all of the above |

**The desktop icon.** Copy the icons from `ECONTACT.RSC` into your desktop's icon file
(`DESKICON.RSC`, `DESKCICN.RSC`) with a resource editor; `ECONTACT.DEF` gives them their names there.
Take `ECONTACT.RSC` under TOS, EmuTOS, or NVDI with COLORVDI (TOS's palette loaded); under NVDI with
its own palette take `ECONTNV.RSC`, whose 256-colour image is drawn on NVDI's colours.
For EmuTOS, `tools/icon/add_to_emuicon.py emuicon.rsc EMUICON.RSC` writes a copy of EmuTOS's icon
file with EContact's added (it says which number), for the root of the boot drive; then
**Options > Install icon**. The icon is the family's (an address card, in 256 colours, TOS's 16
and black and white): `make icons` makes the files from AERA-Atari's drawing,
`common/icons/ECONTACT.txt`, with `common/tools/appicon.py`.

1. Copy `ECONTACT.ACC` to the root of your boot drive (e.g. `C:\`): TOS loads accessories only
   from there. (Until 1.63 an ST had `ECONTST.ACC`: delete it.)
2. Choose a folder for EContact, anywhere on any drive, and put the rest in it: `TLS.OVL`,
   `CACERT.PEM` and `ROOTS.DAT` for a Direct connection, `PIC.OVL` for the photos on a Falcon, `ECONTACT.HYP` and `ECONTACT.REF` for the Help key, and
   `ECONTACT.PRG` if you want the program. EContact keeps its address books there too. No folder
   has to exist beforehand, and none has a fixed name: the settings, `ECONTACT.INF` in the boot
   drive's root, say where it is in `FOLDER=`. Start `ECONTACT.PRG` from the folder once and it
   writes it there; or the first time you open the accessory it asks: **Locate...** (pick any file
   in the folder) or **Use C:** (the boot drive), which makes `C:\ECONTACT` for you.
3. For an ST or STE (or a Falcon or TT without a Direct connection), set up the Gateway on a Raspberry Pi
   from [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s own copy, at its latest release
   (the copy in this repository is for EContact's tests). On the Raspberry Pi:

   ```
   sudo apt install -y git                  (Raspberry Pi OS Lite only)
   git clone https://github.com/whomper/AERA-Gateway
   cd AERA-Gateway
   ./update-pi.sh
   sudo gateway/aera-gateway install dav --allow ATARI_IP
   ```

   Or the short way (AERA-Gateway 1.8 or later), every gateway in one line (EMail's too with
   `--mail IMAP SMTP`):

   ```
   sudo apt install -y git && git clone https://github.com/whomper/AERA-Gateway ~/AERA-Gateway &&
     ~/AERA-Gateway/update-pi.sh --allow ATARI_IP
   ```

   `ATARI_IP` is your Atari's address (`192.168.1.x`): the Gateway answers it alone. ECal uses the
   same Gateway: if it already works through it, there is nothing to do.

   - To update: `cd ~/AERA-Gateway && ./update-pi.sh` (its settings are kept).
   - To check: `sudo aera-gateway status` (`gateway/aera-gateway status` in the folder).
   - In EContact: **Settings > Connection**, **Gateway:** its address (`PI_IP`),
     **Port:** 8180.
4. Reboot, choose **EContact** in the Desk menu, and add your iCloud account with an
   app-specific password, not your Apple ID's own password (see [App passwords](#app-passwords-icloud-fastmail)).
5. Press **Help** in EContact for the guide (with EHypView or ST-Guide installed as an accessory);
   the [user guide](GUIDE.md) describes the rest.

**Updating:** copy the new release's files over the old ones (the accessory to the root, the rest
into EContact's folder) and reboot. The settings, the accounts and the copies of your address books
stay as they are.

**Needs:** TOS 1.04 to 4.x, EmuTOS, MagiC or MiNT; ST high, ST medium, TT or Falcon resolution; 1 MB
of RAM or more (a 512 KB ST can't run it: see [How many contacts](DETAILS.md#how-many-contacts)), and TT/Fast RAM
is used where there is some; STinG or MiNTnet; on an ST or STE, the AERA Gateway.

**Under TOS, put Atari's `FOLDR100.PRG` first in `AUTO`**, renamed for more room (`FOLDR400.PRG`).
TOS keeps an entry in a small internal pool for every folder a program or accessory has looked
into, and halts with "OUT OF INTERNAL MEMORY" when it runs out; FOLDRxxx makes the pool bigger.

**With STinG, set the Delay in `STING.CPX` to suit the machine:** 10 on a
Falcon or TT, 25 to 50 on an ST, as STinG's manual says. STinG looks at the network card that
often, so it sets the speed of every STinG program: on Erez's Falcon with a NetUSBee, Delay 10
took EFTP's downloads from 40 to 75 KB/s. On a NetUSBee, keep STinG's TCP receive window at 5840
(MSS 1460): the card holds about four full packets.

### App passwords (iCloud, Fastmail)

Cloud services refuse your normal password from a program like EContact (it says the server refused
the user name or password). Make an **app password** on the service's website instead, a password
for EContact alone, and type it into EContact's **Password** field in place of your normal one. You
can revoke it there at any time; your normal password and your other programs aren't touched.

- **iCloud** (an app-specific password): your Apple Account needs two-factor authentication. Sign
  in at account.apple.com, choose **Sign-In and Security**, then **App-Specific Passwords**, and make
  one. In EContact (Options > **Add account...** > **iCloud**) type your Apple Account's email address in
  **Apple ID** and the app-specific password in **Password**. Changing your Apple Account's password
  revokes them all: then make a new one and type it in again (right-click the account, **Account
  Settings...**). Apple's page: https://support.apple.com/en-us/102654
- **Fastmail** (Standard and Professional plans; Basic has no CardDAV): **Settings > Privacy &
  Security**, **Connected apps & API tokens**, **Manage app passwords and access**, **New app
  password**, with access that includes contacts (the default does). In EContact (**Other CardDAV
  account**): `https://carddav.fastmail.com/` in **Server**, your whole Fastmail address in **User
  name**, the app password in **Password**. Fastmail's page:
  https://www.fastmail.help/hc/en-us/articles/360058752854-App-passwords
- **Google Contacts**: an app password doesn't help. Google's CardDAV takes only an OAuth sign-in
  (https://developers.google.com/people/carddav: it supports no other way), which EContact can't
  do. Export your contacts as a .vcf file at contacts.google.com and import them into an On My Atari
  address book.

The [user guide](GUIDE.md#app-passwords-icloud-fastmail) has the same, step by step, and the
Help key shows it on the Atari.

## How it reaches the servers

Contacts servers want HTTPS. EContact uses ECal's code and ECal's way:

```
ST, STE:     Atari (ECONTACT.ACC) --plain HTTP, home LAN--> Gateway (ecalgate.py) -------HTTPS--> the server
Falcon, TT:  Atari (ECONTACT.ACC + TLS.OVL: TLS 1.2, RSA and curves on the DSP) ----------HTTPS--> the server
             (a Direct connection; or through the Gateway, as an ST)
```

- **Through the Gateway.** The AERA Gateway's `dav` service (`ecalgate.py`) serves EContact as it serves
  ECal, and `ECONTACT.ACC` can use it too, on any Atari. If ECal already works through your Gateway,
  type the same address and port in **Settings > Connection**.
- **A Direct connection.** TLS 1.2 on the Falcon or TT (a 68020 or better) with BearSSL, in
  `TLS.OVL`, the AERA family's one file (the guidelines 7.4): EContact loads it into its own memory
  the first time it is needed, about 140 KB, and an ST never does. On a Falcon the DSP56001 does the
  heavy sums: the server's RSA signature, and the elliptic curves of ECDSA certificates and of the
  key exchange (P-256, P-384, P-521, X25519), which take the 68030 tens of seconds. A signature
  once checked is remembered in `SIGS.DAT`, so the next start doesn't check it again (names, dates
  and the chain still are). A TT, which has no DSP, does it all on its 68030.

The gateway's own README: [gateway/README.md](../gateway/README.md).
