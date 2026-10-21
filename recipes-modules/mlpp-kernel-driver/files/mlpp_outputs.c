// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

// The ouputs are mainly for reading back the CMA locations
// A write can be used to modify the CSC, Crop or MeanSubSubtract settings


#include "ml_pre_process.h"


/**
 * get_nearest_power_of_2 - Find the nearest power of 2 to a float value
 * @value: Input float value
 * 
 * Returns the nearest power of 2 as an unsigned integer.
 * For values <= 1.0, returns 1.
 * For values between two powers of 2, returns the closer one.
 * In case of exact midpoint, returns the larger power of 2.
 */
static unsigned int get_nearest_power_of_2(unsigned int value)
{
	// Implement logic using bitwise operations (e.g., for finding the next power of 2)
    unsigned int p = 1;
    if (value == 0) return 1;
    while (p < value) {
        p <<= 1;
    }
    return p;
}

/**
 * get_power_of_2_exponent - Get the exponent of a power of 2 value
 * @power_of_2: Input value that should be a power of 2
 * 
 * Returns the exponent (log base 2) of the input value.
 * For example: 1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3, etc.
 * Returns -1 if the input is not a power of 2 or is 0.
 */
static int get_power_of_2_exponent(unsigned int power_of_2)
{
    // Check if input is 0 or not a power of 2
    if (power_of_2 == 0 || (power_of_2 & (power_of_2 - 1)) != 0)
        return -1;
    
    // Use the built-in function to find the position of the single set bit
    // __builtin_ctz counts trailing zeros, which gives us the exponent
    return __builtin_ctz(power_of_2);
}

void* get_current_read_buffer_tracking_addr(int device_index)
{
	return global_priv->cpu_addr_fb[device_index][global_priv->rd_idx[device_index]];
}



