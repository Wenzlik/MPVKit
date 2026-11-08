// Post-build check of this fork's FFmpeg: skip_attachments option, eac3
// encoder, yadif_videotoolbox filter and the version stamp.
// Usage: verify <expected av_version_info> <fixture.mkv>...
// Built and run by verify.sh against the macOS slice of the xcframeworks.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/crc.h>
#include <libavutil/macros.h>
#include <libavutil/opt.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define IO_BUF 4096

// lo/hi: the Attachments payload; while track is set, reads are checked
// against it: payload_read = attachment bytes read, starts_inside = reads
// that began inside the payload (a read that began before it can carry at
// most IO_BUF bytes of it into the buffer; that is data read anyway).
typedef struct {
    FILE *f;
    int64_t bytes, lo, hi, payload_read;
    int track, starts_inside;
} Src;

static int src_read(void *opaque, uint8_t *buf, int size)
{
    Src *s = opaque;
    int64_t off = ftello(s->f);
    size_t n = fread(buf, 1, size, s->f);
    s->bytes += n;
    if (s->track && n) {
        s->payload_read += FFMAX(0, FFMIN(off + (int64_t)n, s->hi) - FFMAX(off, s->lo));
        s->starts_inside += off >= s->lo && off < s->hi;
    }
    return n ? (int)n : AVERROR_EOF;
}

static int64_t vint(const uint8_t *b, int64_t *pos, int keep_marker)
{
    int n = 1;
    while (n < 8 && !(b[*pos] & (0x80 >> (n - 1))))
        n++;
    int64_t v = keep_marker ? b[*pos] : b[*pos] & (0xFF >> n);
    for (int i = 1; i < n; i++)
        v = v << 8 | b[*pos + i];
    *pos += n;
    return v;
}

// [lo, hi) = payload of the Segment's Attachments element (top-level walk).
static int attachments_range(const char *path, int64_t *lo, int64_t *hi)
{
    FILE *f = fopen(path, "rb");
    int64_t pos = 0;
    int in_segment = 0, found = 0;

    while (f && !found && !fseeko(f, pos, SEEK_SET)) {
        uint8_t b[16] = { 0 };
        int64_t p = 0, id, size;
        if (fread(b, 1, sizeof(b), f) < 2)
            break;
        id = vint(b, &p, 1);
        size = vint(b, &p, 0);
        if (!in_segment && id == 0x18538067) {
            in_segment = 1;                 // descend into the Segment
            pos += p;
        } else if (in_segment && id == 0x1941A469) {
            *lo = pos + p;
            *hi = *lo + size;
            found = 1;
        } else
            pos += p + size;
    }
    if (f)
        fclose(f);
    return found;
}

static int64_t src_seek(void *opaque, int64_t off, int whence)
{
    Src *s = opaque;
    if (whence == AVSEEK_SIZE) {
        int64_t cur = ftello(s->f), size;
        fseeko(s->f, 0, SEEK_END);
        size = ftello(s->f);
        fseeko(s->f, cur, SEEK_SET);
        return size;
    }
    return fseeko(s->f, off, whence & ~AVSEEK_FORCE) ? -1 : ftello(s->f);
}

typedef struct {
    int64_t header_bytes;       // bytes read by avformat_open_input
    int64_t payload_read;       // of those, bytes of the Attachments payload
    int starts_inside;          // reads that began inside that payload
    int nb_streams, attachments, chapters, tracks;
    uint32_t track_sig;         // codec params of the track streams
    uint32_t packet_sig;        // every packet's stream/pts/data
    int64_t packets, seek_pts;
} Result;

static uint32_t crc(uint32_t c, const void *p, size_t n)
{
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE), c, p, n);
}

