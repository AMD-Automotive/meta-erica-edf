// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

// When an image is written into /dev/mlpp_svcX_in the pipeline should:
// - Dual check, pipeline 0/1 and display pipeline
// - Kick off 1 csc,crop,scale,sub whichever pipelines 0 and 1 (8ppc and 4ppc) is free
// - Kick off 1 CSC, crop, scale on pipeline 2 (display) in order of SVC0, SVC1, SVC2, SVC3

#include "ml_pre_process.h"

ssize_t mlpp_svc_in_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
    int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("write to device %d, count=%zu\n", device_index, count);

	// Handle DMABUF parameters (small write with fd)
	if (count == sizeof(struct ml_pp_dmabuf_params)) {
		struct ml_pp_dmabuf_params dmabuf_params;
		int ret;

		/* using_dmabuf[] is set by ml_pp_import_dmabuf() only on success, so a
		 * failed import can no longer leave the flag true with a NULL buffer. */

		DEBUG_PRINT("Received DMABUF parameters, size=%zu\n", count);

		if (copy_from_user(&dmabuf_params, buf, sizeof(dmabuf_params))) {
			pr_err("Failed to copy DMABUF parameters from user\n");
			return -EFAULT;
		}
		
		DEBUG_PRINT("DMABUF fd=%d, offset=%u, size=%u, sequence=%u\n",
		           dmabuf_params.dmabuf_fd, dmabuf_params.offset, 
		           dmabuf_params.size, dmabuf_params.frame_sequence);
		
		
		// Ensure THIS camera's previous frame finished reading its DMABUF
		// before we replace/unmap it. Writes to other cameras keep running.
		mlpp_svc_wait_src_idle(device_index - 1);

		// Import and map the DMABUF and free old DMABUFs
		ret = ml_pp_import_dmabuf(global_priv, dmabuf_params.dmabuf_fd, device_index, dmabuf_params.size);
		if (ret) {
			pr_err("Failed to import DMABUF fd %d: %d\n", 
			       dmabuf_params.dmabuf_fd, ret);
			return ret;
		}
		
		// Update hardware to use DMABUF as input
		ret = ml_pp_use_dmabuf_as_input(global_priv, device_index);
		if (ret) {
			pr_err("Failed to configure hardware for DMABUF input: %d\n", ret);
			ml_pp_cleanup_dmabuf(global_priv, device_index);
			return ret;
		}

		// Start the pipeline with DMABUF input
		input_byte_count[device_index] = 0;

		usleep_range(500, 1000); // TODO look into this

		// Run one SVC hardware frame for this camera (ROI0 -> svcN_out,
		// ROI1 -> svcN_display) and publish to the output double-buffers.
		PERF_PRINT("PIPE SVC-DMABUF, START %d, NS=%lld\n", device_index, ktime_get_real_ns() );
		mlpp_process_svc_frame(device_index - 1);

		// Note: DMABUF will be cleaned up when new one is imported or device is closed 
		
		return count;
	}

	if (count == sizeof(struct ml_pp_input_params)) {
        DEBUG_PRINT("Requesting SVC ml pre-processing param change.. \n");
        DEBUG_PRINT("Expected struct size: %zu, received count: %zu\n", sizeof(struct ml_pp_input_params), count);
        
        struct ml_pp_input_params *requested_params;
        requested_params = kmalloc(sizeof(struct ml_pp_input_params), GFP_KERNEL);
		if (!requested_params) {
			pr_err("Failed to allocate memory for requested_params\n");
			return -ENOMEM;
		}

		// Initialize to known values for debugging
		memset(requested_params, 0, sizeof(struct ml_pp_input_params));
        if (copy_from_user(requested_params, buf, sizeof(*requested_params))) {
			kfree(requested_params);
			return -EFAULT;
		}

        DEBUG_PRINT("SVC Input %d, (%u x %u ) fmt (%u)\n", device_index, requested_params->width_in, requested_params->height_in, requested_params->color_in);

		// Overwrite pipeline settings
		memcpy(&mlpp_input_params[device_index], requested_params, sizeof(*requested_params));
		DEBUG_PRINT("Changed SVC ml pre-processing param change.. \n");
		kfree(requested_params);
    }
	else {
		int image_in_size = 0;
		struct ml_pp_input_params *input_params;
		// Use reference to global array instead of unnecessary kmalloc
		input_params = &mlpp_input_params[device_index];
		// 0 - RGB/YUV444P , 1 - YUV444M, 2 - UYVY 4:2:2, 3 - NV12
		switch (input_params->color_in) {
			case MLPP_COLOR_RGB888:
			case MLPP_COLOR_YUV888M:
				image_in_size = input_params->width_in * input_params->height_in * 3;
				break;
			case MLPP_COLOR_UYVY_422:
				image_in_size = input_params->width_in * input_params->height_in * 2;
				break;
			case MLPP_COLOR_UYVY_420:
				image_in_size = (input_params->width_in * input_params->height_in * 3) >> 1; // 1.5 bytes per pixel
				break;
			default:
				pr_err("Unsupported input color format %d\n", input_params->color_in);
				return -EINVAL;
		}

		/* Bound the accumulating copy against the input slot -- see the same
		 * check in mlpp_ffc_in_write(). */
		if (input_byte_count[device_index] + count > mlpp_slot_payload(device_index)) {
			pr_err("SVC%d input write of %zu at offset %zu exceeds slot payload %zu\n",
			       device_index, count, input_byte_count[device_index],
			       mlpp_slot_payload(device_index));
			return -EINVAL;
		}

		if (copy_from_user(mlpp_input_write_cpu(device_index) + input_byte_count[device_index], buf, count))
			return -EFAULT;

		DEBUG_PRINT("mlpp_svc_in: Device %d: input buf = %px, offset = %zu, total_written = %zu\n", 
		       device_index, mlpp_input_write_cpu(device_index), 
		       input_byte_count[device_index], input_byte_count[device_index] + count);

		input_byte_count[device_index] += count; // Update the global byte count
		// After receiving a new image kick off pipeline
		if (input_byte_count[device_index] >= image_in_size)
		{
			input_byte_count[device_index] = 0; // Reset for next frame

			PERF_PRINT("PIPE SVC-MEM, START %d, NS=%lld\n", device_index, ktime_get_real_ns() );

			// Run one SVC hardware frame for this camera (ROI0 -> svcN_out,
			// ROI1 -> svcN_display).
			mlpp_process_svc_frame(device_index - 1);

		}
	}

    return count;
}

