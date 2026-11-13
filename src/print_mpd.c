/*
 * vim:ts=4:sw=4:expandtab
 *
 * synchronous implementation with libmpdclient
 *
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include <mpd/client.h>

#include "i3status.h"

static char buf[1024];
static const char *buf_color = "color_bad";
static pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;

static mpd_ctx_t thread_ctx;
static pthread_t thread_id;
static bool thread_started;

#define MPDTAG(name, type) \
    { #name, sizeof(#name) - 1, MPD_TAG_##type }
static const struct {
    const char *name;
    int len;
    int type;
} mpd_tags[] = {
    MPDTAG(artist, ARTIST),
    MPDTAG(album, ALBUM),
    MPDTAG(album_artist, ALBUM_ARTIST),
    MPDTAG(title, TITLE),
    MPDTAG(track, TRACK),
    MPDTAG(name, NAME),
    MPDTAG(genre, GENRE),
    MPDTAG(date, DATE),
    MPDTAG(composer, COMPOSER),
    MPDTAG(performer, PERFORMER),
    MPDTAG(comment, COMMENT),
    MPDTAG(disc, DISC)};
#undef MPDTAG

static void *mpd_thread(void *arg) {
    const mpd_ctx_t *ctx = &thread_ctx;
    struct mpd_connection *conn;

    if (!(conn = mpd_connection_new(ctx->host, ctx->port, 0)))
        goto err;
    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS &&
        !mpd_connection_clear_error(conn))
        goto err_free;
    if (ctx->password && *ctx->password &&
        !mpd_send_password(conn, ctx->password))
        goto err_free;

    do {
        pthread_mutex_lock(&buf_mutex);

        struct mpd_song *song = mpd_run_current_song(conn);
        if (song) {
            struct mpd_status *status = mpd_run_status(conn);
            char *outwalk = buf;
            const char *fmtwalk;

            if (status && mpd_status_get_state(status) == MPD_STATE_PLAY)
                buf_color = "color_good";
            else
                buf_color = "color_degraded";

            if (status)
                mpd_status_free(status);

            fmtwalk = ctx->format;
            while (*fmtwalk) {
                if (*fmtwalk == '%') {
                    fmtwalk += 1;
                    if (*fmtwalk == '%') {
                        *outwalk = '%';
                        fmtwalk += 1;
                        outwalk += 1;
                    } else {
                        for (size_t i = 0; i < sizeof(mpd_tags) / sizeof(*mpd_tags); i += 1) {
                            if (!strncmp(fmtwalk, mpd_tags[i].name, mpd_tags[i].len)) {
                                const char *tag = mpd_song_get_tag(song, mpd_tags[i].type, 0);
                                fmtwalk += mpd_tags[i].len;
                                outwalk = stpcpy(outwalk, tag ? tag : "Unknown");
                                break;
                            }
                        }
                    }
                } else {
                    *outwalk = *fmtwalk;
                    fmtwalk += 1;
                    outwalk += 1;
                }
            }

            mpd_song_free(song);
        } else {
            strcpy(buf, ctx->format_down);
            buf_color = "color_degraded";
        }

        pthread_mutex_unlock(&buf_mutex);
        pthread_kill(main_thread, SIGUSR1);

        mpd_run_idle_mask(conn, MPD_IDLE_PLAYER);
    } while (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS ||
             mpd_connection_clear_error(conn));

err_free:
    mpd_connection_free(conn);
err:
    pthread_mutex_lock(&buf_mutex);
    strcpy(buf, ctx->format_down);
    buf_color = "color_bad";
    pthread_mutex_unlock(&buf_mutex);
    pthread_kill(main_thread, SIGUSR1);

    return NULL;
}

void print_mpd(mpd_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    int ret;

    if (thread_started) {
        ret = pthread_tryjoin_np(thread_id, NULL);
        if (ret != EBUSY)
            thread_started = false;
    }

    if (!thread_started) {
        memcpy(&thread_ctx, ctx, sizeof(thread_ctx));
        ret = pthread_create(&thread_id, NULL, mpd_thread, NULL);
        if (ret == 0)
            thread_started = true;
    }

    pthread_mutex_lock(&buf_mutex);
    START_COLOR(buf_color);
    outwalk += sprintf(outwalk, "%s", buf);
    pthread_mutex_unlock(&buf_mutex);
    END_COLOR;
    OUTPUT_FULL_TEXT(ctx->buf);
}
