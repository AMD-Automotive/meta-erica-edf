// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * drm_helper.cpp
 * Direct Rendering Manager (DRM) helper functions for display initialization and control.
 * 
 * Supports RGB888 and NV12 outputs formats
 * Can configure for 1920x1080 or 3840x2160
 * Can configure for 30FPS or 60FPS
 * 
 * Author: LT Thomas
 */

#include "drm_helper.h"
#include <iostream>

#define LOG(msg, ...) fprintf( stderr, "[%s] " msg, __func__,  ##__VA_ARGS__ )

#ifndef DRM_FORMAT_RGB888
#define DRM_FORMAT_RGB888 0x34324752  // RG24 format
#endif

#ifndef DRM_FORMAT_NV12
#define DRM_FORMAT_NV12 0x3231564e  // NV12: YUV 4:2:0 semi-planar
#endif


DrmHelper::DrmHelper() : DrmHelper(1920, 1080, 60, DRM_OUT_RGB888) {
	// Default display mode: 1920x1080@60Hz, RGB888
}

DrmHelper::DrmHelper(uint32_t width, uint32_t height, uint32_t refresh_hz,
                     DrmOutputFormat format)
	: primed_framebuffer(nullptr),
	  drm_fd(-1),
	  crtc_to_restore(nullptr),
	  valid_connector(nullptr),
	  chosen_resolution(nullptr),
	  screen_encoder(nullptr),
	  frame_buffer_id(0),
	  display_width(width),
	  display_height(height),
	  display_refresh_hz(refresh_hz),
	  output_format(format),
	  drm_ready(false) {
	std::cout << "drm_helper constructor ("
	          << (format == DRM_OUT_NV12 ? "NV12" : "RGB888")
	          << ") called." << std::endl;
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

	/* Get the requested resolution (and refresh if specified) */
	int best_refresh_delta = INT32_MAX;
	for (int_fast32_t m = 0; m < valid_connector->count_modes; m++) {
		drmModeModeInfo * __restrict tested_resolution =
			&valid_connector->modes[m];
		if ((uint32_t)tested_resolution->hdisplay == display_width &&
		    (uint32_t)tested_resolution->vdisplay == display_height) {
			if (display_refresh_hz == 0) {
				chosen_resolution = tested_resolution;
				break;
			}

			int delta = std::abs((int)tested_resolution->vrefresh - (int)display_refresh_hz);
			if (delta < best_refresh_delta) {
				best_refresh_delta = delta;
				chosen_resolution = tested_resolution;
				if (delta == 0) {
					break;
				}
			}
		}
	}
	if (!chosen_resolution) {
		LOG(
			"No matching mode %ux%u@%u on connector %u.\n",
			display_width,
			display_height,
			display_refresh_hz,
			valid_connector->connector_id
		);
		ret = -ENAVAIL;
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	/* Get an encoder that will transform CRTC data.
	 * On a cold boot the connector often has no encoder bound yet
	 * (encoder_id == 0), so fall back to probing the encoders the
	 * connector advertises until one resolves. */
	screen_encoder = NULL;
	if (valid_connector->encoder_id) {
		screen_encoder =
			drmModeGetEncoder(drm_fd, valid_connector->encoder_id);
	}
	if (!screen_encoder) {
		for (int_fast32_t e = 0; e < valid_connector->count_encoders; e++) {
			screen_encoder =
				drmModeGetEncoder(drm_fd, valid_connector->encoders[e]);
			if (screen_encoder)
				break;
		}
	}
	if (!screen_encoder) {
		LOG(
			"Could not retrieve the encoder for mode %s on connector %u.\n",
			chosen_resolution->name,
			valid_connector->connector_id
		);
		ret = -ENOLINK;
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}
	
	LOG(
			"encoder for mode %s on connector %u.\n",
			chosen_resolution->name,
			valid_connector->connector_id
		);

	/* Request a dumb buffer.
	 * RGB888: one packed plane at 24 bpp, height = display_height.
	 * NV12  : 8 bpp allocation tall enough for the Y plane followed by the
	 *         half-height interleaved UV plane (display_height * 3 / 2 rows). */
	const bool fb_is_nv12 = (output_format == DRM_OUT_NV12);
	create_request = {};
	create_request.width  = display_width;
	create_request.height = fb_is_nv12 ? (display_height * 3 / 2) : display_height;
	create_request.bpp    = fb_is_nv12 ? 8 : 24;
	
	ret = ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_request);
	if (ret) {
		LOG(
			"Dumb Buffer Object Allocation request of %ux%u@%u failed : %s\n",
			create_request.width, create_request.height,
			create_request.bpp,
			strerror(ret)
		);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	/* Create a framebuffer, using the new method drmModeAddFB2 */
	uint32_t handles[4] = {0, 0, 0, 0};
	uint32_t strides[4] = {0, 0, 0, 0};
	uint32_t offsets[4] = {0, 0, 0, 0};
	uint32_t fb_format;

	if (fb_is_nv12) {
		// NV12: two planes sharing one buffer object.
		// Plane 0 = Y; plane 1 = interleaved UV at offset pitch*display_height.
		handles[0] = create_request.handle;
		handles[1] = create_request.handle;
		strides[0] = create_request.pitch;
		strides[1] = create_request.pitch;
		offsets[0] = 0;
		offsets[1] = create_request.pitch * display_height;
		fb_format  = DRM_FORMAT_NV12;
	} else {
		handles[0] = create_request.handle;
		strides[0] = create_request.pitch;
		offsets[0] = 0;
		fb_format  = DRM_FORMAT_RGB888;
	}
	
	ret = drmModeAddFB2(
		drm_fd,
		display_width, display_height,
		fb_format,  // RGB888 or NV12 depending on configured output format
		handles, strides, offsets,
		&frame_buffer_id, 0
	);
	
	LOG(
			"x and y : %d %d\n",
			display_width,
			display_height
		);
	if (ret) {
		LOG(
			"Could not add a framebuffer using drmModeAddFB : %s\n",
			strerror(ret)
		);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	/* Detect a usable CRTC
	 * Prefer the CRTC already bound to the encoder; otherwise pick the
	 * first CRTC allowed by the encoder's possible_crtcs bitmask. */
	uint32_t current_crtc_id = screen_encoder->crtc_id;
	if (!current_crtc_id) {
		for (int_fast32_t i = 0; i < drm_resources->count_crtcs; i++) {
			if (screen_encoder->possible_crtcs & (1u << i)) {
				current_crtc_id = drm_resources->crtcs[i];
				break;
			}
		}
	}
	if (!current_crtc_id) {
		LOG("Could not detect a usable CRTC for connector %u.\n",
		    valid_connector->connector_id);
		drmModeRmFB(drm_fd, frame_buffer_id);
		drmModeFreeEncoder(screen_encoder);
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}
	LOG("Detected CRTC id %u for connector %u.\n",
	    current_crtc_id, valid_connector->connector_id);

	/* Backup the informations of the CRTC to restore later. */
	crtc_to_restore = drmModeGetCrtc(drm_fd, current_crtc_id);
	if (!crtc_to_restore) {
		LOG("Could not retrieve the current CRTC with a valid ID !\n");
		drmModeRmFB(drm_fd, frame_buffer_id);
		drmModeFreeEncoder(screen_encoder);
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
		drmModeFreeConnector(valid_connector);
		close(drm_fd);
		return;
	}

	LOG("Buffer mapped !\n");

	uint_fast64_t size = create_request.size;

	/* Cleanup the framebuffer */
	memset(primed_framebuffer, 0, size);
	LOG("size is %ld\n", size); //17694720
	drm_ready = true;

}


DrmHelper::~DrmHelper() {
	std::cout << "drm_helper destructor called." << std::endl;
	if (!drm_ready) {
		return;
	}

	if (primed_framebuffer && primed_framebuffer != MAP_FAILED) {
		munmap(primed_framebuffer, create_request.size);
	}

	{
		struct drm_mode_destroy_dumb destroy_request = {
			.handle = create_request.handle
		};
		
		ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_request);
	}
	if (crtc_to_restore && valid_connector) {
		drmModeSetCrtc(
			drm_fd,
			crtc_to_restore->crtc_id, crtc_to_restore->buffer_id,
			0, 0, &valid_connector->connector_id, 1, &crtc_to_restore->mode
		);
	}
	if (frame_buffer_id != 0) {
		drmModeRmFB(drm_fd, frame_buffer_id);
	}
	if (screen_encoder) drmModeFreeEncoder(screen_encoder);
	if (valid_connector) drmModeFreeConnector(valid_connector);
	if (drm_fd >= 0) close(drm_fd);
}


void DrmHelper::DrmWriteOutput( cv::Mat* cv_bgr_buffer  ) {
	if (!drm_ready || cv_bgr_buffer == nullptr || cv_bgr_buffer->empty()) {
		return;
	}

	cv::Mat src_view;
	if ((uint32_t)cv_bgr_buffer->cols != display_width ||
	    (uint32_t)cv_bgr_buffer->rows != display_height) {
		cv::resize(*cv_bgr_buffer, src_view,
		           cv::Size((int)display_width, (int)display_height),
		           0, 0, cv::INTER_LINEAR);
	} else {
		src_view = *cv_bgr_buffer;
	}

	// Fast row-by-row processing with direct memory access for RGB888
	const uint8_t* src_data = src_view.data;
	const int src_step = src_view.step[0]; // Row stride in bytes
	
	uint8_t* dst_row = primed_framebuffer;
	const uint32_t dst_pitch_bytes = create_request.pitch; // Pitch in bytes
	
	for (uint32_t y = 0; y < display_height; ++y) {
		const uint8_t* src_row = src_data + y * src_step;
		uint8_t* dst_pixel = dst_row + y * dst_pitch_bytes;
		
		// For RGB888, we need to convert BGR to RGB and copy 3 bytes per pixel
		for (uint32_t x = 0; x < display_width; ++x) {
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
	if (!drm_ready || cv_bgr_buffer == nullptr || cv_bgr_buffer->empty()) {
		return;
	}

	// Create a Mat that wraps the DRM framebuffer as BGR (3 channels)
	cv::Mat drm_frame((int)display_height, (int)display_width,
	                  CV_8UC3, primed_framebuffer, create_request.pitch);
	
	if ((uint32_t)cv_bgr_buffer->cols == display_width &&
	    (uint32_t)cv_bgr_buffer->rows == display_height) {
		cv_bgr_buffer->copyTo(drm_frame);
	} else {
		cv::Mat resized;
		cv::resize(*cv_bgr_buffer, resized,
		           cv::Size((int)display_width, (int)display_height),
		           0, 0, cv::INTER_LINEAR);
		resized.copyTo(drm_frame);
	}
}

// VSync-aware version with wait for vertical blank
void DrmHelper::DrmWriteOutputVSync( cv::Mat* cv_bgr_buffer  ) {
	if (!drm_ready || cv_bgr_buffer == nullptr || cv_bgr_buffer->empty()) {
		return;
	}

	// Wait for VBlank to ensure smooth display without tearing
	drmVBlank vblank = {};
	vblank.request.type = DRM_VBLANK_RELATIVE;
	vblank.request.sequence = 1;
	drmWaitVBlank(drm_fd, &vblank);
	
	// Use direct copy without color conversion for RGB888
	cv::Mat drm_frame((int)display_height, (int)display_width,
	                  CV_8UC3, primed_framebuffer, create_request.pitch);
	if ((uint32_t)cv_bgr_buffer->cols == display_width &&
	    (uint32_t)cv_bgr_buffer->rows == display_height) {
		cv_bgr_buffer->copyTo(drm_frame);
	} else {
		cv::Mat resized;
		cv::resize(*cv_bgr_buffer, resized,
		           cv::Size((int)display_width, (int)display_height),
		           0, 0, cv::INTER_LINEAR);
		resized.copyTo(drm_frame);
	}
	
}

// Write a packed NV12 frame directly into the NV12 framebuffer (no conversion).
void DrmHelper::DrmWriteOutputNV12( cv::Mat* cv_nv12_buffer ) {
	if (!drm_ready || cv_nv12_buffer == nullptr || cv_nv12_buffer->empty()) {
		return;
	}
	if (output_format != DRM_OUT_NV12) {
		LOG("DrmWriteOutputNV12 called but framebuffer is not NV12.\n");
		return;
	}
	blitNv12ToFramebuffer(*cv_nv12_buffer);
}

// VSync-aware packed NV12 write.
void DrmHelper::DrmWriteOutputNV12VSync( cv::Mat* cv_nv12_buffer ) {
	if (!drm_ready || cv_nv12_buffer == nullptr || cv_nv12_buffer->empty()) {
		return;
	}
	if (output_format != DRM_OUT_NV12) {
		LOG("DrmWriteOutputNV12VSync called but framebuffer is not NV12.\n");
		return;
	}

	// Wait for VBlank to ensure smooth display without tearing
	drmVBlank vblank = {};
	vblank.request.type = DRM_VBLANK_RELATIVE;
	vblank.request.sequence = 1;
	drmWaitVBlank(drm_fd, &vblank);

	blitNv12ToFramebuffer(*cv_nv12_buffer);
}

// Copy a packed NV12 Mat (rows = display_height*3/2, cols = display_width,
// CV_8UC1) into the NV12 framebuffer, honoring the framebuffer pitch and the
// UV plane offset. The input's own row stride (step) is respected so callers
// may pass non-contiguous Mats.
void DrmHelper::blitNv12ToFramebuffer(const cv::Mat& nv12) {
	const int expected_rows = (int)(display_height * 3 / 2);
	if (nv12.cols != (int)display_width || nv12.rows != expected_rows ||
	    nv12.type() != CV_8UC1) {
		LOG("blitNv12ToFramebuffer: size/type mismatch (got %dx%d type %d, "
		    "expected %ux%d CV_8UC1).\n",
		    nv12.cols, nv12.rows, nv12.type(), display_width, expected_rows);
		return;
	}

	const uint32_t pitch     = create_request.pitch;
	const uint32_t uv_offset = pitch * display_height;
	const size_t   src_step  = nv12.step[0];

	// Y plane: display_height rows of display_width bytes.
	for (uint32_t y = 0; y < display_height; ++y) {
		memcpy(primed_framebuffer + (size_t)y * pitch,
		       nv12.data + (size_t)y * src_step,
		       display_width);
	}

	// Interleaved UV plane: display_height/2 rows of display_width bytes,
	// located immediately after the Y rows in the source Mat.
	const uint8_t* uv_src = nv12.data + (size_t)display_height * src_step;
	for (uint32_t row = 0; row < display_height / 2; ++row) {
		memcpy(primed_framebuffer + uv_offset + (size_t)row * pitch,
		       uv_src + (size_t)row * src_step,
		       display_width);
	}
}

// Overlay an RGBX (CV_8UC4) image onto a packed NV12 Mat at pixel (x, y).
// The NV12 destination is (H*3/2) rows x W cols, CV_8UC1: a full-size Y plane
// followed by a half-height interleaved UV plane. RGBX channel order is
// R, G, B, X (pass a BGRX Mat through cv::COLOR_BGRA2BGR instead if needed).
void DrmHelper::overlay_rgbx_data_2_nv12(int x, int y, cv::Mat& rgbx,
                                         cv::Mat& nv12_dst) {
	if (rgbx.empty() || nv12_dst.empty() ||
	    rgbx.type() != CV_8UC4 || nv12_dst.type() != CV_8UC1) {
		LOG("overlay_rgbx_data_2_nv12: bad input (rgbx type %d, nv12 type %d).\n",
		    rgbx.type(), nv12_dst.type());
		return;
	}

	const int dst_w = nv12_dst.cols;
	const int dst_h = nv12_dst.rows * 2 / 3; // Y-plane height

	// Align the destination origin to even coordinates for 4:2:0 chroma.
	x &= ~1;
	y &= ~1;
	if (x < 0 || y < 0 || x >= dst_w || y >= dst_h) {
		return;
	}

	// Clip the copied region to the destination, keeping even dimensions.
	int w = rgbx.cols;
	int h = rgbx.rows;
	if (x + w > dst_w) w = dst_w - x;
	if (y + h > dst_h) h = dst_h - y;
	w &= ~1;
	h &= ~1;
	if (w <= 0 || h <= 0) {
		return;
	}

	// Convert the (clipped) RGBX region to Y, U, V components (BT.601).
	cv::Mat rgbx_roi = rgbx(cv::Rect(0, 0, w, h));
	cv::Mat bgr, yuv;
	cv::cvtColor(rgbx_roi, bgr, cv::COLOR_RGBA2BGR);
	cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV); // 3 channels: Y, U, V per pixel

	// Y plane: one byte per pixel.
	for (int j = 0; j < h; ++j) {
		const uint8_t* src = yuv.ptr<uint8_t>(j);
		uint8_t* ydst = nv12_dst.ptr<uint8_t>(y + j) + x;
		for (int i = 0; i < w; ++i) {
			ydst[i] = src[i * 3 + 0];
		}
	}

	// Interleaved UV plane: one (U, V) pair per 2x2 block, sampled at the
	// top-left pixel of each block.
	for (int j = 0; j < h; j += 2) {
		const uint8_t* src = yuv.ptr<uint8_t>(j);
		uint8_t* uvdst = nv12_dst.ptr<uint8_t>(dst_h + (y + j) / 2) + x;
		for (int i = 0; i < w; i += 2) {
			uvdst[i + 0] = src[i * 3 + 1]; // U
			uvdst[i + 1] = src[i * 3 + 2]; // V
		}
	}
}

// Overlay a YUVX (CV_8UC4, channel order Y, U, V, X) image onto a packed NV12
// Mat at pixel (x, y). The NV12 destination is (H*3/2) rows x W cols, CV_8UC1:
// a full-size Y plane followed by a half-height interleaved UV plane. Unlike
// overlay_rgbx_data_2_nv12 no color conversion is done; the Y/U/V samples are
// taken straight from the source channels.
void DrmHelper::overlay_yuvx_data_2_nv12(int x, int y, cv::Mat& yuvx,
                                         cv::Mat& nv12_dst) {
	if (yuvx.empty() || nv12_dst.empty() ||
	    yuvx.type() != CV_8UC4 || nv12_dst.type() != CV_8UC1) {
		LOG("overlay_yuvx_data_2_nv12: bad input (yuvx type %d, nv12 type %d).\n",
		    yuvx.type(), nv12_dst.type());
		return;
	}

	const int dst_w = nv12_dst.cols;
	const int dst_h = nv12_dst.rows * 2 / 3; // Y-plane height

	// Align the destination origin to even coordinates for 4:2:0 chroma.
	x &= ~1;
	y &= ~1;
	if (x < 0 || y < 0 || x >= dst_w || y >= dst_h) {
		return;
	}

	// Clip the copied region to the destination, keeping even dimensions.
	int w = yuvx.cols;
	int h = yuvx.rows;
	if (x + w > dst_w) w = dst_w - x;
	if (y + h > dst_h) h = dst_h - y;
	w &= ~1;
	h &= ~1;
	if (w <= 0 || h <= 0) {
		return;
	}

	// Y plane: copy channel 0 directly, one byte per pixel.
	for (int j = 0; j < h; ++j) {
		const uint8_t* src = yuvx.ptr<uint8_t>(j);
		uint8_t* ydst = nv12_dst.ptr<uint8_t>(y + j) + x;
		for (int i = 0; i < w; ++i) {
			ydst[i] = src[i * 4 + 0]; // Y
		}
	}

	// Interleaved UV plane: one (U, V) pair per 2x2 block, sampled at the
	// top-left pixel of each block (channels 1 and 2).
	for (int j = 0; j < h; j += 2) {
		const uint8_t* src = yuvx.ptr<uint8_t>(j);
		uint8_t* uvdst = nv12_dst.ptr<uint8_t>(dst_h + (y + j) / 2) + x;
		for (int i = 0; i < w; i += 2) {
			uvdst[i + 0] = src[i * 4 + 1]; // U
			uvdst[i + 1] = src[i * 4 + 2]; // V
		}
	}
}

// Overlay a packed NV12 Mat (CV_8UC1, rows = src_h*3/2, cols = src_w) onto
// another packed NV12 Mat at pixel (x, y). Both buffers share the NV12 layout
// (full-size Y plane followed by a half-height interleaved UV plane), so the
// planes are copied straight across with no conversion or subsampling.
void DrmHelper::overlay_nv12_data_2_nv12(int x, int y, cv::Mat& nv12_src,
                                         cv::Mat& nv12_dst) {
	if (nv12_src.empty() || nv12_dst.empty() ||
	    nv12_src.type() != CV_8UC1 || nv12_dst.type() != CV_8UC1) {
		LOG("overlay_nv12_data_2_nv12: bad input (src type %d, dst type %d).\n",
		    nv12_src.type(), nv12_dst.type());
		return;
	}

	const int dst_w = nv12_dst.cols;
	const int dst_h = nv12_dst.rows * 2 / 3; // destination Y-plane height
	const int src_w = nv12_src.cols;
	const int src_h = nv12_src.rows * 2 / 3; // source Y-plane height

	// Align the destination origin to even coordinates for 4:2:0 chroma.
	x &= ~1;
	y &= ~1;
	if (x < 0 || y < 0 || x >= dst_w || y >= dst_h) {
		return;
	}

	// Clip the copied region to both source and destination, keeping even dims.
	int w = src_w;
	int h = src_h;
	if (x + w > dst_w) w = dst_w - x;
	if (y + h > dst_h) h = dst_h - y;
	w &= ~1;
	h &= ~1;
	if (w <= 0 || h <= 0) {
		return;
	}

	// Y plane: copy h rows of w bytes.
	for (int j = 0; j < h; ++j) {
		const uint8_t* src = nv12_src.ptr<uint8_t>(j);
		uint8_t* dst = nv12_dst.ptr<uint8_t>(y + j) + x;
		memcpy(dst, src, (size_t)w);
	}

	// Interleaved UV plane: copy h/2 rows of w bytes. The source UV plane
	// starts at row src_h; the destination UV plane starts at row dst_h.
	for (int j = 0; j < h; j += 2) {
		const uint8_t* src = nv12_src.ptr<uint8_t>(src_h + j / 2);
		uint8_t* dst = nv12_dst.ptr<uint8_t>(dst_h + (y + j) / 2) + x;
		memcpy(dst, src, (size_t)w);
	}
}



