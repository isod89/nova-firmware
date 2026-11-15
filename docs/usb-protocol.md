# USB protocol

The radio speaks a plain text protocol over its USB serial port (USB CDC, any baud rate). One command per line,
fields separated by **TAB** (spaces are accepted too). Answers end with `END`, `OK`, `DONE` or `ERR`. This is what the
NOVA Editor and NOVA Installer use; you can use it from any serial terminal or script.

Opening the port must not toggle DTR/RTS: on the ESP32-S3, DTR low with RTS high resets the chip. When closing,
drop RTS before DTR.

```
   NOVA?                         -> NOVA <version>
   INFO                          -> INFO <key> <value> ... END
   STATUS                        -> STATUS freq mode rssi snr stereo volume battery% screen name text
   CFG GET                       -> CFG <key> <value> ... END
   CFG SET <key> <value>         -> OK <key> <value> | ERR <reason>
   CFG DEFAULTS                  -> OK            settings back to defaults (stations kept)
   ST LIST                       -> ST freq mode rssi snr flags name label ... END <n>
   ST BEGIN / ST+ ... / ST COMMIT-> READY / - / DONE <n>   replace the whole list
   BL LIST                       -> BL freq ... END <n>
   BL BEGIN / BL+ freq / BL COMMIT -> READY / - / DONE <n>
   TUNE <hz> <mode>              -> OK | ERR
   SCAN [parts speed]            -> OK, then SCANDONE <stations> when finished
   SCAN EST [parts speed]        -> EST <seconds> <channels>
   GB CAPS                       -> GBB band list, GBR rates, END
   GB STATUS                     -> GB active paused band hz mode rate dir step flutter gaps gate antenna volume elapsed steps marks trace rssi snr epoch
                                    content chaos timer blind lo hi left learning carriers bandLo bandHi
                                    react onMark window noise cluster trigger trigSens trigOpen holdLeft windowLeft trigState trigLevel trigBase trigOpenLeft
                                    catchT catchSnr holdSec holdKind catches
   GB PROFILES / PROFILE <n> | U<slot> | SAVE <slot>   GB HIST [i | CLEAR]   (marks kind: 0 response, 1 question, 2 RF trigger)
   GB START / STOP / PAUSE / RESUME -> OK
   GB MARK [Q]                   -> OK <n>   (Q = a question asked)
   GB SET <key> <value>          -> OK <key> <value>   (band rate dir step flutter gaps gate antenna content chaos timer blind lo hi react onMark window noise cluster trigger trigSens trigOpen catchT catchSnr holdSec)
   GB MARKS                      -> GBM n t hz rssi snr kind ... END
   GB TRACE <from>               -> GBT i t hz rssi snr (500 max) ... END next total oldest
   NH CAPS / STATUS / SPOTS / START / STOP / ZERO / MAX / QUIET / SPOT / SET band|hz|att v
   SD STATUS / MIN / EVENTS <from> / START / STOP / RESET / QUIET / LISTEN / MUTE / SET thresh|flash|hz v
   while a search runs:  NOVA? -> NOVA <version> SCAN <percent>
                         SCAN STOP -> OK (search stopped, results kept)
                         anything else -> BUSY
   SHOT [D]                      -> SHOT 320 170, Q<y> <base64 run-length rows>, END <rows>  (D: changed rows only)
   TIME <epoch>                  -> OK            set the clock (UTC)
   WIFI GET | SETHEX ssid pass | FORGET | TEST | EIBI   (WPROG stage n ... then WIFI OK ip rssi clock / DONE count / FAIL reason)
   EIBI BEGIN                    -> READY         start receiving eibi.txt lines
   <eibi.txt lines>              -> ACK <n>       every 100 lines (flow control)
   EIBI END                      -> DONE <count>  sort, save, use
```

Station records (`ST`): `freq` in Hz, `mode` (0 FM, 1 LSB, 2 USB, 3 AM, 4 CW), `rssi`, `snr`, `flags`
(1 favorite, 2 added by hand, 4 new, 8 renamed), `name` (up to 19 characters), `label` (free text kept by the editor).

Example session:

```
> NOVA?
< NOVA 2.0.0
> TUNE	9650000	3
< OK
> STATUS
< STATUS	9650000	3	40	22	0	38	75	1	World Radio Intl.	
```
