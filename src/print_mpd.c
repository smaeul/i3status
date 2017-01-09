/*
 * vim:ts=4:sw=4:expandtab
 *
 * synchronous implementation with libmpdclient
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include <mpd/client.h>

#include "i3status.h"

static struct mpd_connection *conn = NULL;

#define MPDTAG(name, type) \
    { #name, sizeof(#name) - 1, MPD_TAG_##type }
static struct {
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

static bool mpd_initialize(const char *host, int port, const char *password) {
    if (conn) {
        /* Ensure an existing connection is still valid. */
        if (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS ||
            mpd_connection_clear_error(conn))
            return true;
        /* Otherwise, start over with a new one. */
        mpd_connection_free(conn);
    }
    if (!(conn = mpd_connection_new(host, port, 0)))
        return false;
    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS &&
        !mpd_connection_clear_error(conn))
        goto err;
    if (password && *password && !mpd_send_password(conn, password))
        goto err;
    return true;

err:
    mpd_connection_free(conn);
    conn = NULL;
    return false;
}

void print_mpd(yajl_gen json_gen, char *buffer,
               const char *format, const char *format_down,
               const char *host, int port, const char *password) {
    const char *fmtwalk;
    char *outwalk = buffer;
    struct mpd_song *song;
    struct mpd_status *status;

    if (!mpd_initialize(host, port, password)) {
        START_COLOR("color_bad");
        outwalk += sprintf(outwalk, "%s", format_down);
        goto end;
    }

    if (!(song = mpd_run_current_song(conn))) {
        START_COLOR("color_degraded");
        outwalk += sprintf(outwalk, "%s", format_down);
        goto end;
    }

    if ((status = mpd_run_status(conn)) && mpd_status_get_state(status) == MPD_STATE_PLAY)
        START_COLOR("color_good");
    else
        START_COLOR("color_degraded");
    if (status)
        mpd_status_free(status);

    fmtwalk = format;
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

end:
    END_COLOR;
    OUTPUT_FULL_TEXT(buffer);
}
