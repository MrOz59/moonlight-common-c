# Moonlight Streaming Core Library: Hestia fork

This is [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)
with what the [Hestia](https://github.com/MrOz59/Hestia) clients need on top:
[Hermes](https://github.com/MrOz59/Hermes) protocol extensions, Apollo's
microphone passthrough and a few client options. Everything else is upstream's,
kept current by cherry-picking it onto the branches below.

## Branches

| Branch | Used by | What it is |
| --- | --- | --- |
| `master` | | Upstream's `master`, unchanged and kept in sync. |
| `hestia` | [Hestia](https://github.com/MrOz59/Hestia) (desktop) | Upstream plus the Hermes extensions, microphone passthrough and mouse wiggle option. |
| `hestia-android` | [Hestia-Android](https://github.com/MrOz59/Hestia-Android) | [Artemis'](https://github.com/ClassicOldSong/moonlight-common-c) commits, upstream cherry-picked on top, plus microphone passthrough and the mouse wiggle option. |
| `feature/pyrowave-codec` | | Work in progress: the PyroWave codec in the API and SDP. |
| `atomics`, `safestring` | | Upstream's own old branches, as they were when this was forked. |

The clients pin commits of `hestia` and `hestia-android`, so those branches only
ever move forward: nothing a client pins is rebased away.

## What the fork adds

### Hermes extensions (`hestia`)

- `LiSetPacketFeedbackEnabled(bool)` reports when each video packet arrived,
  for Hermes' `packet_feedback` extension (modelled on RFC 8888), on the
  control stream. Turn it on only when the host negotiated the extension; it is
  off by default.
- RTP reorder/FEC queue depth telemetry: `RTP_VIDEO_STATS` gains
  `currentQueueDepth`, `maxQueueDepth`, `totalQueueDepth` and
  `queueDepthSamples`, and `DECODE_UNIT` gains `rtpQueuePeakDepth`.

### Microphone passthrough (`hestia`, `hestia-android`)

Logan Abell's [microphone commits](https://github.com/logabell/moonlight-common-c-mic),
for hosts that offer a microphone stream (Apollo, and Hermes with "Stream the
client's microphone" on):

- Set `STREAM_CONFIGURATION.enableMic`. When the host's SDP offers
  `a=rtpmap:96 opus/48000/1`, the handshake sets up and plays `streamid=mic`,
  and connection stage `STAGE_MICROPHONE_STREAM_INIT` (12) opens it; `STAGE_MAX`
  is 13. A host that doesn't offer it streams as usual without it.
- Once connected, `LiIsMicrophoneStreamActive()` says whether the host took it.
  Send 20 ms Opus packets of 48 kHz audio with `LiSendMicrophoneOpusData()`, or
  `LiSendMicrophoneOpusDataEx()` for another frame duration.
- The packets are encrypted with the remote input key when the host asks for
  it (`SS_ENC_MICROPHONE`) or the client opts in with `ENCFLG_MICROPHONE`;
  `LiIsMicrophoneEncryptionEnabled()` says which.

### Client options (`hestia`, `hestia-android`)

- `LiSetMouseWiggleOnConnect(bool)`: whether the connection moves the mouse by
  one pixel and back once it is up, to wake a sleeping host display. On by
  default. A host streaming a session of its own, such as a Gamescope session,
  shows its cursor for that motion instead. Call before `LiStartConnection()`.

### Artemis (`hestia-android`)

- `LiSendExecServerCmd(uint8_t)` asks the host to run one of its server
  commands, on control channel `0x08`. `LiSendEmptyPayload()` sends a 4-byte
  filler packet on the same channel.

## Keeping up with upstream

Upstream's commits are cherry-picked with `-x`, so each one names the upstream
commit it came from, and that trailer is how to tell what a branch is missing.
(`git cherry` compares patches instead, so a commit that needed a conflict
resolved would show up again as missing.)

```bash
git remote add upstream https://github.com/moonlight-stream/moonlight-common-c.git
git fetch upstream

# Upstream itself: master only ever fast-forwards.
git push origin upstream/master:master

# A client branch: the upstream commits it has no cherry-pick of yet, in order.
branch=hestia
picked=$(git log --format=%b "$branch" | sed -n 's/^(cherry picked from commit \([0-9a-f]*\))$/\1/p')
git rev-list --reverse --no-merges "$(git merge-base "$branch" upstream/master)..upstream/master" |
    grep -vxF -f <(echo "$picked")

# Then pick them onto the branch.
git switch "$branch"
git cherry-pick -x <commits>
```

The same works for `hestia-android`, whose base is Artemis' older one; Artemis'
own repository is <https://github.com/ClassicOldSong/moonlight-common-c>. When an
upstream change alters an API the clients use, update the client in the same
step: the Android build lists its sources in `Android.mk`, and the desktop
client in `moonlight-common-c.pro`.

## Upstream

Moonlight-common-c contains the core GameStream client code shared between
[Moonlight](https://moonlight-stream.org) clients, including
[Moonlight PC](https://github.com/moonlight-stream/moonlight-qt),
[Moonlight Android](https://github.com/moonlight-stream/moonlight-android),
[Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios), and
[Moonlight Chrome](https://github.com/moonlight-stream/moonlight-chrome).

If you are implementing your own Moonlight game streaming client that can use a
C library, you probably want the code here.

### Note to Developers

Moonlight-common-c requires the _specific_ version of ENet that is bundled as a
submodule. This version has changes required for IPv6 compatibility and
retransmission reliability, among other things. These are breaking API/ABI
changes which make Moonlight-common-c incompatible with other versions of the
ENet library. Attempting to runtime link to another libenet library will cause
your client to crash when connecting to recent versions of GeForce Experience.
