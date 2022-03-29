#include <video_streamer_c_api.h>

namespace {

bool done = false;

void
LogFunc(Log::Level, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

void
OnStreamerStatus(Streamer::Status status, void *)
{
    fprintf(stderr, "Streamer status: %d\n", status);
    if (status != Streamer::Status::INITIALIZING && status != Streamer::Status::OPERATIONAL) {
        done = true;
    }
}

}

int
main(int argc, char **argv)
{
    VsInitialize(LogFunc);
    VsParams streamerParams;
    streamerParams.vehicleId = "SampleVehicle-1";
    streamerParams.targetUri = "urtp+connect://127.0.0.1:3341";
    VideoStreamerHandle vs = VsCreate(&streamerParams, OnStreamerStatus, nullptr);
    if (!vs) {
        av_log(0, AV_LOG_ERROR, "Streamer instance creation failed\n");
        return 1;
    }
    if (VsStart(vs)) {
        av_log(0, AV_LOG_ERROR, "Streamer instance starting failed\n");
        return 1;
    }

    std::freopen(nullptr, "rb", stdin);

    if(std::ferror(stdin)) {
        av_log(0, AV_LOG_ERROR, "Failed to reopen stdin: %s\n", std::strerror(errno));
        return 1;
    }

    std::size_t n;
    uint8_t buf[256];
    while(!done && (n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if(std::ferror(stdin) && !std::feof(stdin)) {
            av_log(0, AV_LOG_ERROR, "Failed to read stdin: %s\n", std::strerror(errno));
            break;
        }
        if (VsFeedData(vs, buf,n)) {
            av_log(0, AV_LOG_ERROR, "Failed to feed video packet\n");
            break;
        }
    }

    VsStop(vs);
    VsDestroy(vs);
    VsTerminate();

    return 0;
}
