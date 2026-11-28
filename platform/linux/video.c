#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <video/artinchip_fb.h>
#include "fusion_display.h"
#include "mpp_decoder.h"
#include "rtcp_linux.h"

enum { FRAME_COUNT = 3, FRAME_CAP = 1024 * 1024, CONFIG_CAP = 16 * 1024 };
typedef struct
{
    uint8_t *bytes;
    size_t size;
    uint64_t timestamp;
    bool idr;
} encoded_frame_t;

static struct
{
    int fd;
    uint16_t width, height;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    encoded_frame_t frames[FRAME_COUNT];
    unsigned first, count, generation;
    bool running, stopping, streaming, waiting_idr;
    atomic_bool visible, needs_keyframe;
    uint8_t config[CONFIG_CAP];
    size_t config_size;
} video = {
    .fd = -1, .mutex = PTHREAD_MUTEX_INITIALIZER, .ready = PTHREAD_COND_INITIALIZER,
};

static bool contains_idr(const uint8_t *bytes, size_t size)
{
    unsigned zeros = 0;
    for (size_t index = 0; index + 1 < size; ++index)
    {
        if (bytes[index] == 1 && zeros >= 2 && (bytes[index + 1] & 31) == 5)
            return true;
        zeros = bytes[index] == 0 ? zeros + 1 : 0;
    }
    return false;
}

static void reset_queue(void)
{
    video.first = 0;
    video.count = 0;
    video.waiting_idr = true;
    ++video.generation;
    atomic_store(&video.visible, false);
    pthread_cond_signal(&video.ready);
}

static int packet_in(rtcp_video_message_type_t type, uint8_t *bytes, size_t size,
                     size_t prefix, uint64_t timestamp, uint64_t arrival, void *context)
{
    (void)prefix;
    (void)arrival;
    (void)context;
    if (!size)
        return RTCP_EINVAL;
    pthread_mutex_lock(&video.mutex);
    if (type == RTCP_VIDEO_CONFIG_H264)
    {
        reset_queue();
        video.config_size = size <= CONFIG_CAP ? size : 0;
        if (video.config_size)
            memcpy(video.config, bytes, size);
    }
    else
    {
        bool idr = contains_idr(bytes, size);
        if (video.count == FRAME_COUNT || size > FRAME_CAP)
        {
            reset_queue();
            atomic_store(&video.needs_keyframe, true);
        }
        if ((!video.waiting_idr || idr) && size <= FRAME_CAP && video.config_size)
        {
            unsigned next = (video.first + video.count) % FRAME_COUNT;
            encoded_frame_t *frame = &video.frames[next];
            memcpy(frame->bytes, bytes, size);
            frame->size = size;
            frame->timestamp = timestamp;
            frame->idr = idr;
            ++video.count;
            video.waiting_idr = false;
            pthread_cond_signal(&video.ready);
        }
    }
    pthread_mutex_unlock(&video.mutex);
    return RTCP_OK;
}

static struct mpp_decoder *open_decoder(void)
{
    struct decode_config config = {
        .pix_fmt = MPP_FMT_YUV420P, .bitstream_buffer_size = 2 * FRAME_CAP,
        .packet_count = 4, .extra_frame_num = 2,
    };
    struct mpp_decoder *decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_H264);
    if (decoder && mpp_decoder_init(decoder, &config) == DEC_OK)
        return decoder;
    if (decoder)
        mpp_decoder_destory(decoder);
    return NULL;
}

static bool submit(struct mpp_decoder *decoder, const encoded_frame_t *frame, unsigned flags)
{
    struct mpp_packet packet = {0};
    if (mpp_decoder_get_packet(decoder, &packet, (int)frame->size) != DEC_OK)
        return false;
    memcpy(packet.data, frame->bytes, frame->size);
    packet.size = (int)frame->size;
    packet.flag = flags;
    packet.pts = (long long)(frame->timestamp / 1000);
    return mpp_decoder_put_packet(decoder, &packet) == DEC_OK;
}