ssize_t ml_pp_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
    struct ml_pp_output_params *requested_params;
	struct ml_pp_output_params_stored *stored_requested_params;

    int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("write to %s device %d, count=%zu\n", mlpp_device_names[device_index], device_index, count);

    if (count == sizeof(struct ml_pp_output_params)) {
        DEBUG_PRINT("Requesting ml pre-processing param change.. \n");

        requested_params = kmalloc(sizeof(struct ml_pp_output_params), GFP_KERNEL);
		if (!requested_params) {
			pr_err("Failed to allocate memory for requested_params\n");
			return -ENOMEM;
		}
		stored_requested_params = kmalloc(sizeof(struct ml_pp_output_params_stored), GFP_KERNEL);
		if (!stored_requested_params) {
			pr_err("Failed to allocate memory for stored_requested_params\n");
			kfree(requested_params);
			return -ENOMEM;
		}

        if (copy_from_user(requested_params, buf, sizeof(*requested_params))) { // TODO adjust offset
			kfree(requested_params);
			kfree(stored_requested_params);
			return -EFAULT;
		}
		
        DEBUG_PRINT(" csc_out(%u), color_in(%u), output %u x %u  (%u), crop %u %u %u %u, sub rgb = %u %u %u\n", 
			requested_params->color_from_csc, requested_params->color_out,
			requested_params->width_out,  requested_params->height_out, requested_params->color_out,
			requested_params->crop_xstart,  requested_params->crop_xstop, requested_params->crop_ystart, requested_params->crop_ystop,
			requested_params->sub_gy, requested_params->sub_bcb, requested_params->sub_rcr);
	
	
		// Check if the device has valid parameters before modifying
		if (mlpp_params_lookup[device_index] == NULL) {
			pr_err("Device %d has no output parameters (input device)\n", device_index);
			kfree(requested_params);
			kfree(stored_requested_params);
			return -EINVAL;
		}


		// The mlpp-v2 hardware derives the scaler step directly from the
		// crop/output geometry at frame-program time, so no scaler-coefficient
		// precomputation is needed here.

		// Copy settings over to stored format before saving
		// This is to reduce the calculations needed in userspace
		stored_requested_params->width_out = requested_params->width_out;
		stored_requested_params->height_out = requested_params->height_out;
		stored_requested_params->crop_xstart = requested_params->crop_xstart;
		stored_requested_params->crop_xstop = requested_params->crop_xstop;
		stored_requested_params->crop_ystart = requested_params->crop_ystart;
		stored_requested_params->crop_ystop = requested_params->crop_ystop;
		stored_requested_params->color_from_csc = requested_params->color_from_csc;
		stored_requested_params->color_out = requested_params->color_out;
		if ( (requested_params->sub_gy == 255) && (requested_params->sub_bcb == 255) && (requested_params->sub_rcr == 255 ) )
		{
			DEBUG_PRINT("MeanSumSub Bypass requested.. \n");
			stored_requested_params->subtract_value = 0x80000000;
		}
		else // do mean-sum-sub
		{
			// Subtract control // 31-17 Red , 16-8 Blue , 7-0 Green
			stored_requested_params->subtract_value = (requested_params->sub_gy) + (requested_params->sub_bcb << 8) + (requested_params->sub_rcr << 16);
		}
		if(requested_params->scale_y == 0 && requested_params->scale_cb == 0 && requested_params->scale_cr == 0 ) {
			DEBUG_PRINT("Scale Bypass requested.. \n");
			stored_requested_params->scale_value = 0x80000000;
		}
		else{
			//CH1 [2:0] 	- shift_gy	Amount to shift green/luma channel
			//CH1 [7] 		- shift_left_nright_gy	Direction of shift for green/luma channel (0: right 1: left)
			//CH1 [10:8] 	- shift_bcb	Amount to shift blue/blue-difference channel
			//CH1 [15] 		- shift_left_nright_bcb	Direction of shift for blue/blue-difference channel (0: right 1: left)
			//CH1 [18:16] 	- shift_rcr	Amount to shift red/red-difference channel
			//CH1 [23] 		- shift_left_nright_rcr	Direction of shift for red/red-difference channel (0: right 1: left)
			//CH1 [31] 		- bypass_scale	Bypass pixel color scaling (0: do not bypass 1: bypass)
			u32 scale_output = 0;
			DEBUG_PRINT(" [%d] scaled value = %d\n",device_index, requested_params->scale_y);

			if (requested_params->scale_y >= 0 )
			{
				uint8_t power_of_2 = get_nearest_power_of_2(requested_params->scale_y);
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 1;
				scale_output = scale_output + (direction << 7) + (shift_amount);
			}
			else{
				uint8_t power_of_2 = get_nearest_power_of_2( -requested_params->scale_y );
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 0;
				scale_output = scale_output + (direction << 7) + (shift_amount);

			}
			if (requested_params->scale_cb >= 0 )
			{
				uint8_t power_of_2 = get_nearest_power_of_2(requested_params->scale_cb);
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 1;
				scale_output = scale_output + (direction << 15) + (shift_amount << 8);
			}
			else{
				uint8_t power_of_2 = get_nearest_power_of_2( -requested_params->scale_cb);
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 0;
				scale_output = scale_output + (direction << 15) + (shift_amount << 8);

			}
			if (requested_params->scale_cr >= 0 )
			{
				uint8_t power_of_2 = get_nearest_power_of_2(requested_params->scale_cr);
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 1;
				scale_output = scale_output + (direction << 23) + (shift_amount << 16);
			}
			else{
				uint8_t power_of_2 = get_nearest_power_of_2( -requested_params->scale_cr);
				uint8_t shift_amount = get_power_of_2_exponent(power_of_2);
				uint8_t direction = 0;
				scale_output = scale_output + (direction << 23) + (shift_amount << 16);
			}
			stored_requested_params->scale_value = scale_output;
			DEBUG_PRINT(" [%d] scaled output = 0x%08X\n",device_index, scale_output);

		}
		
		if (requested_params->width_out == 0 || requested_params->height_out == 0 ||
		    (size_t)requested_params->width_out * requested_params->height_out * 4 >
		    MLPP_MAX_OUT_BYTES) {
			pr_err("device %d: invalid output geometry %ux%u\n", device_index,
			       requested_params->width_out, requested_params->height_out);
			kfree(requested_params);
			kfree(stored_requested_params);
			return -EINVAL;
		}


		int cma_memory_needed = requested_params->width_out * requested_params->height_out * 4;
		bool need_realloc = ((size_t)cma_memory_needed > mlpp_slot_payload(device_index));

		DEBUG_PRINT("Memory needed for device %d is %d, pre-allocated %d \n", device_index, cma_memory_needed, global_priv->cma_alloc_size[device_index] );

		if (need_realloc)
		{
			int nbuf = mlpp_dev_nbuf(device_index);
			int new_alloc = ONE_MB_BUFFER_SIZE + cma_memory_needed;
			size_t new_size = (size_t)new_alloc + MLPP_SLOT_SLACK;
			size_t old_size = (size_t)global_priv->cma_alloc_size[device_index] + MLPP_SLOT_SLACK;
			int s;

			pr_info("device %d reallocating more memory for %d output ring buffers\n",
				device_index, nbuf);

			/* The engine may already have this device's old slot addresses
			 * programmed into MLPP_ROI_OBASE_*. Take the engine's op_lock and
			 * let any in-flight frame land, so no frame can be programmed
			 * against the memory we are about to free. Held until the new
			 * slots are in place -- every exit below must unlock. */
			mlpp_output_engine_lock(device_index);

			down_write(&global_priv->buf_sem[device_index]);

			/* Refuse the resize while userspace still holds an exported DMABUF
			 * over one of these slots: dma_free_coherent() would free memory
			 * that is still mapped into a process, and those pages would then
			 * be handed to an unrelated allocation -- a userspace-visible
			 * use-after-free.
			 *
			 * Checked under both locks, and ml_pp_export_dmabuf claims under
			 * buf_sem, so no new export can slip in between this test and the
			 * frees below. The new geometry is not published until the very
			 * end, so bailing here leaves the hardware programmed for the old,
			 * still-valid buffer size. */
			if (mlpp_dev_has_pinned_slots(device_index)) {
				pr_err("device %d: cannot resize output buffers while exported DMABUFs are outstanding\n",
				       device_index);
				up_write(&global_priv->buf_sem[device_index]);
				mlpp_output_engine_unlock(device_index);
				kfree(requested_params);
				kfree(stored_requested_params);
				return -EBUSY;
			}

			// Free every existing ring slot, then reallocate it at the new size.
			for (s = 0; s < nbuf; s++) {
				if (global_priv->cpu_addr_fb_orig[device_index][s] && global_priv->dev) {
					dma_free_coherent(global_priv->dev, old_size,
						global_priv->cpu_addr_fb_orig[device_index][s],
						global_priv->dma_handle_fb_orig[device_index][s]);
					pr_info("Framebuffer slot %d freed successfully\n", s);
				} else {
					pr_err("Cannot free Framebuffer slot %d: cpu_addr_fb_orig=%px, dev=%px\n",
						s, global_priv->cpu_addr_fb_orig[device_index][s], global_priv->dev);
				}
				/* Drop the stale pointers immediately. Leaving them set would
				 * let ml_pp_remove() free them a second time if the
				 * reallocation below fails. */
				global_priv->cpu_addr_fb_orig[device_index][s] = NULL;
				global_priv->cpu_addr_fb[device_index][s] = NULL;
				global_priv->dma_handle_fb_orig[device_index][s] = 0;
				global_priv->dma_handle_fb[device_index][s] = 0;
				global_priv->buffer_written[device_index][s] = false;
			}

			/* The old buffers are gone; commit the new size now so any
			 * subsequent free uses the right length. */
			global_priv->cma_alloc_size[device_index] = new_alloc;

			for (s = 0; s < nbuf; s++) {
				uintptr_t aligned_cpu_addr, offset;

				DEBUG_PRINT("Allocating framebuffer slot %d (%s): dev=%px, size=%zu\n",
					s, mlpp_device_names[device_index], global_priv->dev, new_size);
				global_priv->cpu_addr_fb_orig[device_index][s] = dma_alloc_coherent(
					global_priv->dev, new_size,
					&global_priv->dma_handle_fb_orig[device_index][s], GFP_KERNEL);
				if (!global_priv->cpu_addr_fb_orig[device_index][s]) {
					pr_err("dma_alloc_coherent failed for output slot %d reallocation!\n", s);
					// Roll back the slots already reallocated in this loop.
					while (--s >= 0) {
						dma_free_coherent(global_priv->dev, new_size,
							global_priv->cpu_addr_fb_orig[device_index][s],
							global_priv->dma_handle_fb_orig[device_index][s]);
						global_priv->cpu_addr_fb_orig[device_index][s] = NULL;
						global_priv->cpu_addr_fb[device_index][s] = NULL;
						global_priv->dma_handle_fb_orig[device_index][s] = 0;
						global_priv->dma_handle_fb[device_index][s] = 0;
					}
					/* No usable buffers for this device any more. Mark it so
					 * and leave the OLD geometry in place -- the new params
					 * were never published. */
					global_priv->cma_alloc_size[device_index] = 0;
					global_priv->wr_idx[device_index] = 0;
					global_priv->rd_idx[device_index] = 0;
					up_write(&global_priv->buf_sem[device_index]);
					mlpp_output_engine_unlock(device_index);
					kfree(requested_params);
					kfree(stored_requested_params);
					return -ENOMEM;
				}

				// Ensure 64-byte alignment for this slot
				aligned_cpu_addr = (uintptr_t)global_priv->cpu_addr_fb_orig[device_index][s];
				offset = (64 - (aligned_cpu_addr & 63)) & 63;
				global_priv->cpu_addr_fb[device_index][s] = (void *)(aligned_cpu_addr + offset);
				global_priv->dma_handle_fb[device_index][s] =
					global_priv->dma_handle_fb_orig[device_index][s] + offset;

				// New buffer starts empty in the ring
				global_priv->buffer_written[device_index][s] = false;
			}

			// Reset the ring indices for the freshly reallocated buffers
			global_priv->wr_idx[device_index] = 0;
			global_priv->rd_idx[device_index] = 0;

			pr_info("Reallocated %d-deep output ring: dev=%px, size=%d, slot0 cpu=%px dma=%pad (aligned)\n",
				nbuf, global_priv->dev, global_priv->cma_alloc_size[device_index],
				global_priv->cpu_addr_fb[device_index][0],
				&global_priv->dma_handle_fb[device_index][0]);

			up_write(&global_priv->buf_sem[device_index]);
			mlpp_output_engine_unlock(device_index);
		}

		/* Publish the new geometry only now that the backing buffers are
		 * guaranteed to be large enough for it. Every failure path above
		 * returns with the previous parameters still in force. */
		memcpy(mlpp_params_lookup[device_index], stored_requested_params,
		       sizeof(*stored_requested_params));
		DEBUG_PRINT("Changed ml pre-processing param change.. \n");

		kfree(requested_params);
		kfree(stored_requested_params);
    }
	else
	{
		DEBUG_PRINT("Unknown request, doing nothing\n");
	}

    return count;
}

