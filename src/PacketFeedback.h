#pragma once

#include <stdbool.h>
#include <stdint.h>

// Hermes protocol extension: per-packet feedback, modelled on RFC 8888.
//
// The compatible feedback tells the host which frames its FEC could not
// repair. That shows damage after the fact and can never measure capacity:
// without knowing which packets arrived and when, a host can only infer
// congestion once it has already hurt the picture.
//
// This reports the arrival of individual packets so the host can measure the
// rate the path actually delivered, and see one-way delay trending up while a
// queue is still filling -- before it overflows into loss.
//
// Fields are big-endian, matching the other Sunshine/Hermes control messages.
//
// offset size field
// 0      2    report sequence, increments per report
// 2      2    base sequence, first packet covered
// 4      2    packet count covered by this report
// 6      4    reference time, receiver clock in microseconds
// 10     2*n  one metric per covered packet, in sequence order:
//               bit 15    received
//               bits 0-14 arrival offset from the reference time,
//                         in units of 64 us
#define HERMES_PACKET_FEEDBACK_PTYPE 0x5601

// A report never covers more than this many packets, which bounds both the
// message and the accumulator.
#define PF_MAX_METRICS 512

// Arrival offsets are carried in units of this many microseconds, which gives
// a report a span of about two seconds -- far longer than one worth sending.
#define PF_TIME_UNIT_US 64

#define PF_METRIC_RECEIVED_FLAG 0x8000
#define PF_METRIC_OFFSET_MASK 0x7FFF

// Enable or disable reporting for this connection. Disabled is the default and
// the only correct state unless the host negotiated the extension: a client
// must not reach an extension path by sending its message.
void PfSetEnabled(bool enabled);
bool PfIsEnabled(void);

void PfInitialize(void);
void PfCleanup(void);

// Called from the video receive thread for every packet that arrives.
void PfRecordArrival(uint16_t sequenceNumber);

// Serialize the pending report into buffer, if one is worth sending.
// Returns the payload length, or 0 when there is nothing to send.
// Called from the periodic control thread.
int PfBuildReport(uint8_t* buffer, int bufferLength);