ssize_t mlpp_svc_in_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
   	int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("NOT SUPPORTED: read from device %d, count=%zu\n", device_index, count);
	
	return 0;
}

int mlpp_svc_in_release(struct inode *inode, struct file *file)
{
	int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("releasing device %d\n", device_index);

	if (global_priv) {
		// Decrement active opens counter
		atomic_dec(&global_priv->active_opens[device_index]);
		DEBUG_PRINT("Device %d closing: active_opens=%d\n", device_index,
		           atomic_read(&global_priv->active_opens[device_index]));
		
		mlpp_svc_wait_src_idle(device_index - 1);
		ml_pp_cleanup_dmabuf(global_priv, device_index);
	}

	return 0;
}

int mlpp_svc_in_open(struct inode *inode, struct file *file)
{
	int device_index = iminor(inode);  // Get the minor number (0-19)
	DEBUG_PRINT("open %s \n", mlpp_device_names[device_index]);

	if (global_priv) {
		// Increment active opens counter
		atomic_inc(&global_priv->active_opens[device_index]);
		DEBUG_PRINT("Device %d opened: active_opens=%d\n", device_index, 
		           atomic_read(&global_priv->active_opens[device_index]));
	}

	// Store device index in file private data for use in other operations
	file->private_data = (void *)(long)device_index;
	
	return 0;
}

