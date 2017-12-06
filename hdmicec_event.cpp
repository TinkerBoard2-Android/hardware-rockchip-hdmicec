/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hardware_legacy/uevent.h>
#include <utils/Log.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <hardware/hdmi_cec.h>
#include <errno.h>
#include <hdmicec.h>

#define HDMI_CEC_UEVENT_THREAD_NAME "HdmiCecThread"

static int validcecmessage(hdmi_event_t cec_event)
{
	int ret = 0;

	if (cec_event.cec.length > 15)
		ret = -E2BIG;
	return ret;
}

static void *uevent_loop(void *param)
{
	hdmi_cec_context_t * ctx = reinterpret_cast<hdmi_cec_context_t *>(param);
	char thread_name[64] = HDMI_CEC_UEVENT_THREAD_NAME;
	hdmi_event_t cec_event;
	struct pollfd pfd[2];
	int fd[2];
	int ret, i;

	prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
	setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

	fd[0] = ctx->fd;
	if (fd[0] < 0) {
		ALOGE ("%s:not able to open cec state node", __func__);
		return NULL;
	}

	pfd[0].fd = fd[0];
  if (pfd[0].fd >= 0)
		pfd[0].events = POLLIN | POLLRDNORM | POLLPRI;

	while (true) {
		int err = poll(&pfd[0], 1, 20);

		if (!err) {
			continue;
		} else if(err > 0) {
			if (!ctx->enable || !ctx->system_control)
				continue;
			ALOGI("poll revent:%02x\n", pfd[0].revents);
			memset(&cec_event, 0, sizeof(hdmi_event_t));
			if (pfd[0].revents & (POLLIN)) {
				struct cec_msg cecframe;

				ALOGI("poll receive msg\n");
				ret = ioctl(pfd[0].fd, CEC_RECEIVE, &cecframe);
				if (!ret) {
					cec_event.type = HDMI_EVENT_CEC_MESSAGE;
					cec_event.dev = &ctx->device;
					cec_event.cec.initiator = (cec_logical_address_t)(cecframe.msg[0] >> 4);
					cec_event.cec.destination = (cec_logical_address_t)(cecframe.msg[0] & 0x0f);
					cec_event.cec.length = cecframe.len - 1;
					cec_event.cec.body[0] = cecframe.msg[1];
					if (!validcecmessage(cec_event)) {
						for (ret = 0; ret < cec_event.cec.length; ret++) {
							cec_event.cec.body [ret + 1] = cecframe.msg[ret + 2];
						}
						for (i = 0; i < cecframe.len; i++)
							ALOGI("poll receive msg[%d]:%02x\n", i, cecframe.msg[i]);
						if (ctx->event_callback)
							ctx->event_callback(&cec_event, ctx->cec_arg);
					} else {
						ALOGE("%s cec_event length > 15 ", __func__);
					}
				} else {
					ALOGE("%s hdmi cec read error", __FUNCTION__);
				}
			}

			if (pfd[0].revents & (POLLPRI)) {
				int state = -1;
				struct cec_event event;

				ALOGI("poll receive event\n");
				ret = ioctl(pfd[0].fd, CEC_DQEVENT, &event);
				if (!ret) {
					printf("event:%02x\n", event.event);
					if (event.event == CEC_EVENT_PIN_HPD_LOW)
						state = 0;
					else if (event.event == CEC_EVENT_PIN_HPD_HIGH)
						state = 1;
					if (state >= 0) {
						cec_event.type = HDMI_EVENT_HOT_PLUG;
						cec_event.dev = &ctx->device;
						cec_event.hotplug.connected = state;
						cec_event.hotplug.port_id = HDMI_CEC_PORT_ID;
						if (ctx->event_callback)
							ctx->event_callback(&cec_event, ctx->cec_arg);
					}
				} else {
					ALOGE("%s cec event get err\n", __func__);
				}
			}
		} else {
			ALOGE("%s: cec poll failed errno: %s", __FUNCTION__,
		              strerror(errno));
			continue;
		}
  }

	return NULL;
}

void init_uevent_thread(hdmi_cec_context_t* ctx)
{
	pthread_t uevent_thread;
	int ret;

	ALOGI("Initializing UEVENT Thread");
	ret = pthread_create(&uevent_thread, NULL, uevent_loop, (void*) ctx);
	if (ret) {
		ALOGE("%s: failed to create %s: %s", __FUNCTION__,
			HDMI_CEC_UEVENT_THREAD_NAME, strerror(ret));
	}
}

