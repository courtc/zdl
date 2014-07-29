/*
 * Copyright (c) 2012, Courtney Cavin
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef _WIN32
#include <windows.h>
#define time_ms GetTickCount64
#else
#include <stdlib.h>
#include <sys/time.h>
unsigned long long time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (unsigned long long)tv.tv_sec*1000 + tv.tv_usec/1000;
}
#endif

#include <GL/gl.h>
#include <stdio.h>

#include "zdl.h"

class FPSTracker {
public:
	FPSTracker(void)
	 : mFrame(0)
	{
		mLastTime = time_ms();
	}

	bool update(unsigned int delta, int &fps)
	{
		unsigned long long now = time_ms();

		mFrame++;
		if (now - mLastTime < delta)
			return false;

		fps = mFrame * 1000. / (now - mLastTime) + .5;
		mFrame = 0;
		mLastTime = now;

		return true;
	}

private:
	unsigned long long mLastTime;
	unsigned int mFrame;
};

int main(int argc, char **argv)
{
	zdl_flags_t flags = ZDL_FLAG_NONE;
	ZDL::Window *window = new ZDL::Window(320, 240, flags);
	FPSTracker tracker;
	int done = 0;
	int w, h;
	int fps;

	window->setTitle(argv[0]);
	window->getSize(&w, &h);
	glViewport(0, 0, w, h);

	while (!done) {
		struct zdl_event event;

		while (window->pollEvent(&event) == 0) {
			switch (event.type) {
			case ZDL_EVENT_KEYPRESS:
				if (event.key.unicode != 0)
					fprintf(stderr, "%c", event.key.unicode & 0x7f);
				switch (event.key.sym) {
				case ZDL_KEYSYM_R:
					flags ^= ZDL_FLAG_NORESIZE;
					fprintf(stderr, "\rresize: %sabled\n", (flags & ZDL_FLAG_NORESIZE) ? "dis" : "en");
					window->setFlags(flags);
					break;
				case ZDL_KEYSYM_F:
					flags ^= (ZDL_FLAG_FULLSCREEN | ZDL_FLAG_NOCURSOR);
					fprintf(stderr, "\rfullscreen: %sabled\n", (flags & ZDL_FLAG_FULLSCREEN) ? "en" : "dis");
					window->setFlags(flags);
					window->getSize(&w, &h);
					glViewport(0, 0, w, h);
					break;
				case ZDL_KEYSYM_ESCAPE:
				case ZDL_KEYSYM_Q:
					fprintf(stderr, "\rexiting");
					done = 1;
					break;
				default:
					break;
				}
				break;
			case ZDL_EVENT_BUTTONPRESS:
				switch (event.button.button) {
				case ZDL_BUTTON_LEFT:
					fprintf(stderr, "\rbutton left@(%d,%d)\n", event.button.x, event.button.y);
					break;
				case ZDL_BUTTON_RIGHT:
					fprintf(stderr, "\rbutton right@(%d,%d)\n", event.button.x, event.button.y);
					break;
				case ZDL_BUTTON_MIDDLE:
					fprintf(stderr, "\rbutton middle@(%d,%d)\n", event.button.x, event.button.y);
					break;
				case ZDL_BUTTON_MWDOWN:
					fprintf(stderr, "\rbutton wheel-down@(%d,%d)\n", event.button.x, event.button.y);
					break;
				case ZDL_BUTTON_MWUP:
					fprintf(stderr, "\rbutton wheel-up@(%d,%d)\n", event.button.x, event.button.y);
					break;
				}
				break;
			case ZDL_EVENT_MOTION:
				//fprintf(stderr, "\rmotion (%d,%d)\n", event.motion.d_x, event.motion.d_y);
				break;
			case ZDL_EVENT_EXIT:
				done = 1;
				break;
			case ZDL_EVENT_RECONFIGURE:
				w = event.reconfigure.width;
				h = event.reconfigure.height;
				fprintf(stderr, "\rresize (%d,%d)\n", w, h);
				glViewport(0, 0, w, h);
				break;
			default:
				break;
			}
		}

		glClearColor(0.0,0.0,0.0,0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0f,w,h,0.0f,1.0f,-1.0f);

		glBegin(GL_QUADS);
			glColor3f(1.0f,0.0f,0.0f);

			float fw = (float)w, fh = (float)h;
			glVertex2f(0.0f,0.0f);
			glVertex2f(fw,   0.0f);
			glVertex2f(fw,  10.0f);
			glVertex2f(0.0f,10.0f);

			glColor3f(1.0f,0.0f,1.0f);
			glVertex2f(0.0f,fh);
			glVertex2f(fw,  fh);
			glVertex2f(fw,  fh-10.0f);
			glVertex2f(0.0f,fh-10.f);


		glEnd();

		window->swap();
		if (tracker.update(100, fps))
			fprintf(stderr, "\r%3d fps ", fps);
	}

	delete window;
	return 0;
}

ZDL_MAIN_FIXUP
