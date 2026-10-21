// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

// When an image is written into /dev/mlpp_ffc_in the pipeline should:
// - Kick off 6 csc,crop,scale,sub on pipelines 0 and 1 (8ppc and 4ppc)
// - Kick off 1 CSC, crop, scale on pipeline 2 (display)

#include "ml_pre_process.h"


ssize_t mlpp_ffc_in_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
    int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("write to device %d, count=%zu\n", device_index, count);


	// Handle DMABUF parameters (small write with fd)
	if (count == sizeof(struct ml_pp_dmabuf_params)) {
		struct ml_pp_dmabuf_params dmabuf_params;
		int ret;

		DEBUG_PRINT("Received DMABUF parameters, size=%zu\n", count);

		if (copy_from_user(&dmabuf_params, buf, sizeof(dmabuf_params))) {
			pr_err("Failed to copy DMABUF parameters from user\n");
			return -EFAULT;
		}
		
		// Ensure parameter data is synchronized to memory
		rmb(); // Read memory barrier to ensure struct data is properly loaded

		DEBUG_PRINT("DMABUF fd=%d, offset=%u, size=%u, sequence=%u\n",
		           dmabuf_params.dmabuf_fd, dmabuf_params.offset, 
		           dmabuf_params.size, dmabuf_params.frame_sequence);
		
		// Ensure the previous FVC frame finished reading the old DMABUF
		// before we replace/unmap it (async pipeline safety).
		mlpp_wait_engine_idle(MLPP_ENGINE_FVC);

		// Import and map the DMABUF and free old DMABUFs
		ret = ml_pp_import_dmabuf(global_priv, dmabuf_params.dmabuf_fd, 0, dmabuf_params.size);
		if (ret) {
			pr_err("Failed to import DMABUF fd %d: %d\n", 
			       dmabuf_params.dmabuf_fd, ret);
			return ret;
		}
		
		// Update hardware to use DMABUF as input
		ret = ml_pp_use_dmabuf_as_input(global_priv, 0);
		if (ret) {
			pr_err("Failed to configure hardware for DMABUF input: %d\n", ret);
			ml_pp_cleanup_dmabuf(global_priv, 0);
			return ret;
		}

		/* Reset the FVC output devices (5..11), not device_index 20 times --
		 * the old loop reinit'd the input's own unused completion instead. */
		for (int i = 5; i <= 11; i++) {
			reinit_completion(&global_priv->read_complete[i]);
			global_priv->read_ready[i] = false;
		}

		// Start the pipeline with DMABUF input
		input_byte_count[0] = 0;

		// Validate the DMABUF is still mapped before kicking the hardware.
		if (!(global_priv->using_dmabuf[0] && global_priv->dmabuf[0])) {
			pr_err("Attempting to start pipelines but DMABUF is not valid\n");
			return -EINVAL;
		}

		PERF_PRINT("PIPE FVC-DMABUF, START X, NS=%lld\n", ktime_get_real_ns());
		// Run one FVC hardware frame (7 ROIs -> output devices 5..11) and
		// publish the results to the output double-buffers for read().
		mlpp_process_fvc_frame();

		// Note: DMABUF will be cleaned up when new one is imported or device is closed 
		
		return count;
	}

	if (count == sizeof(struct ml_pp_input_params)) {
        DEBUG_PRINT("Requesting FFC ml pre-processing param change.. \n");
        struct ml_pp_input_params *requested_params;
        requested_params = kmalloc(sizeof(struct ml_pp_input_params), GFP_KERNEL);
		if (!requested_params) {
			pr_err("Failed to allocate memory for requested_params\n");
			return -ENOMEM;
		}

        if (copy_from_user(requested_params, buf, sizeof(*requested_params))) {
			kfree(requested_params);
			return -EFAULT;
		}

		// Ensure parameter data is synchronized to memory  
		rmb(); // Read memory barrier to ensure struct data is properly loaded

        DEBUG_PRINT("FFC Input %d, (%u x %u ) fmt (%u)\n", device_index, requested_params->width_in, requested_params->height_in, requested_params->color_in);

		// Overwrite pipeline settings
		memcpy(&mlpp_input_params[device_index], requested_params, sizeof(*requested_params));
		DEBUG_PRINT("Changed FFC ml pre-processing param change.. \n");
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

		/* Bound the accumulating copy against the input slot: without this a
		 * single oversized write(), or a sequence of writes exceeding the
		 * frame size, overruns the input CMA allocation. */
		if (input_byte_count[0] + count > mlpp_slot_payload(0)) {
			pr_err("FFC input write of %zu at offset %zu exceeds slot payload %zu\n",
			       count, input_byte_count[0], mlpp_slot_payload(0));
			return -EINVAL;
		}

		if (copy_from_user(mlpp_input_write_cpu(0) + input_byte_count[0], buf, count))
			return -EFAULT;

		// Ensure cache coherency - flush CPU writes to memory before DMA access
		wmb(); // Write memory barrier to ensure copy_from_user data is visible to hardware

		input_byte_count[0] += count; // Update the global byte count
		DEBUG_PRINT("Wrote %zu bytes of %zu bytes of total %d to DMA buffer\n", count , input_byte_count[0], image_in_size);
		
		// After recieving a new image kick off pipeline
		if (input_byte_count[0] >= image_in_size)
		{
			mlpp_wait_engine_idle(MLPP_ENGINE_FVC);
			ml_pp_cleanup_dmabuf(global_priv, 0);

			/* Reset the FVC output devices (5..11). */
			for (int i = 5; i <= 11; i++) {
				reinit_completion(&global_priv->read_complete[i]);
				global_priv->read_ready[i] = false;
			}

			DEBUG_PRINT("Starting pipeline for device %d after receiving full image (%zu bytes)\n", device_index, input_byte_count[0]);

			input_byte_count[0] = 0;

			PERF_PRINT("PIPE FVC-MEM, START X, NS=%lld\n", ktime_get_real_ns());

			// Run one FVC hardware frame (7 ROIs -> output devices 5..11).
			mlpp_process_fvc_frame();
		}
	}
	return count;
}

