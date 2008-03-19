/**
 * \file glc/play/gl_play.c
 * \brief OpenGL playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup gl_play
 *  \{
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include <unistd.h>
#include <packetstream.h>
#include <pthread.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>
#include <glc/common/thread.h>

#include "gl_play.h"

#define GL_PLAY_RUNNING            0x1
#define GL_PLAY_INITIALIZED        0x2
#define GL_PLAY_FULLSCREEN         0x4
#define GL_PLAY_NON_POWER_OF_TWO   0x8
#define GL_PLAY_CANCEL            0x10

struct gl_play_s {
	glc_t *glc;
	glc_flags_t flags;

	glc_thread_t play_thread;

	glc_stream_id_t id;
	GLenum format;
	unsigned int w, h;
	unsigned int pack_alignment;
	glc_utime_t last;
	size_t row;
	size_t bpp;

	glc_utime_t sleep_threshold;
	glc_utime_t skip_threshold;

	Display *dpy;
	Window win;
	GLXContext ctx;
	char name[100];

	GLsizei max_texture_width;
	GLsizei max_texture_height;

	GLuint *textures;
	GLint *draw_coord;
	GLfloat *tex_coord;
	GLsizei *tex_from;
	GLsizei texture_w, texture_h;
	GLsizei texture_x_num, texture_y_num;

	Atom wm_proto_atom;
	Atom wm_delete_window_atom;
	Atom net_wm_state_atom;
	Atom net_wm_state_fullscreen_atom;
};

int gl_play_thread_create_callback(void *ptr, void **threadptr);
int gl_play_read_callback(glc_thread_state_t *state);
void gl_play_finish_callback(void *ptr, int err);

int gl_play_create_ctx(gl_play_t gl_play);
int gl_play_update_ctx(gl_play_t gl_play);
int gl_play_update_viewport(gl_play_t gl_play, int x, int y,
			    unsigned int w, unsigned int h);
int gl_play_toggle_fullscreen(gl_play_t gl_play);

int gl_play_init_texture_information(gl_play_t gl_play);
int gl_play_create_textures(gl_play_t gl_play);
int gl_play_destroy_textures(gl_play_t gl_play);

int gl_play_draw_video_data_messageture(gl_play_t gl_play, char *from);

int gl_play_handle_xevents(gl_play_t gl_play, glc_thread_state_t *state);

int gl_play_init(gl_play_t *gl_play, glc_t *glc)
{
	*gl_play = (gl_play_t) malloc(sizeof(struct gl_play_s));
	memset(*gl_play, 0, sizeof(struct gl_play_s));

	(*gl_play)->glc = glc;
	(*gl_play)->id = 1;
	(*gl_play)->sleep_threshold = 100; /* 100us */
	(*gl_play)->skip_threshold = 25000; /* 25ms */

	(*gl_play)->play_thread.flags = GLC_THREAD_READ;
	(*gl_play)->play_thread.ptr = *gl_play;
	(*gl_play)->play_thread.thread_create_callback = &gl_play_thread_create_callback;
	(*gl_play)->play_thread.read_callback = &gl_play_read_callback;
	(*gl_play)->play_thread.finish_callback = &gl_play_finish_callback;
	(*gl_play)->play_thread.threads = 1;

	/* TODO support more formats */
	(*gl_play)->format = GL_BGR;

	return 0;
}

int gl_play_destroy(gl_play_t gl_play)
{
	free(gl_play);
	return 0;
}

int gl_play_set_stream_number(gl_play_t gl_play, glc_stream_id_t ctx)
{
	gl_play->id = ctx;
	return 0;
}

int gl_play_process_start(gl_play_t gl_play, ps_buffer_t *from)
{
	int ret;
	if (gl_play->flags & GL_PLAY_RUNNING)
		return EAGAIN;

	if ((ret = glc_thread_create(gl_play->glc, &gl_play->play_thread, from, NULL)))
		return ret;
	gl_play->flags |= GL_PLAY_RUNNING;

	return 0;
}

