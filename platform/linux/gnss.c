#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

static int serial = -1;
static struct termios original;
static pthread_mutex_t serial_lock = PTHREAD_MUTEX_INITIALIZER;

bool demo_gnss_open(const char *device)
{
    int fd = open(device, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return false;
    if (tcgetattr(fd, &original) == 0)
    {
        struct termios settings = original;
        cfmakeraw(&settings);
        settings.c_cflag |= CLOCAL | CREAD;
        if (cfsetspeed(&settings, B9600) == 0 && tcsetattr(fd, TCSAFLUSH, &settings) == 0)
        {
            serial = fd;
            return true;
        }
    }
    close(fd);
    return false;
}

void demo_gnss_close(void)
{
    if (serial >= 0)
    {
        tcsetattr(serial, TCSANOW, &original);
        close(serial);
        serial = -1;
    }
}

bool demo_gnss_available(void)
{
    return serial >= 0;
}

void demo_gnss_tick(demo_gnss_stream_t *stream, iap2_session_t *session, uint64_t now)
{
    if (serial < 0 || !iap2_session_location_requested(session))
    {
        stream->subscribed = false;
        stream->length = 0;
        return;
    }
    if (pthread_mutex_trylock(&serial_lock) != 0)
        return;
    if (!stream->subscribed)
    {
        tcflush(serial, TCIFLUSH);
        stream->subscribed = true;
    }
    char bytes[256];
    ssize_t count = read(serial, bytes, sizeof(bytes));
    pthread_mutex_unlock(&serial_lock);
    for (ssize_t index = 0; index < count; ++index)
    {
        char value = bytes[index];
        if (value == '$')
        {
            stream->sentence[0] = value;
            stream->length = 1;
        }
        else if (value == '\r' || value == '\n')
        {
            if (stream->length)
                iap2_session_send_location(session, stream->sentence, stream->length, now);
            stream->length = 0;
        }
        else if (stream->length)
        {
            if (stream->length < sizeof(stream->sentence))
                stream->sentence[stream->length++] = value;
            else
                stream->length = 0;
        }
    }
}
