// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * drm_helper.cpp
 * Direct Rendering Manager (DRM) helper functions for display initialization and control.
 * Author: LT Thomas
 */

#include "drm_helper.h"
#include <iostream>

#define LOG(msg, ...) fprintf( stderr, "[%s] " msg, __func__,  ##__VA_ARGS__ )

#ifndef DRM_FORMAT_RGB888
#define DRM_FORMAT_RGB888 0x34324752  // RG24 format
#endif


DrmHelper::DrmHelper() {
    	std::cout << "drm_helper constructor (default) called." << std::endl;
    	drmModeRes         * __restrict drm_resources;
	valid_connector   = NULL;
	chosen_resolution = NULL;
	screen_encoder    = NULL;

	int ret = 0;

	/* Open the DRM device node and get a File Descriptor */
	drm_fd = open("/dev/dri/by-path/platform-edd00000.mmi_dc-card", O_RDWR | O_CLOEXEC);

	if (drm_fd < 0) {
		LOG("Could not open /dev/dri/by-path/platform-edd00000.mmi_dc-card : %m\n");
		return;
	}


	drm_resources = drmModeGetResources(drm_fd);
	/* Get a valid connector */
	for (int_fast32_t c = 0; c < drm_resources->count_connectors; c++) {
		valid_connector =
			drmModeGetConnector(drm_fd, drm_resources->connectors[c]);

		if (valid_connector->connection == DRM_MODE_CONNECTED)
			break;

		drmModeFreeConnector(valid_connector);
		valid_connector = NULL;
	}
	if (!valid_connector) {
		LOG("No connectors or no connected connectors found...\n");
		ret = -ENOLINK;
		close(drm_fd);
		return;
	}

	/* Get the 3840x2160 resolution */
	for (int_fast32_t m = 0; m < valid_connector->count_modes; m++) {
		drmModeModeInfo * __restrict tested_resolution =
			&valid_connector->modes[m];
		if (tested_resolution->hdisplay == 3840 && tested_resolution->vdisplay == 2160) {
			chosen_resolution = tested_resolution;
			break;
		}
	}
	if (!chosen_resolution) {
		LOG(
			"No preferred resolution on the selected connector %u ?\n",
			valid_connector->connector_id
		);
		ret = -ENAVAIL;
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}
	
	LOG("Found resolution: %dx%d @ %dHz\n", chosen_resolution->hdisplay, chosen_resolution->vdisplay, chosen_resolution->vrefresh);

	/* Get an encoder that will transform CRTC data */
	screen_encoder =
		drmModeGetEncoder(drm_fd, valid_connector->encoder_id);
	if (!screen_encoder) {
		LOG(
			"Could not retrieve the encoder for mode %s on connector %u.\n",
			chosen_resolution->name,
			valid_connector->connector_id
		);
		ret = -ENOLINK;
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}
	
	LOG(
			"encoder for mode %s on connector %u.\n",
			chosen_resolution->name,
			valid_connector->connector_id
		);

	/* Request a dumb buffer */
	create_request = {
		.height = 2160,
		.width  = 3840,		
		.bpp    = 24  // Changed from 32 to 24 for RGB888
	};
	
	ret = ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_request);
	if (ret) {
		LOG(
			"Dumb Buffer Object Allocation request of %ux%u@%u failed : %s\n",
			create_request.width, create_request.height,
			create_request.bpp,
			strerror(ret)
		);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	/* Create a framebuffer, using the new method drmModeAddFB2 */
	uint32_t handles[4] = {create_request.handle, 0, 0, 0};
	uint32_t strides[4] = {create_request.pitch, 0, 0, 0};
	uint32_t offsets[4] = {0, 0, 0, 0};
	
	ret = drmModeAddFB2(
		drm_fd,
		3840, 2160,
		DRM_FORMAT_RGB888,  // RGB24 format (24-bit with 8-bit R, G, B)
		handles, strides, offsets,
		&frame_buffer_id, 0
	);
	
	LOG(
			"x and y : %d %d\n",
			3840,
			2160
		);
	if (ret) {
		LOG(
			"Could not add a framebuffer using drmModeAddFB : %s\n",
			strerror(ret)
		);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	uint32_t current_crtc_id = 43; // Use CRTC 40

	/* Backup the informations of the CRTC to restore later. */
	crtc_to_restore = drmModeGetCrtc(drm_fd, current_crtc_id);
	if (!crtc_to_restore) {
		LOG("Could not retrieve the current CRTC with a valid ID !\n");
		drmModeRmFB(drm_fd, frame_buffer_id);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}
	LOG("current CRTC is %d FB is %d \n", current_crtc_id, frame_buffer_id);
	
	/* Set the CRTC so that it uses our new framebuffer */
	ret = drmModeSetCrtc(
		drm_fd, current_crtc_id, frame_buffer_id,
		0, 0,
		&valid_connector->connector_id,
		1,
		chosen_resolution);

	/* Export our dumb buffer using PRIME */
	struct drm_prime_handle prime_request = {
		.handle = create_request.handle,
		.flags  = DRM_CLOEXEC | DRM_RDWR,
		.fd     = -1
	};

	ret = ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_request);
	int const dma_buf_fd = prime_request.fd;
	if (ret || dma_buf_fd < 0) {
		LOG(
			"Could not export buffer : %s (%d) - FD : %d\n",
			strerror(ret), ret,
			dma_buf_fd
 		);
		//could_not_export_buffer;
		drmModeRmFB(drm_fd, frame_buffer_id);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	/* Map the exported buffer, using the PRIME File descriptor */
	primed_framebuffer = static_cast<uint8_t*>(mmap(
		0, create_request.size,	PROT_READ | PROT_WRITE, MAP_SHARED,
		dma_buf_fd, 0));
	ret = errno;

	/* Bail out if we could not map the framebuffer using this method */
	if (primed_framebuffer == NULL || primed_framebuffer == MAP_FAILED) {
		LOG(
			"Could not map buffer exported through PRIME : %s (%d)\n"
			"Buffer : %p\n",
			strerror(ret), ret,
			primed_framebuffer
		);
		//could_not_map_buffer;
		drmModeRmFB(drm_fd, frame_buffer_id);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeModeInfo(chosen_resolution);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	LOG("Buffer mapped !\n");

	uint_fast64_t size = create_request.size;

	/* Cleanup the framebuffer */
	memset(primed_framebuffer, 0, size);
	LOG("size is %ld\n", size); //17694720

}


DrmHelper::~DrmHelper() {
	std::cout << "drm_helper destructor called." << std::endl;
	munmap(primed_framebuffer, create_request.size);

	{
		struct drm_mode_destroy_dumb destroy_request = {
			.handle = create_request.handle
		};
		
		ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_request);
	}
	drmModeSetCrtc(
		drm_fd,
		crtc_to_restore->crtc_id, crtc_to_restore->buffer_id,
		0, 0, &valid_connector->connector_id, 1, &crtc_to_restore->mode
	);
	drmModeRmFB(drm_fd, frame_buffer_id);
	drmModeFreeEncoder(screen_encoder);
	drmModeFreeModeInfo(chosen_resolution);
	drmModeFreeConnector(valid_connector);
	close(drm_fd);
}


void DrmHelper::DrmWriteOutput( cv::Mat* cv_bgr_buffer  ) {
	// Fast row-by-row processing with direct memory access for RGB888
	const uint8_t* src_data = cv_bgr_buffer->data;
	const int src_step = cv_bgr_buffer->step[0]; // Row stride in bytes
	
	uint8_t* dst_row = primed_framebuffer;
	const uint32_t dst_pitch_bytes = create_request.pitch; // Pitch in bytes
	
	for (uint32_t y = 0; y < 2160; ++y) {
		const uint8_t* src_row = src_data + y * src_step;
		uint8_t* dst_pixel = dst_row + y * dst_pitch_bytes;
		
		// For RGB888, we need to convert BGR to RGB and copy 3 bytes per pixel
		for (uint32_t x = 0; x < 3840; ++x) {
			// Convert BGR to RGB: swap B and R channels
			dst_pixel[0] = src_row[2]; // R
			dst_pixel[1] = src_row[1]; // G
			dst_pixel[2] = src_row[0]; // B
			
			src_row += 3;   // Move to next BGR pixel
			dst_pixel += 3; // Move to next RGB pixel
		}
	}
}

// Alternative ultra-fast version using direct copy without color conversion
void DrmHelper::DrmWriteOutputFast( cv::Mat* cv_bgr_buffer  ) {
	// Create a Mat that wraps the DRM framebuffer as BGR (3 channels)
	cv::Mat drm_frame(2160, 3840, CV_8UC3, primed_framebuffer, create_request.pitch);
	
	// Direct copy without color conversion - assumes input is already in correct format
	cv_bgr_buffer->copyTo(drm_frame);
}

// VSync-aware version with wait for vertical blank
void DrmHelper::DrmWriteOutputVSync( cv::Mat* cv_bgr_buffer  ) {
	// Wait for VBlank to ensure smooth display without tearing
	drmVBlank vblank = {};
	vblank.request.type = DRM_VBLANK_RELATIVE;
	vblank.request.sequence = 1;
	drmWaitVBlank(drm_fd, &vblank);
	
	// Use direct copy without color conversion for RGB888
	cv::Mat drm_frame(2160, 3840, CV_8UC3, primed_framebuffer, create_request.pitch);
	cv_bgr_buffer->copyTo(drm_frame);
	
}

// Get direct access to DRM framebuffer as cv::Mat
cv::Mat DrmHelper::getDrmFramebuffer() {
	// Return a Mat that wraps the DRM framebuffer memory (zero-copy)
	// Applications can write directly to this Mat to avoid extra copies
	return cv::Mat(2160, 3840, CV_8UC3, primed_framebuffer, create_request.pitch);
}