static int layer(struct mpp_frame *frame, rtcp_display_rect_t *destination, uint32_t *layout_generation)
{
    struct aicfb_layer_data data = { .layer_id = AICFB_LAYER_TYPE_VIDEO };
    if (frame)
    {
        data.enable = 1;
        data.buf = frame->buf;
        const struct mpp_buf *buffer = &frame->buf;
        rtcp_display_rect_t panel;
        if (!carplay_fusion_video_target(video.width, video.height, &panel, layout_generation) ||
            !rtcp_display_fit(&panel, buffer->crop_en ? buffer->crop.width : buffer->size.width,
                              buffer->crop_en ? buffer->crop.height : buffer->size.height, destination))
            return -1;
        data.pos.x = destination->x;
        data.pos.y = destination->y;
        data.scale_size.width = destination->width;
        data.scale_size.height = destination->height;
    }
    return ioctl(video.fd, AICFB_UPDATE_LAYER_CONFIG, &data);
}

static void remove_buffers(struct mpp_frame *frame, unsigned planes)
{
    while (planes > 0)
    {
        struct dma_buf_info buffer = { .fd = frame->buf.fd[--planes] };
        ioctl(video.fd, AICFB_RM_DMABUF, &buffer);
    }
}

static bool show_frame(struct mpp_frame *frame, struct mpp_frame *previous, bool *retained,
                       struct mpp_decoder *decoder)
{
    if (frame->buf.format != MPP_FMT_YUV420P || (frame->flags & FRAME_FLAG_ERROR))
        return false;
    unsigned plane = 0;
    while (plane < 3)
    {
        struct dma_buf_info buffer = { .fd = frame->buf.fd[plane] };
        if (ioctl(video.fd, AICFB_ADD_DMABUF, &buffer) < 0)
            break;
        ++plane;
    }
    rtcp_display_rect_t destination;
    uint32_t layout_generation;
    if (plane != 3 || layer(frame, &destination, &layout_generation) < 0)
    {
        remove_buffers(frame, plane);
        return false;
    }
    int synced = ioctl(video.fd, AICFB_WAIT_FOR_VSYNC, 0);
    if (synced < 0)
    {
        layer(NULL, NULL, NULL);
        remove_buffers(frame, plane);
        return false;
    }
    if (*retained)
    {
        remove_buffers(previous, 3);
        mpp_decoder_put_frame(decoder, previous);
    }
    *previous = *frame;
    *retained = true;
    carplay_fusion_video_latched(&destination, video.width, video.height, layout_generation,
                                 (uint32_t)rtcp_os_linux.now_ms(), true);
    return true;
}

static void release_output(struct mpp_decoder *decoder, struct mpp_frame *previous, bool *retained)
{
    if (*retained)
    {
        layer(NULL, NULL, NULL);
        ioctl(video.fd, AICFB_WAIT_FOR_VSYNC, 0);
        remove_buffers(previous, 3);
        mpp_decoder_put_frame(decoder, previous);
        *retained = false;
    }
    carplay_fusion_video_stopped();
}

