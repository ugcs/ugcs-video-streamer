#include <ffmpeg.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wattributes"
extern "C" {
#   include <libavdevice/avdevice.h>
}
#pragma GCC diagnostic pop

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
    avdevice_register_all();

    int ret = 0;
    AVDictionary *options = nullptr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s input_file\n"
                "Streams video from a H.264 web camera.\n"
                "\n", argv[0]);
        exit(1);
    }
    const char *src_filename = argv[1];

    AVInputFormat *ifmt = av_find_input_format("video4linux2");
    if (!ifmt) {
        av_log(0, AV_LOG_ERROR, "Cannot find input format\n");
        exit(1);
    }

    AVFormatContext *fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_log(0, AV_LOG_ERROR, "Cannot allocate input format (Out of memory?)\n");
        exit(1);
    }

    // Framerate needs to set before opening the v4l2 device
    av_dict_set(&options, "framerate", "15", 0);
    // This will not work if the camera does not support h264.
    av_dict_set(&options, "input_format", "h264", 0);
    av_dict_set(&options, "video_size", "320x224", 0);

    // open input file, and allocate format context
    if (avformat_open_input(&fmt_ctx, src_filename, ifmt, &options) < 0) {
        av_log(0, AV_LOG_ERROR, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    // Retrieve stream information
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        av_log(0, AV_LOG_ERROR, "Could not find stream information\n");
        exit(1);
    }

    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        av_log(0, AV_LOG_ERROR, "Could not find video stream in input file '%s'\n", src_filename);
        return video_stream_idx;
    }

    // Dump input information to stderr
    av_dump_format(fmt_ctx, 0, src_filename, 0);

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

    ff::AvPacket pkt;

    // Read frames from the source
    while (!done) {
        ret = av_read_frame(fmt_ctx, &pkt.pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                continue;
            } else {
                break;
            }
        }
        if (pkt->stream_index != video_stream_idx) {
            pkt.Reset();
            continue;
        }
        if (VsFeedData(vs, pkt->data, pkt->size)) {
            av_log(0, AV_LOG_ERROR, "Failed to feed video packet\n");
            done = true;
        }
        pkt.Reset();
    }

    VsStop(vs);
    VsDestroy(vs);
    VsTerminate();

    avformat_close_input(&fmt_ctx);

    return 0;
}