int gl_play_process_wait(gl_play_t gl_play)
{
	if (!(gl_play->flags & GL_PLAY_RUNNING))
		return EAGAIN;

	glc_thread_wait(&gl_play->play_thread);
	gl_play->flags &= ~GL_PLAY_RUNNING;

	return 0;
}

int gl_play_thread_create_callback(void *ptr, void **threadptr)
{
	gl_play_t gl_play = (gl_play_t) ptr;

	if (!gl_play->dpy)
		gl_play->dpy = XOpenDisplay(NULL);

	if (!gl_play->dpy) {
		glc_log(gl_play->glc, GLC_ERROR, "gl_play",
			 "can't open display");
		return EAGAIN;
	}

	return 0;
}

void gl_play_finish_callback(void *ptr, int err)
{
	gl_play_t gl_play = (gl_play_t) ptr;

	if (err)
		glc_log(gl_play->glc, GLC_ERROR, "gl_play",
			 "%s (%d)", strerror(err), err);

	if (gl_play->flags & GL_PLAY_INITIALIZED) {
		if (gl_play->textures)
			gl_play_destroy_textures(gl_play);

		glXDestroyContext(gl_play->dpy, gl_play->ctx);
		XDestroyWindow(gl_play->dpy, gl_play->win);
	}

	XCloseDisplay(gl_play->dpy);
	gl_play->dpy = NULL;
}

int gl_play_draw_video_data_messageture(gl_play_t gl_play, char *from)
{
	unsigned int x, y, tex_i = 0;

	for (y = 0; y < gl_play->texture_y_num; y++) {
		for (x = 0; x < gl_play->texture_x_num; x++) {
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, gl_play->textures[tex_i]);

			glPixelStorei(GL_UNPACK_ALIGNMENT, gl_play->pack_alignment);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, gl_play->w);
			glTexImage2D(GL_TEXTURE_2D, 0, 3,
				     gl_play->tex_from[tex_i * 3 + 1],
				     gl_play->tex_from[tex_i * 3 + 2],
				     0, gl_play->format, GL_UNSIGNED_BYTE,
				     &from[gl_play->tex_from[tex_i * 3]]);

			/* coord: [x1][y1][x2][y2] */
			glBegin(GL_QUADS);
			glTexCoord2f(gl_play->tex_coord[tex_i * 4], gl_play->tex_coord[tex_i * 4 + 1]);
			glVertex2i(gl_play->draw_coord[tex_i * 4], gl_play->draw_coord[tex_i * 4 + 1]);

			glTexCoord2f(gl_play->tex_coord[tex_i * 4 + 2], gl_play->tex_coord[tex_i * 4 + 1]);
			glVertex2i(gl_play->draw_coord[tex_i * 4 + 2], gl_play->draw_coord[tex_i * 4 + 1]);

			glTexCoord2f(gl_play->tex_coord[tex_i * 4 + 2], gl_play->tex_coord[tex_i * 4 + 3]);
			glVertex2i(gl_play->draw_coord[tex_i * 4 + 2], gl_play->draw_coord[tex_i * 4 + 3]);

			glTexCoord2f(gl_play->tex_coord[tex_i * 4], gl_play->tex_coord[tex_i * 4 + 3]);
			glVertex2i(gl_play->draw_coord[tex_i * 4], gl_play->draw_coord[tex_i * 4 + 3]);
			glEnd();

			tex_i++;
		}
	}

	return 0;
}