// mode: -1 = option absent, 0/1 = skip_attachments value
static int probe(const char *path, int mode, Result *r)
{
    Src src = { .f = fopen(path, "rb") };
    AVFormatContext *fc = avformat_alloc_context();
    AVDictionary *opts = NULL;
    AVPacket *pkt = av_packet_alloc();
    int ret;

    memset(r, 0, sizeof(*r));
    if (!src.f)
        return -1;
    if (!attachments_range(path, &src.lo, &src.hi))
        return -1;
    fc->pb = avio_alloc_context(av_malloc(IO_BUF), IO_BUF, 0, &src, src_read, NULL, src_seek);
    if (mode >= 0)
        av_dict_set_int(&opts, "skip_attachments", mode, 0);
    src.track = 1;
    ret = avformat_open_input(&fc, NULL, av_find_input_format("matroska"), &opts);
    src.track = 0;
    if (ret < 0)
        return ret;
    CHECK(!av_dict_count(opts), "%s: skip_attachments not consumed by the demuxer", path);
    av_dict_free(&opts);
    r->header_bytes = src.bytes;
    r->payload_read = src.payload_read;
    r->starts_inside = src.starts_inside;

    if ((ret = avformat_find_stream_info(fc, NULL)) < 0)
        return ret;
    r->nb_streams = fc->nb_streams;
    r->chapters = fc->nb_chapters;
    for (unsigned i = 0; i < fc->nb_streams; i++) {
        AVStream *st = fc->streams[i];
        AVCodecParameters *p = st->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_ATTACHMENT ||
            (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            r->attachments++;
            continue;
        }
        CHECK(!r->attachments, "%s: track stream %u after an attachment stream", path, i);
        r->tracks++;
        int v[] = { i, p->codec_type, p->codec_id, p->width, p->height,
                    p->sample_rate, p->ch_layout.nb_channels, p->extradata_size };
        r->track_sig = crc(r->track_sig, v, sizeof(v));
        if (p->extradata_size)
            r->track_sig = crc(r->track_sig, p->extradata, p->extradata_size);
    }

    while (av_read_frame(fc, pkt) >= 0) {
        int64_t v[] = { pkt->stream_index, pkt->pts, pkt->size };
        r->packet_sig = crc(r->packet_sig, v, sizeof(v));
        r->packet_sig = crc(r->packet_sig, pkt->data, pkt->size);
        r->packets++;
        av_packet_unref(pkt);
    }

    // Seek to 5 s (cues) and read the first video packet after it.
    r->seek_pts = AV_NOPTS_VALUE;
    if (av_seek_frame(fc, -1, 5 * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD) >= 0) {
        while (av_read_frame(fc, pkt) >= 0) {
            AVStream *st = fc->streams[pkt->stream_index];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                r->seek_pts = av_rescale_q(pkt->pts, st->time_base, AV_TIME_BASE_Q);
                av_packet_unref(pkt);
                break;
            }
            av_packet_unref(pkt);
        }
    }

    AVIOContext *pb = fc->pb;   // custom I/O: ours to free, after the demuxer
    av_packet_free(&pkt);
    avformat_close_input(&fc);
    av_freep(&pb->buffer);
    avio_context_free(&pb);
    fclose(src.f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <expected av_version_info> <fixture.mkv>...\n", argv[0]);
        return 2;
    }

    // (d) build identity
    printf("av_version_info: %s\n", av_version_info());
    CHECK(!strcmp(av_version_info(), argv[1]), "av_version_info is not %s", argv[1]);
    CHECK(avcodec_find_encoder_by_name("eac3"), "eac3 encoder missing");
    CHECK(avfilter_get_by_name("yadif_videotoolbox"), "yadif_videotoolbox filter missing");

    // (a) the option on the matroska demuxer
    const AVInputFormat *mkv = av_find_input_format("matroska");
    const AVOption *o = mkv && mkv->priv_class ?
        av_opt_find((void *)&mkv->priv_class, "skip_attachments", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ) : NULL;
    CHECK(o && o->type == AV_OPT_TYPE_BOOL && o->default_val.i64 == 0 &&
          (o->flags & AV_OPT_FLAG_DECODING_PARAM),
          "skip_attachments missing on matroska demuxer or not bool/default 0/decoding");

    // (b)(c) fixtures
    for (int i = 2; i < argc; i++) {
        Result absent, off, on;
        const char *f = argv[i];
        if (probe(f, -1, &absent) < 0 || probe(f, 0, &off) < 0 || probe(f, 1, &on) < 0) {
            CHECK(0, "%s: open/probe failed", f);
            continue;
        }
        printf("%s: streams absent/0/1 = %d/%d/%d, attachments %d/%d/%d, "
               "header bytes read %" PRId64 "/%" PRId64 "/%" PRId64 ", "
               "attachment bytes read unset/1 = %" PRId64 "/%" PRId64 ", packets %" PRId64 "\n",
               f, absent.nb_streams, off.nb_streams, on.nb_streams,
               absent.attachments, off.attachments, on.attachments,
               absent.header_bytes, off.header_bytes, on.header_bytes,
               absent.payload_read, on.payload_read, absent.packets);

        CHECK(!memcmp(&absent, &off, sizeof(absent)), "%s: skip_attachments=0 differs from absent", f);
        CHECK(absent.attachments > 0, "%s: fixture has no attachment streams", f);
        CHECK(on.attachments == 0, "%s: skip_attachments=1 still created attachment streams", f);
        CHECK(on.tracks == absent.tracks && on.track_sig == absent.track_sig,
              "%s: track streams differ with skip_attachments=1", f);
        CHECK(on.nb_streams == absent.nb_streams - absent.attachments, "%s: stream count", f);
        CHECK(on.packets > 0 && on.packets == absent.packets && on.packet_sig == absent.packet_sig,
              "%s: packets differ with skip_attachments=1", f);
        CHECK(on.chapters == absent.chapters, "%s: chapters differ", f);
        CHECK(absent.seek_pts != AV_NOPTS_VALUE && on.seek_pts == absent.seek_pts,
              "%s: seek result differs (%" PRId64 " vs %" PRId64 ")", f, on.seek_pts, absent.seek_pts);
        // I/O ranges while opening: stock reads the payload (sanity check
        // of the fixture); with 1 no read may begin inside it, and only the
        // tail of a buffer filled before it may cover its first bytes.
        CHECK(absent.payload_read > 0 && absent.starts_inside > 0,
              "%s: stock open did not read the attachments (fixture?)", f);
        CHECK(!on.starts_inside && on.payload_read < IO_BUF,
              "%s: skip_attachments=1 read %" PRId64 " attachment bytes, %d reads inside it",
              f, on.payload_read, on.starts_inside);
    }

    printf(failures ? "verify: %d FAILURE(S)\n" : "verify: OK\n", failures);
    return failures ? 1 : 0;
}
