#include "Limelight-internal.h"
#include "PacketFeedback.h"

// One report under construction. The receive thread fills it, the periodic
// control thread drains it.
//
// Only received packets are observed here, so gaps are what the report says
// about loss: any sequence number inside the covered span that was never
// recorded is reported as not received. That is the same conclusion the host
// would draw, reached without waiting for a frame to fail.
static struct {
    bool active;
    uint16_t baseSequence;
    // One past the highest offset recorded, i.e. the covered span.
    uint16_t span;
    uint64_t referenceTimeUs;
    // 0 means the packet was never seen; otherwise the wire metric.
    uint16_t metrics[PF_MAX_METRICS];
} pending;

static PLT_MUTEX pendingMutex;
static uint16_t nextReportSequence;
static bool enabled;
static bool initialized;

void PfSetEnabled(bool value) {
    enabled = value;
}

bool PfIsEnabled(void) {
    return enabled;
}

void PfInitialize(void) {
    PltCreateMutex(&pendingMutex);
    memset(&pending, 0, sizeof(pending));
    nextReportSequence = 0;
    initialized = true;
}

void PfCleanup(void) {
    if (!initialized) {
        return;
    }

    PltDeleteMutex(&pendingMutex);
    initialized = false;
    enabled = false;
}

void PfRecordArrival(uint16_t sequenceNumber) {
    if (!enabled || !initialized) {
        return;
    }

    uint64_t nowUs = PltGetMicroseconds();

    PltLockMutex(&pendingMutex);

    if (!pending.active) {
        pending.active = true;
        pending.baseSequence = sequenceNumber;
        pending.referenceTimeUs = nowUs;
        pending.span = 0;
        memset(pending.metrics, 0, sizeof(pending.metrics));
    }

    // Signed 16-bit difference, which is what makes this correct across the
    // sequence number wrapping at 65535.
    int16_t delta = (int16_t)(sequenceNumber - pending.baseSequence);
    if (delta < 0) {
        // Older than this report's base: it belongs to a report already sent.
        // Reordering that far back is not worth reopening a report for.
        PltUnlockMutex(&pendingMutex);
        return;
    }

    if (delta >= PF_MAX_METRICS) {
        // The span would exceed what one report can carry. The pending report
        // is left for the sender to drain as-is, and this packet starts the
        // next one rather than being dropped.
        pending.baseSequence = sequenceNumber;
        pending.referenceTimeUs = nowUs;
        pending.span = 0;
        memset(pending.metrics, 0, sizeof(pending.metrics));
        delta = 0;
    }

    uint64_t offsetUs = nowUs - pending.referenceTimeUs;
    uint16_t offsetUnits = (uint16_t)(offsetUs / PF_TIME_UNIT_US);
    if (offsetUnits > PF_METRIC_OFFSET_MASK) {
        // The report has been open longer than an offset can express. Clamp
        // rather than wrap: a wrapped offset would read as a packet that
        // arrived early and invert the delay gradient it feeds.
        offsetUnits = PF_METRIC_OFFSET_MASK;
    }

    pending.metrics[delta] = PF_METRIC_RECEIVED_FLAG | offsetUnits;
    if (delta + 1 > pending.span) {
        pending.span = (uint16_t)(delta + 1);
    }

    PltUnlockMutex(&pendingMutex);
}

int PfBuildReport(uint8_t* buffer, int bufferLength) {
    if (!enabled || !initialized) {
        return 0;
    }

    PltLockMutex(&pendingMutex);

    if (!pending.active || pending.span == 0) {
        PltUnlockMutex(&pendingMutex);
        return 0;
    }

    uint16_t span = pending.span;
    int payloadLength = 10 + (span * 2);
    if (payloadLength > bufferLength) {
        // Cannot happen with a correctly sized caller buffer, but truncating a
        // report would produce one whose declared count does not match its
        // length, which the host rejects outright.
        PltUnlockMutex(&pendingMutex);
        return 0;
    }

    uint16_t reportSequence = nextReportSequence++;
    uint16_t baseSequence = pending.baseSequence;
    // The host only ever compares arrival offsets within one report, so this
    // clock never needs to agree with the host's on anything but its rate.
    uint32_t referenceTimeUs = (uint32_t)(pending.referenceTimeUs & 0xFFFFFFFF);

    buffer[0] = (uint8_t)(reportSequence >> 8);
    buffer[1] = (uint8_t)(reportSequence & 0xFF);
    buffer[2] = (uint8_t)(baseSequence >> 8);
    buffer[3] = (uint8_t)(baseSequence & 0xFF);
    buffer[4] = (uint8_t)(span >> 8);
    buffer[5] = (uint8_t)(span & 0xFF);
    buffer[6] = (uint8_t)((referenceTimeUs >> 24) & 0xFF);
    buffer[7] = (uint8_t)((referenceTimeUs >> 16) & 0xFF);
    buffer[8] = (uint8_t)((referenceTimeUs >> 8) & 0xFF);
    buffer[9] = (uint8_t)(referenceTimeUs & 0xFF);

    for (uint16_t index = 0; index < span; index++) {
        uint16_t metric = pending.metrics[index];
        buffer[10 + (index * 2)] = (uint8_t)(metric >> 8);
        buffer[10 + (index * 2) + 1] = (uint8_t)(metric & 0xFF);
    }

    // The report is consumed: the next packet opens a new one.
    pending.active = false;
    pending.span = 0;

    PltUnlockMutex(&pendingMutex);

    return payloadLength;
}
