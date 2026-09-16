# Hitag S EM4100 writer provenance

The Hitag S transport in `hitags.c` and `hitags.h` is derived verbatim from
DarkFlippers/unleashed-firmware commit `49238e1bd3acfd09d202e12918d5d12073254cbd`
(PR #1148), which builds on the opt-in write-target design introduced by commit
`22ce1d9ae`. The source remains covered by the repository's GPL-3.0 license.

This integration deliberately packages the transport and its UI as an external
FAP. It does not add a protocol, symbol, scene, setting, or worker branch to the
base firmware. The application requires two physical OK confirmations before
writing pages 4 and 5 and labels the operation as unsafe for genuine Hitag S
tags. The upstream transport's frame/CRC/decoder self-test runs on every launch.

Hardware verification is still required on a known ID8268 clone. A successful
open-loop write only means both page transactions were transmitted; verify the
result by reading the tag as EM4100 in the standard 125 kHz RFID application.
