// Feed real Hermes-style payloads through the actual depacketizer, with
// network/FEC/decoder losses. No codec bitstream is parsed in this layer.
#include "../src/VideoDepacketizer.c"

int AppVersionQuad[4] = {7, 1, 450, -1};
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
int NegotiatedVideoFormat;
bool ReferenceFrameInvalidationSupported;

static int idrRequests, completed, submitted, successful, rejectFrame;
void LiRequestIdrFrame(void) { idrRequests++; }
void connectionDetectedFrameLoss(uint32_t start, uint32_t end) { (void)start; (void)end; }
void connectionReceivedCompleteFrame(uint32_t frame, bool ltr) { (void)frame; (void)ltr; completed++; }
void connectionSawFrame(uint32_t frame) { (void)frame; }
void notifyKeyFrameReceived(void) {}
bool LiGetCurrentHostDisplayHdrMode(void) { return false; }

static int submit(PDECODE_UNIT unit) {
    submitted++;
    // Padding must be removed; bytes resembling Annex B must remain intact.
    LC_ASSERT(unit->fullLength == 4);
    LC_ASSERT(unit->bufferList && !unit->bufferList->next);
    LC_ASSERT(unit->bufferList->length == 4);
    LC_ASSERT(unit->bufferList->data[0] == unit->frameNumber);
    LC_ASSERT(unit->bufferList->data[1] == 0 && unit->bufferList->data[2] == 0 && unit->bufferList->data[3] == 1);
    if (unit->frameNumber == rejectFrame) return DR_NEED_IDR;
    successful++;
    return DR_OK;
}

static void frame(unsigned index, bool markedIdr) {
    size_t capacity = sizeof(RTPV_QUEUE_ENTRY) + sizeof(NV_VIDEO_PACKET) + 8 + 4 + 7;
    char* allocation = calloc(1, capacity);
    LC_ASSERT(allocation);
    PLENTRY_INTERNAL entry = (PLENTRY_INTERNAL)allocation;
    entry->allocPtr = allocation;
    PNV_VIDEO_PACKET packet = (PNV_VIDEO_PACKET)(allocation + sizeof(RTPV_QUEUE_ENTRY));
    packet->streamPacketIndex = (index - 1) << 8;
    packet->frameIndex = index;
    packet->flags = FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA;
    unsigned char* bytes = (unsigned char*)(packet + 1);
    bytes[0] = 1;
    bytes[3] = markedIdr ? 2 : 1;
    bytes[4] = 12; // 8-byte header + exactly 4 bytes, despite packet padding
    bytes[8] = index;
    bytes[11] = 1;
    processRtpPayload(packet, sizeof(*packet)+8+4+7, index*16667, index*16667, index*1500, &entry);
    if (entry) free(entry->allocPtr);
}

static void scenario(int format, int loss, bool markedIdr) {
    NegotiatedVideoFormat = format;
    memset(&VideoCallbacks, 0, sizeof(VideoCallbacks));
    memset(&StreamConfig, 0, sizeof(StreamConfig));
    VideoCallbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
    VideoCallbacks.submitDecodeUnit = submit;
    idrRequests = completed = submitted = successful = 0;
    rejectFrame = loss == 2 ? 5 : 0;
    initializeVideoDepacketizer(1024);
    for (unsigned i = 1; i <= 10; ++i) {
        if (i == 5 && loss != 2) {
            if (loss == 1) notifyFrameLost(i, false);
            continue;
        }
        frame(i, markedIdr);
    }
    if (format == VIDEO_FORMAT_PYROWAVE) {
        LC_ASSERT(successful == 9);
        LC_ASSERT(submitted == (loss == 2 ? 10 : 9));
        LC_ASSERT(idrRequests == 0);
    } else if (loss == 2) {
        // AV1 still asks for an IDR when its decoder rejects a frame.
        LC_ASSERT(idrRequests > 0);
    }
    stopVideoDepacketizer();
    destroyVideoDepacketizer();
}

int main(void) {
    for (int marked = 0; marked <= 1; ++marked)
        for (int loss = 0; loss < 3; ++loss)
            scenario(VIDEO_FORMAT_PYROWAVE, loss, marked);
    scenario(VIDEO_FORMAT_AV1_MAIN8, 2, true);
    puts("PASS: 6 PyroWave packet/loss scenarios and AV1 recovery regression");
    return 0;
}
