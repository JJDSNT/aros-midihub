# AppleMIDI: where practice leaves the documents

Recorded on 2026-10-12, after the first runs against implementations that
are not ours showed two places where following the written word made our
packets unreadable to others. Each entry says what the documents say, what
the implementations do, how we know, and what our code does.

There is no specification of AppleMIDI as a whole. The pieces are:

- RFC 6295 (2011, replacing RFC 4695 of 2006), for the RTP payload and its
  recovery journal;
- Apple's own note, "MIDI Network Driver Protocol" (archived, last updated
  2016), which calls its packet format "a mostly conforming implementation
  of RFC 6295" and describes the session commands in a few pages;
- what implementations do, which is the only place some details exist.

## The RTP marker bit

**The documents.** RFC 4695 and RFC 6295, section 2.1, in the same words:
for native streams the M bit must be 1 when the command section has
commands and 0 otherwise. Apple's note has a table of the header's fields
with M given as 1 and the payload type as 0x61.

**The implementations.**

| Implementation | Sends | Takes | In its source |
|---|---|---|---|
| Benoit Bouchez's RTP-MIDI class (KissBox, UMP-OX) | 0 | only 0: a packet is taken when its first two bytes are `80 61` | No comment |
| rtpmidid | 0 | either (the bit is masked off) | "Here it SHOULD send 0x80 \| 0x61 if there is midi data sent, but if done, then rtpmidi for windows does not read messages, so for compatibility, just send 0x61" |
| Arduino-AppleMIDI-Library | 0 | either | The RFC's rule is there, commented out, with: "However, doing so on, MacOS does not take the given MIDI commands. Clear the M field" |
| node-rtpmidi | 0 | either | No comment |
| pymidi | — | either | Its header definition notes the bit as "always 0x1", which nothing else here bears out |

So three authors, independently, found that a packet with the bit set is
not read: by macOS, by rtpMIDI for Windows, and by the RTP-MIDI class,
whose source we read. The first two are their reports, not ours.

**What we could not establish.** What macOS and iOS themselves send. No
capture from an Apple device was at hand. That the RTP-MIDI class takes
only `80 61` and is sold in products used with Macs suggests Apple sends
the bit clear; that is an inference.

**What our code does.** Sends the bit clear always; takes either. It began
by sending and requiring it set, as Apple's table reads; was changed to
the RFC's rule; and was changed again when the RTP-MIDI class showed none
of our notes. With the bit clear that class shows them
(`tools/interop-bouchez-host.sh`).

**If this turns out wrong for some peer**, the remedy is a setting per
session. No peer that needs the bit set is known.

## The receiver feedback packet (RS)

**The documents.** RFC 6295 has no such packet: feedback is left to other
protocols. Apple's note gives its fields as the sender's SSRC and "the
sequence number of the most recently received packet", without sizes.

**The implementations.** The field after the SSRC is four bytes. The
RTP sequence number is sixteen bits.

| Implementation | Where the number is |
|---|---|
| Wireshark's dissector | First two bytes: "Apple includes a 32bit sequence-number, but the RTP-packet only specifies 16bit", and it reads the number from the first two |
| node-rtpmidi | First two bytes, read and written |
| Benoit Bouchez's RTP-MIDI class | First two bytes (seen on the wire: `bba60000` for sequence 0xbba6) |
| rtpmidid | Last two: it writes a 32-bit extended sequence number and takes the low sixteen bits of what it reads |

**What our code does.** Sends the number in the first two bytes; reads the
field both ways and takes the reading nearer behind the last sequence
number we sent (`applemidi_feedback_acked()`). Before that we wrote and
read the last two, and every peer of the first kind read our
acknowledgements as zero and we theirs: no journal was ever trimmed.

## Smaller things seen

- **The name in an invitation.** node-rtpmidi counts the terminating zero
  as part of the name it shows; ours ends at it. Harmless.
- **The recovery journal.** Apple's note: its driver "always transmits a
  recovery journal (J=1) but does not require one in received packets".
  Neither node-rtpmidi nor the RTP-MIDI class implements the journal, and
  the Arduino library never applies one. So among the implementations met,
  ours is the only one that repairs anything from it; whether Apple's
  agrees with ours chapter by chapter is untested.
- **The four undefined status bytes** (F4, F5, F9, FD): Apple's note says
  their behaviour is undefined. We do not send them.

## What would settle the open points

A capture of a session with a Mac or an iPhone: the marker bit in its
packets, the layout of its RS, and a journal of its making to feed our
receiver. That is the AppleMIDI backlog's AM06, still open for those peers.