ssize_t mlpp_ffc_in_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
   	int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("NOT SUPPORTED: read from device %d, count=%zu\n", device_index, count);
	return 0;
}

int mlpp_ffc_in_release(struct inode *inode, struct file *file)
{
    int device_index = (int)(long)file->private_data;
	// unsigned long flags;
	DEBUG_PRINT("releasing device %d\n", device_index);

	if (global_priv) {
		// Decrement active opens counter
		atomic_dec(&global_priv->active_opens[0]);
		DEBUG_PRINT("Device %d closing: active_opens=%d\n", device_index,
		           atomic_read(&global_priv->active_opens[0]));
		
		mlpp_wait_engine_idle(MLPP_ENGINE_FVC);
		ml_pp_cleanup_dmabuf(global_priv, 0);

		
		// Reset all pipeline defaults
		DEBUG_PRINT("Resetting pipeline defaults for all pipes\n");
		
		// Reset input/output byte counters for all devices
		for (int i = 0; i < 20; i++) {
			input_byte_count[i] = 0;
			output_byte_count[i] = 0;
		}
		
		// Reset read ready flags for all output buffers
		for (int i = 0; i < 20; i++) {
			global_priv->read_ready[i] = false;
		}

		DEBUG_PRINT("Pipeline defaults reset complete\n");

	}

	return 0;
}

int mlpp_ffc_in_open(struct inode *inode, struct file *file)
{
    int device_index = iminor(inode);
	DEBUG_PRINT("open %s \n", mlpp_device_names[device_index]);

	if (global_priv) {
		// Increment active opens counter
		atomic_inc(&global_priv->active_opens[0]);
		DEBUG_PRINT("Device %d opened: active_opens=%d\n", device_index, 
		           atomic_read(&global_priv->active_opens[0]));
	}

	// Store device index in file private data for use in other operations
	file->private_data = (void *)(long)device_index;

    return 0;
}