int gl_play_create_ctx(gl_play_t gl_play)
{
	int attribs[] = {GLX_RGBA,
			 GLX_RED_SIZE, 1,
			 GLX_GREEN_SIZE, 1,
			 GLX_BLUE_SIZE, 1,
			 GLX_DOUBLEBUFFER,
			 GLX_DEPTH_SIZE, 1,
			 None};
	XVisualInfo *visinfo;
	XSetWindowAttributes winattr;

	visinfo = glXChooseVisual(gl_play->dpy, DefaultScreen(gl_play->dpy), attribs);

	winattr.background_pixel = 0;
	winattr.border_pixel = 0;
	winattr.colormap = XCreateColormap(gl_play->dpy, RootWindow(gl_play->dpy, DefaultScreen(gl_play->dpy)),
					   visinfo->visual, AllocNone);
	winattr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask;
	winattr.override_redirect = 0;
	gl_play->win = XCreateWindow(gl_play->dpy, RootWindow(gl_play->dpy, DefaultScreen(gl_play->dpy)),
				     0, 0, gl_play->w, gl_play->h, 0, visinfo->depth, InputOutput,
				     visinfo->visual, CWBackPixel | CWBorderPixel |
				     CWColormap | CWEventMask | CWOverrideRedirect, &winattr);

	gl_play->ctx = glXCreateContext(gl_play->dpy, visinfo, NULL, True);
	if (gl_play->ctx == NULL)
		return EAGAIN;

	gl_play->flags |= GL_PLAY_INITIALIZED;

	XFree(visinfo);

	gl_play_init_texture_information(gl_play);

	gl_play->wm_proto_atom = XInternAtom(gl_play->dpy, "WM_PROTOCOLS", True);
	gl_play->wm_delete_window_atom = XInternAtom(gl_play->dpy, "WM_DELETE_WINDOW", False);

	gl_play->net_wm_state_atom = XInternAtom(gl_play->dpy, "_NET_WM_STATE", False);
	gl_play->net_wm_state_fullscreen_atom = XInternAtom(gl_play->dpy,
							    "_NET_WM_STATE_FULLSCREEN", False);

	XSetWMProtocols(gl_play->dpy, gl_play->win, &gl_play->wm_delete_window_atom, 1);

	return gl_play_update_ctx(gl_play);
}

int gl_play_update_ctx(gl_play_t gl_play)
{
	XSizeHints sizehints;

	if (!(gl_play->flags & GL_PLAY_INITIALIZED))
		return EINVAL;

	snprintf(gl_play->name, sizeof(gl_play->name) - 1, "glc-play (ctx %d)", gl_play->id);

	XUnmapWindow(gl_play->dpy, gl_play->win);

	sizehints.x = 0;
	sizehints.y = 0;
	sizehints.width = gl_play->w;
	sizehints.height = gl_play->h;
	sizehints.min_aspect.x = gl_play->w;
	sizehints.min_aspect.y = gl_play->h;
	sizehints.max_aspect.x = gl_play->w;
	sizehints.max_aspect.y = gl_play->h;
	sizehints.flags = USSize | USPosition | PAspect;
	XSetNormalHints(gl_play->dpy, gl_play->win, &sizehints);
	XSetStandardProperties(gl_play->dpy, gl_play->win, gl_play->name, gl_play->name, None,
			       (char **)NULL, 0, &sizehints);
	XResizeWindow(gl_play->dpy, gl_play->win, gl_play->w, gl_play->h);

	XMapWindow(gl_play->dpy, gl_play->win);

	glXMakeCurrent(gl_play->dpy, gl_play->win, gl_play->ctx);

	/* make sure our textures match */
	if (!gl_play->textures)
		gl_play_create_textures(gl_play);

	return gl_play_update_viewport(gl_play, 0, 0, gl_play->w, gl_play->h);
}

int gl_play_update_viewport(gl_play_t gl_play, int x, int y,
			    unsigned int w, unsigned int h)
{
	/* make sure old viewport is clear */
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glXSwapBuffers(gl_play->dpy, gl_play->win);

	glViewport((GLint) x, (GLint) y, (GLsizei) w, (GLsizei) h);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, gl_play->w, 0.0, gl_play->h, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	return 0;
}

