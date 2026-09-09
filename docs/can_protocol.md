# Engine status wire protocol

Transport: Classical CAN, standard 11-bit identifier `0x100`, data frames only.
Payload length: exactly **5 bytes**. Extended frames and remote requests are
rejected by the decoder. Error frames are not telemetry and are also rejected.
The SocketCAN driver leaves CAN FD disabled and does not subscribe to error frames.

| Byte offset | Field | Wire representation | Valid range |
| --- | --- | --- | --- |
| 0–1 | Engine speed | Unsigned 16-bit, big-endian, 1 rpm per unit | 0–12,000 rpm |
| 2–3 | Coolant temperature | Signed 16-bit two's complement, big-endian, 1 C per unit | -40–215 C |
| 4 | Throttle position | Unsigned 8-bit, 1% per unit | 0–100% |

For example, 2500 rpm, 91 C, and 35% throttle encodes as:

```text
CAN ID: 0x100
Length: 5
Data:   09 C4 00 5B 23
```

Negative temperatures use two's complement: -40 C is `FF D8`. Temperature uses
two bytes so values above 127 C remain representable. Conversion explicitly
handles signed values without depending on host byte order, struct padding,
or implementation-defined unsigned-to-signed narrowing.

The encoder rejects out-of-range values. The decoder checks frame flags, ID,
length, and field ranges, in that order, before returning usable telemetry.
Bytes 5–7 of the in-memory frame are unused storage, not part of the wire payload.
The encoder zeros them; the decoder ignores them. No C++ struct is copied into
the payload.

This milestone has no sequence counter, protocol version field, application
checksum, or request-response messages. A future incompatible payload needs an
explicit protocol revision rather than silently changing these definitions.