ssize_t ml_pp_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
   	int device_index = (int)(long)file->private_data;

	DEBUG_PRINT("read from %s device %d, count=%zu \n", mlpp_device_names[device_index] , device_index, count);
	DEBUG_PRINT("Out %d x %d, Crop %d,%d to %d,%d, MeanSub %ul, scale %ul, Color In %d, Color Out %d \n", mlpp_params_lookup[device_index]->width_out, 	mlpp_params_lookup[device_index]->height_out,
        mlpp_params_lookup[device_index]->crop_xstart, mlpp_params_lookup[device_index]->crop_ystart, mlpp_params_lookup[device_index]->crop_xstop, mlpp_params_lookup[device_index]->crop_ystop,
        mlpp_params_lookup[device_index]->subtract_value, mlpp_params_lookup[device_index]->scale_value,
        mlpp_params_lookup[device_index]->color_from_csc, mlpp_params_lookup[device_index]->color_out);

	// Special case: if count == sizeof(int), user wants DMABUF fd instead of data copy
	if (count == sizeof(int)) {
		DEBUG_PRINT("sending dmabuf\n");
		return ml_pp_export_dmabuf(device_index, buf);
	}

	// Calculate output image size
	int image_out_size = 0;
	if (mlpp_params_lookup[device_index]->color_out == MLPP_COLOR_OUT_NV12 ) // NV12: 1.5 bytes
		image_out_size = mlpp_params_lookup[device_index]->width_out * mlpp_params_lookup[device_index]->height_out * 3 / 2;
	else if (mlpp_params_lookup[device_index]->color_out < 2 ) // 3 bytes
		image_out_size = mlpp_params_lookup[device_index]->width_out * mlpp_params_lookup[device_index]->height_out * 3;
	else if (mlpp_params_lookup[device_index]->color_out == 3 )
		image_out_size = mlpp_params_lookup[device_index]->width_out * mlpp_params_lookup[device_index]->height_out; // Y Only
	else // 4 bytes
		image_out_size = mlpp_params_lookup[device_index]->width_out * mlpp_params_lookup[device_index]->height_out * 4; // RGBX 

    DEBUG_PRINT("Read called with count %zu, outbufcount %zu, imagesize %u\n", count, output_byte_count[device_index], image_out_size);

	/* Never read past the usable payload of a slot, whatever the configured
	 * geometry claims. */
	if (image_out_size <= 0 ||
	    (size_t)image_out_size > mlpp_slot_payload(device_index)) {
		pr_err("Output size %d invalid for device %d (usable %zu)\n",
		       image_out_size, device_index, mlpp_slot_payload(device_index));
		return -EINVAL;
	}

	// Wait for the current ring slot (rd_idx) to be filled by the hardware/ISR.
	{
		int const wait_ret = mlpp_output_wait_ready(device_index);

		if (wait_ret) {
			output_byte_count[device_index] = 0;
			reinit_completion(&global_priv->read_complete[device_index]);
			global_priv->read_ready[device_index] = false;

			if (wait_ret == -ERESTARTSYS)
				return -ERESTARTSYS;

			pr_warn_ratelimited("Timeout waiting for output ring slot for device %d\n",
					    device_index);
			return -EAGAIN;
		}
	}

	down_read(&global_priv->buf_sem[device_index]);

	int read_slot = global_priv->rd_idx[device_index];
	void *current_read_buffer = global_priv->cpu_addr_fb[device_index][read_slot];

	if (!current_read_buffer) {
		up_read(&global_priv->buf_sem[device_index]);
		pr_err("DMA buffer not allocated for device %d\n", device_index);
		return -ENOMEM;
	}
	DEBUG_PRINT("mlpp_outputs Device %d: reading from ring slot %d, cpu_addr = %px, offset = %zu, total_read = %zu\n",
	       device_index, read_slot, current_read_buffer,
	       output_byte_count[device_index], output_byte_count[device_index] + count);

	if (output_byte_count[device_index] > (size_t)image_out_size)
		output_byte_count[device_index] = 0;

	if (count > image_out_size)
		count = image_out_size;

	if (output_byte_count[device_index] + count > image_out_size)
		count = image_out_size - output_byte_count[device_index];

	if ( output_byte_count[device_index] <= image_out_size )
	{
		if (copy_to_user(buf, current_read_buffer + output_byte_count[device_index],
				 count)) {
			up_read(&global_priv->buf_sem[device_index]);
			return -EFAULT;
		}
		wmb();
		output_byte_count[device_index] += count;
		DEBUG_PRINT("Read %zu bytes of  %zu bytes from FB buffer\n", count, output_byte_count[device_index]);

		DEBUG_PRINT("Output buffer %d read - wr_idx: %d, rd_idx: %d \n",
		           device_index, global_priv->wr_idx[device_index], global_priv->rd_idx[device_index]);

		if (output_byte_count[device_index] == image_out_size)
		{
			output_byte_count[device_index] = 0;
			DEBUG_PRINT("Image read done for Device Index: %d \n", device_index);
			PERF_PRINT("PIPE OUT-MEM, READ %d, NS=%lld\n", device_index, ktime_get_real_ns());

			// Frame fully drained: free this ring slot and advance rd_idx,
			// waking any producer blocked on a full ring.
			mlpp_output_consumed(device_index);
			reinit_completion(&global_priv->read_complete[device_index]);
			global_priv->read_ready[device_index] = false;
		}
	}

	up_read(&global_priv->buf_sem[device_index]);

	return count;
}

int ml_pp_release(struct inode *inode, struct file *file)
{
	int device_index = (int)(long)file->private_data;
	DEBUG_PRINT("releasing device %d\n", device_index);

	return 0;
}

int ml_pp_open(struct inode *inode, struct file *file)
{
	int device_index = iminor(inode);  // Get the minor number (0-19)
	DEBUG_PRINT("open %s \n", mlpp_device_names[device_index]);

	// Store device index in file private data for use in other operations
	file->private_data = (void *)(long)device_index;


	return 0;
}