int gl_play_init_texture_information(gl_play_t gl_play)
{
	const char *gl_extensions = NULL;

	/* check for GL_ARB_texture_non_power_of_two extension */
	glXMakeCurrent(gl_play->dpy, gl_play->win, gl_play->ctx);
	gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (gl_extensions) {
		if (strstr(gl_extensions, "GL_ARB_texture_non_power_of_two")) {
			gl_play->flags |= GL_PLAY_NON_POWER_OF_TWO;
			glc_log(gl_play->glc, GLC_INFORMATION, "gl_play",
				"GL_ARB_texture_non_power_of_two supported");
		}
	}

	/* figure out maximum texture size */
	gl_play->max_texture_width = 64;
	gl_play->max_texture_height = 64;

	while ((gl_play->max_texture_width < 4096) &&
	       (gl_play->max_texture_height < 4096)) {
		glTexImage2D(GL_PROXY_TEXTURE_2D, 0, 3,
			     gl_play->max_texture_width * 2,
			     gl_play->max_texture_height * 2,
			     0, gl_play->format, GL_UNSIGNED_BYTE,
			     NULL);

		if (glGetError()) /* we hit maximum value */
			break;

		gl_play->max_texture_width *= 2;
		gl_play->max_texture_height *= 2;
	}

	glc_log(gl_play->glc, GLC_INFORMATION, "gl_play",
		"maximum texture size is %ux%u",
		gl_play->max_texture_width,
		gl_play->max_texture_height);

	return 0;
}

int gl_play_create_textures(gl_play_t gl_play)
{
	unsigned int x, y; /* texture grid position */
	unsigned int dx, dy; /* coordinates in output */
	unsigned int dw, dh; /* draw dimensions */
	float px1, py1; /* texture coordinates */
	unsigned int fx, fy; /* source image x, y */
	unsigned int fw, fh; /* source image w, h */
	GLsizei i; /* texture array index */

	if (gl_play->textures)
		return EALREADY;

	/* texture width or height */
	if (gl_play->flags & GL_PLAY_NON_POWER_OF_TWO) {
		gl_play->texture_w = (gl_play->w > gl_play->max_texture_width) ?
				     gl_play->max_texture_width : gl_play->w;
		gl_play->texture_h = (gl_play->h > gl_play->max_texture_height) ?
				     gl_play->max_texture_height : gl_play->h;
	} else {
		gl_play->texture_w = gl_play->max_texture_width;
		gl_play->texture_h = gl_play->max_texture_height;

		while ((!(gl_play->flags & GL_PLAY_NON_POWER_OF_TWO)) &&
		((gl_play->texture_w > gl_play->h) | (gl_play->texture_h > gl_play->w))) {
			gl_play->texture_w /= 2;
			gl_play->texture_h /= 2;
		}
	}

	gl_play->texture_x_num = (gl_play->w / gl_play->texture_w
				 + ((gl_play->w % gl_play->texture_w) ? 1 : 0));
	gl_play->texture_y_num = (gl_play->h / gl_play->texture_h +
				 + ((gl_play->h % gl_play->texture_h) ? 1 : 0));

	glc_log(gl_play->glc, GLC_INFORMATION, "gl_play",
		"creating %ux%u textures", gl_play->texture_x_num, gl_play->texture_y_num);

	gl_play->textures = (GLuint *) malloc(sizeof(GLuint) * gl_play->texture_x_num
							     * gl_play->texture_y_num);
	gl_play->draw_coord = (GLint *) malloc(sizeof(GLint) * gl_play->texture_x_num
							     * gl_play->texture_y_num * 4);
	gl_play->tex_coord = (GLfloat *) malloc(sizeof(GLfloat) * gl_play->texture_x_num
								* gl_play->texture_y_num * 4);
	gl_play->tex_from = (GLsizei *) malloc(sizeof(GLsizei) * gl_play->texture_x_num
							       * gl_play->texture_y_num * 3);

	glGenTextures(gl_play->texture_x_num * gl_play->texture_y_num, gl_play->textures);

	dx = dy = i = 0;
	for (y = 0; y < gl_play->texture_y_num; y++) {
		for (x = 0; x < gl_play->texture_x_num; x++) {
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, gl_play->textures[i]);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

			dh = (dy + gl_play->texture_h > gl_play->h) ?
			     (gl_play->h - dy) : (gl_play->texture_h);
			dw = (dx + gl_play->texture_w > gl_play->w) ?
			     (gl_play->w - dx) : (gl_play->texture_w);

			if (gl_play->flags & GL_PLAY_NON_POWER_OF_TWO) {
				px1 = py1 = 0;
				fh = dh;
				fw = dw;
				fx = dx;
				fy = dy;
			} else {
				py1 = (float) (gl_play->texture_h - dh) / (float) gl_play->texture_h;
				px1 = (float) (gl_play->texture_w - dw) / (float) gl_play->texture_w;
				fh = gl_play->texture_h;
				fw = gl_play->texture_w;
				fx = (dx + fw > gl_play->w) ? (gl_play->w - gl_play->texture_w) : (dx);
				fy = (dy + fh > gl_play->h) ? (gl_play->h - gl_play->texture_h) : (dy);
			}

			glc_log(gl_play->glc, GLC_DEBUG, "gl_play",
				"tex %u: draw coords: x1=%u, y1=%u, x2=%u, y2=%u",
				i, dx, dy, dx + dw, dy + dh);
			gl_play->draw_coord[i * 4 + 0] = dx;
			gl_play->draw_coord[i * 4 + 1] = dy;
			gl_play->draw_coord[i * 4 + 2] = dx + dw;
			gl_play->draw_coord[i * 4 + 3] = dy + dh;

			glc_log(gl_play->glc, GLC_DEBUG, "gl_play",
				"tex %u: tex coords: x1=%f, y1=%f, x2=%f, y2=%f",
				i, px1, py1, 1.0, 1.0);
			gl_play->tex_coord[i * 4 + 0] = px1;
			gl_play->tex_coord[i * 4 + 1] = py1;
			gl_play->tex_coord[i * 4 + 2] = 1.0; /* x2 */
			gl_play->tex_coord[i * 4 + 3] = 1.0; /* y2 */

			glc_log(gl_play->glc, GLC_DEBUG, "gl_play",
				"tex %u: source: x=%u, y=%u, w=%u, h=%u",
				i, fx, fy, fw, fh);
			gl_play->tex_from[i * 3 + 0] = gl_play->row * fy + gl_play->bpp * fx;
			gl_play->tex_from[i * 3 + 1] = fw;
			gl_play->tex_from[i * 3 + 2] = fh;

			i++;
			dx += gl_play->texture_w;
		}
		dx = 0;
		dy += gl_play->texture_h;
	}

	return 0;
}