static void *decode(void *context)
{
    struct mpp_decoder *decoder = context;
    struct mpp_frame previous;
    bool retained = false;
    uint8_t extra[CONFIG_CAP];
    unsigned generation = 0;
    bool needs_config = true;
    for (;;)
    {
        pthread_mutex_lock(&video.mutex);
        while (!video.count && generation == video.generation && !video.stopping)
            pthread_cond_wait(&video.ready, &video.mutex);
        if (video.stopping)
        {
            pthread_mutex_unlock(&video.mutex);
            break;
        }
        bool changed = generation != video.generation;
        generation = video.generation;
        encoded_frame_t config = { .bytes = extra, .size = video.config_size };
        if (changed && config.size)
            memcpy(extra, video.config, config.size);
        encoded_frame_t frame = {0};
        if (video.count)
            frame = video.frames[video.first];
        if (changed)
        {
            pthread_mutex_unlock(&video.mutex);
            release_output(decoder, &previous, &retained);
            if (decoder)
                mpp_decoder_destory(decoder);
            decoder = config.size ? open_decoder() : NULL;
            needs_config = !decoder || !submit(decoder, &config, 0);
            if (!needs_config)
                mpp_decoder_decode(decoder);
            continue;
        }
        bool ok = frame.size && !needs_config &&
            submit(decoder, &frame, frame.idr ? PACKET_FLAG_KEY : 0);
        video.first = (video.first + 1) % FRAME_COUNT;
        --video.count;
        pthread_mutex_unlock(&video.mutex);
        if (ok)
        {
            int result = mpp_decoder_decode(decoder);
            ok = result >= 0;
            struct mpp_frame output;
            if (ok && mpp_decoder_get_frame(decoder, &output) == DEC_OK)
            {
                ok = show_frame(&output, &previous, &retained, decoder);
                if (!ok)
                    mpp_decoder_put_frame(decoder, &output);
                else
                {
                    pthread_mutex_lock(&video.mutex);
                    if (generation == video.generation)
                        atomic_store(&video.visible, true);
                    pthread_mutex_unlock(&video.mutex);
                }
            }
        }
        if (!ok)
        {
            pthread_mutex_lock(&video.mutex);
            if (generation == video.generation)
            {
                reset_queue();
                atomic_store(&video.needs_keyframe, true);
            }
            pthread_mutex_unlock(&video.mutex);
        }
    }
    release_output(decoder, &previous, &retained);
    if (decoder)
        mpp_decoder_destory(decoder);
    return NULL;
}

int demo_video_open(const demo_options_t *options, rtcp_media_backend_t *media)
{
    video.fd = open(options->framebuffer, O_RDWR | O_CLOEXEC);
    if (video.fd < 0)
        return RTCP_EIO;
    struct mpp_decoder *decoder = open_decoder();
    if (!decoder)
        return RTCP_EIO;
    video.width = options->width;
    video.height = options->height;
    if (carplay_fusion_init(video.width, video.height) != RTCP_OK)
    {
        mpp_decoder_destory(decoder);
        return RTCP_ENOTSUP;
    }
    carplay_fusion_set_mode(CARPLAY_FULLSCREEN);
    carplay_fusion_set_projection_hidden(true);
    carplay_fusion_set_vehicle(&(carplay_vehicle_sample_t){0});
    video.waiting_idr = true;
    for (unsigned index = 0; index < FRAME_COUNT; ++index)
    {
        video.frames[index].bytes = malloc(FRAME_CAP);
        if (!video.frames[index].bytes)
        {
            mpp_decoder_destory(decoder);
            return RTCP_ENOMEM;
        }
    }
    if (pthread_create(&video.thread, NULL, decode, decoder) != 0)
    {
        mpp_decoder_destory(decoder);
        return RTCP_EIO;
    }
    video.running = true;
    media->on_video = packet_in;
    return RTCP_OK;
}

void demo_video_session(bool live)
{
    if (!video.running || video.streaming == live)
        return;
    video.streaming = live;
    if (!live)
    {
        pthread_mutex_lock(&video.mutex);
        video.config_size = 0;
        reset_queue();
        pthread_mutex_unlock(&video.mutex);
    }
}

bool demo_video_needs_keyframe(void)
{
    return atomic_exchange(&video.needs_keyframe, false);
}

bool demo_video_visible(void)
{
    return atomic_load(&video.visible);
}

void demo_video_close(void)
{
    if (video.running)
    {
        pthread_mutex_lock(&video.mutex);
        video.stopping = true;
        pthread_cond_signal(&video.ready);
        pthread_mutex_unlock(&video.mutex);
        pthread_join(video.thread, NULL);
        video.running = false;
    }
    for (unsigned index = 0; index < FRAME_COUNT; ++index)
    {
        free(video.frames[index].bytes);
        video.frames[index].bytes = NULL;
    }
    if (video.fd >= 0)
        close(video.fd);
    video.fd = -1;
}
