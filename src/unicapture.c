#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include "unicapture.h"
#include "log.h"

#define DLSYM_ERROR_CHECK()                                         \
    if ((error = dlerror()) != NULL)  {                             \
        ERR("Error! dlsym failed, msg: %s", error);                 \
        return -2;                                                  \
    }

static uint64_t getticks_us() {
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

int unicapture_init_backend(cap_backend_config_t* config, capture_backend_t* backend, char* name) {
    char* error;
    void* handle = dlopen(name, RTLD_LAZY);

    if (handle == NULL) {
        WARN("Unable to load %s: %s", name, dlerror());
        return -1;
    }

    dlerror();

    backend->init = dlsym(handle, "capture_init"); DLSYM_ERROR_CHECK();
    backend->cleanup = dlsym(handle, "capture_cleanup"); DLSYM_ERROR_CHECK();

    backend->start = dlsym(handle, "capture_start"); DLSYM_ERROR_CHECK();
    backend->terminate = dlsym(handle, "capture_terminate"); DLSYM_ERROR_CHECK();

    backend->acquire_frame = dlsym(handle, "capture_acquire_frame"); DLSYM_ERROR_CHECK();
    backend->release_frame = dlsym(handle, "capture_release_frame"); DLSYM_ERROR_CHECK();

    backend->wait = dlsym(handle, "capture_wait"); DLSYM_ERROR_CHECK();

    DBG("Backend loaded, initializing...");
    return backend->init(config, &backend->state);
}

int unicapture_run(capture_backend_t* ui_capture, capture_backend_t* video_capture) {
    bool ui_capture_ready = ui_capture != NULL;
    bool video_capture_ready = video_capture != NULL;

    uint64_t framecounter = 0;
    uint64_t framecounter_start = getticks_us();

    while (true) {
        int ret = 0;
        uint64_t frame_start = getticks_us();
        if (video_capture_ready && video_capture->wait) {
            video_capture->wait(video_capture->state);
        } else if (false && ui_capture_ready && ui_capture->wait) {
            ui_capture->wait(ui_capture->state);
        } else {
            DBG("Using fallback wait...");
            usleep (1000000 / 30);
        }

        uint64_t frame_wait = getticks_us();

        frame_info_t ui_frame = {PIXFMT_INVALID};
        frame_info_t video_frame = {PIXFMT_INVALID};

        if (ui_capture_ready) {
            if ((ret = ui_capture->acquire_frame(ui_capture->state, &ui_frame)) != 0) {
                ui_frame.pixel_format = PIXFMT_INVALID;
            }
        }

        if (video_capture_ready) {
            if ((ret = video_capture->acquire_frame(video_capture->state, &video_frame)) != 0) {
                DBG("video_capture acquire_frame failed: %d", ret);
                video_frame.pixel_format = PIXFMT_INVALID;
            }
        }

        uint64_t frame_acquired = getticks_us();

        uint64_t frame_processed = getticks_us();

        uint64_t frame_sent = getticks_us();

        if (ui_frame.pixel_format != PIXFMT_INVALID) {
            ui_capture->release_frame(ui_capture->state, &ui_frame);
        }

        if (video_frame.pixel_format != PIXFMT_INVALID) {
            video_capture->release_frame(video_capture->state, &video_frame);
        }

        framecounter += 1;
        if (framecounter >= 60) {
            double fps = (framecounter * 1000000.0) / (getticks_us() - framecounter_start);
            INFO("Framerate: %.6f FPS; timings - wait: %lldus, acquire: %lldus, process; %lldus, send: %lldus",
                    fps, frame_wait - frame_start, frame_acquired - frame_wait, frame_processed - frame_acquired, frame_sent - frame_processed);

            INFO("   UI: pixfmt: %d; %dx%d", ui_frame.pixel_format, ui_frame.width, ui_frame.height);
            INFO("VIDEO: pixfmt: %d; %dx%d", video_frame.pixel_format, video_frame.width, video_frame.height);

            framecounter = 0;
            framecounter_start = getticks_us();
        }
    }
}