int gl_play_destroy_textures(gl_play_t gl_play)
{
	if (!gl_play->textures)
		return EAGAIN;

	glDeleteTextures(gl_play->texture_x_num * gl_play->texture_y_num, gl_play->textures);
	free(gl_play->textures);
	free(gl_play->draw_coord);
	free(gl_play->tex_coord);
	free(gl_play->tex_from);
	gl_play->textures = NULL;

	return 0;
}

int gl_play_toggle_fullscreen(gl_play_t gl_play)
{
	XClientMessageEvent event;
	int fsmsg;

	if (gl_play->flags & GL_PLAY_FULLSCREEN) {
		gl_play->flags &= ~GL_PLAY_FULLSCREEN;
		fsmsg = 0;
	} else {
		gl_play->flags |= GL_PLAY_FULLSCREEN;
		fsmsg = 1;
	}

	memset(&event, 0, sizeof(XClientMessageEvent));

	event.type = ClientMessage;
	event.message_type = gl_play->net_wm_state_atom;
	event.display = gl_play->dpy;
	event.window = gl_play->win;
	event.format = 32;
	event.data.l[0] = fsmsg;
	event.data.l[1] = gl_play->net_wm_state_fullscreen_atom;

	XSendEvent(gl_play->dpy, DefaultRootWindow(gl_play->dpy),
		   False, SubstructureRedirectMask, (XEvent*) &event);

	return 0;
}

int gl_handle_xevents(gl_play_t gl_play, glc_thread_state_t *state)
{
	XEvent event;
	XConfigureEvent *ce;
	int code;
	float xf, yf;

	while (XPending(gl_play->dpy) > 0) {
		XNextEvent(gl_play->dpy, &event);

		switch (event.type) {
		case KeyPress:
			code = XLookupKeysym(&event.xkey, 0);

			if (code == XK_Right)
				glc_state_time_add_diff(gl_play->glc, -100000);
			else if (code == XK_f)
				gl_play_toggle_fullscreen(gl_play);
			break;
		case KeyRelease:
			code = XLookupKeysym(&event.xkey, 0);

			if (code == XK_Escape)
				glc_state_set(gl_play->glc, GLC_STATE_CANCEL);
			break;
		case DestroyNotify:
			glc_state_set(gl_play->glc, GLC_STATE_CANCEL);
			break;
		case ClientMessage:
			if (event.xclient.message_type == gl_play->wm_proto_atom) {
				if ((Atom) event.xclient.data.l[0] == gl_play->wm_delete_window_atom) {
					/*
					state->flags |= GLC_THREAD_STOP;
					 this would kill just this single stream, but it confuses
					 users, so...
					*/
					glc_state_set(gl_play->glc, GLC_STATE_CANCEL);
				}
			}
			break;
		case ConfigureNotify:
			ce = (XConfigureEvent *) &event;
			xf = (float) ce->width / (float) gl_play->w;
			yf = (float) ce->height / (float) gl_play->h;

			if (xf < yf)
				gl_play_update_viewport(gl_play, 0, (ce->height - gl_play->h * xf) / 2,
							gl_play->w * xf, gl_play->h * xf);
			else
				gl_play_update_viewport(gl_play, (ce->width - gl_play->w * yf) / 2, 0,
							gl_play->w * yf, gl_play->h * yf);

			break;
		}
	}

	return 0;
}

int gl_play_read_callback(glc_thread_state_t *state)
{
	gl_play_t gl_play = (gl_play_t) state->ptr;

	glc_video_format_message_t *format_msg;
	glc_video_data_header_t *pic_hdr;
	glc_utime_t time;

	gl_handle_xevents(gl_play, state);

	if (state->flags & GLC_THREAD_STOP)
		return 0;

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT) {
		format_msg = (glc_video_format_message_t *) state->read_data;
		if (format_msg->id != gl_play->id)
			return 0; /* just ignore it */

		gl_play->w = format_msg->width;
		gl_play->h = format_msg->height;
		gl_play->bpp = 3;
		gl_play->row = gl_play->w * gl_play->bpp;

		if (format_msg->flags & GLC_VIDEO_DWORD_ALIGNED) {
			gl_play->pack_alignment = 8;
			if (gl_play->row % 8 != 0)
				gl_play->row += 8 - gl_play->row % 8;
		} else
			gl_play->pack_alignment = 1;

		if ((format_msg->format == GLC_VIDEO_BGR) &&
		    !(gl_play->flags & GL_PLAY_INITIALIZED))
			gl_play_create_ctx(gl_play);
		else if (format_msg->format == GLC_VIDEO_BGR) {
			if (gl_play_update_ctx(gl_play)) {
				glc_log(gl_play->glc, GLC_ERROR, "gl_play",
					 "broken video stream %d", format_msg->id);
				return EINVAL;
			}
		} else {
			glc_log(gl_play->glc, GLC_ERROR, "gl_play",
				"video stream %d is in unsupported format 0x%02x",
				format_msg->id, format_msg->format);
			return EINVAL;
		}
	} else if (state->header.type == GLC_MESSAGE_VIDEO_DATA) {
		pic_hdr = (glc_video_data_header_t *) state->read_data;

		if (pic_hdr->id != gl_play->id)
			return 0;

		if (!(gl_play->flags & GL_PLAY_INITIALIZED)) {
			glc_log(gl_play->glc, GLC_ERROR, "gl_play",
				"picture refers to uninitalized video stream %d",
				pic_hdr->id);
			return EINVAL;
		}

		/* check if we have to draw this frame */
		time = glc_state_time(gl_play->glc);
		if (time > pic_hdr->time + gl_play->skip_threshold) {
			glc_log(gl_play->glc, GLC_DEBUG, "gl_play", "dropped frame");
			return 0;
		}

		/* draw first, measure and sleep after */
		gl_play_draw_video_data_messageture(gl_play, &state->read_data[GLC_VIDEO_DATA_HEADER_SIZE]);

		/* wait until actual drawing is done */
		glFinish();

		time = glc_state_time(gl_play->glc);
		if (pic_hdr->time > time + gl_play->sleep_threshold)
			usleep(pic_hdr->time - time);

		glXSwapBuffers(gl_play->dpy, gl_play->win);
	}

	return 0;
}

/**  \} */
