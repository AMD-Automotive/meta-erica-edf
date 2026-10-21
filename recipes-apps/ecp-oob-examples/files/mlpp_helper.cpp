// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * mlpp_helper.cpp
 * Helper functions for Machine Learning Pre-Processing (MLPP) hardware interface operations.
 * Author: LT Thomas
 */

#include "mlpp_helper.h"

static int framecount=0;

// Constructor
MlppHelper::MlppHelper() : mlpre_fd(-1) {
    // Initialize file descriptor to invalid state
}

MlppHelper::MlppHelper(const char* device_path) : mlpre_fd(-1) {
    openDevice(device_path);
    dmabuf_fd = -1;
}

// Destructor
MlppHelper::~MlppHelper() {
    // Ensure device is properly closed when object is destroyed
    closeDevice();
}

bool MlppHelper::openDevice(const char* device_path) {
    // Close existing device if open
    if (mlpre_fd != -1) {
        close(mlpre_fd);
        mlpre_fd = -1;
    }
    
    // Open the ML preprocessing device
    //mlpre_fd = open(device_path, O_RDWR | O_NOCTTY);
	mlpre_fd = open(device_path, O_RDWR | O_NOCTTY | O_SYNC); // O_SYNC ensures writes are immediately flushed to the device
    if (mlpre_fd == -1) {
        perror("Error opening ML preprocessing device");
        std::cerr << "Failed to open device: " << device_path << std::endl;
        return false;
    }
    
    std::cout << "Successfully opened ML preprocessing device: " << device_path 
              << " (fd: " << mlpre_fd << ")" << std::endl;
    return true;
}

void MlppHelper::closeDevice() {
    if (mlpre_fd != -1) {
        close(mlpre_fd);
        mlpre_fd = -1;
        std::cout << "ML preprocessing device closed." << std::endl;
    }
}

bool MlppHelper::isDeviceOpen() const {
    return (mlpre_fd != -1);
}

//TODO remove hard coding
void MlppHelper::write_dmabuf_to_mlpp(int dmabuf_fd, int size) {
    
    ml_pp_dmabuf_params dmabuf_params;
    dmabuf_params.dmabuf_fd = dmabuf_fd;
    dmabuf_params.offset = 0;  // Usually 0 for full frame
    dmabuf_params.size = size;
    dmabuf_params.frame_sequence = framecount++;
    
    // Send DMABUF fd to ML driver for zero-copy processing
    int bytes_written = write(mlpre_fd, &dmabuf_params, sizeof(dmabuf_params));
    if (bytes_written != sizeof(dmabuf_params)) {
        std::cerr << "Failed to send DMABUF to ML driver: " << strerror(errno) << "Bytes read back: " << bytes_written << std::endl;
    }

}

void MlppHelper::write_image_to_mlpp(cv::Mat * tiled_output) {
    
    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }

	// Calculate the total number of bytes in the image
	size_t total_image_bytes = tiled_output->total() * tiled_output->elemSize(); //(width * height) * (3 for CV_8UC3)
	const size_t CHUNK_SIZE = 65536*16;
	size_t bytes_written_total = 0;
	const uchar* current_data_ptr = tiled_output->data; 

	//size_t written_bytes = write(mlpre_fd, &tiled_output.data, sizeof(tiled_output.data));

	while (bytes_written_total < total_image_bytes) {
		size_t bytes_remaining = total_image_bytes - bytes_written_total;
		size_t bytes_to_write_this_chunk = (bytes_remaining < CHUNK_SIZE) ? bytes_remaining : CHUNK_SIZE;

		// Call write() for the current chunk
		ssize_t written_this_call = write(mlpre_fd, current_data_ptr, bytes_to_write_this_chunk);

		if (written_this_call == -1) {
		    perror("Error writing chunk to device");
		    close(mlpre_fd);
		    return;
		}
		if (written_this_call == 0) {
		    std::cerr << "Warning: write() returned 0 bytes written. Device might be busy or closed." << std::endl;
		    break; // Exit loop if nothing is written
		}
		
		// Handle partial writes - ensure we wrote what we expected
		if (written_this_call < (ssize_t)bytes_to_write_this_chunk) {
			std::cerr << "Partial write issue." << std::endl;
		}

		// Update total bytes written and advance the pointer
		bytes_written_total += written_this_call;
		current_data_ptr += written_this_call;
		//std::cout << "  Wrote " << written_this_call << " bytes. Total: " << bytes_written_total << "/" << total_image_bytes << std::endl;
	}

	if (bytes_written_total == total_image_bytes) {
		// Force synchronization - ensure all data is written to the device
		//std::cout << "Successfully wrote all " << bytes_written_total << " bytes to device." << std::endl;
	} else {
		std::cerr << "Warning: Incomplete write. Expected " << total_image_bytes
		  << ", but only wrote " << bytes_written_total << " bytes." << std::endl;
	}

}

void MlppHelper::read_image_from_mlpp(cv::Mat * vpss_output )
{
    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }
    
    size_t total_bytes_expected = vpss_output->total() * vpss_output->elemSize();

    // Pointers and counters for reading in a loop
    uchar* current_buffer_ptr = vpss_output->data; // Pointer to the current read position in Mat's buffer
    size_t bytes_read_total = 0;                  // Total bytes read so far
    //std::cout << "Attempting to read " << total_bytes_expected << " bytes..." << std::endl;

    // 5. Loop to read all expected bytes
    while (bytes_read_total < total_bytes_expected) {
        size_t bytes_to_read_this_call = total_bytes_expected - bytes_read_total;

        // Call read()
        ssize_t bytes_read_this_call = read(mlpre_fd, current_buffer_ptr, bytes_to_read_this_call);

        if (bytes_read_this_call == -1) {
            perror("Error reading from file/device");
            close(mlpre_fd);
            return;
        }
        if (bytes_read_this_call == 0) {
            // End of file/stream reached prematurely (device might close or stop sending data)
            std::cerr << "Warning: End of file/device stream reached prematurely. Read "
                      << bytes_read_total << " of " << total_bytes_expected << " bytes." << std::endl;
            break; // Exit loop
        }

        // Update total bytes read and advance the buffer pointer
        bytes_read_total += bytes_read_this_call;
        current_buffer_ptr += bytes_read_this_call;
        //std::cout << "  Read " << bytes_read_this_call << " bytes. Total: " << bytes_read_total << "/" << total_bytes_expected << std::endl;
    }

    if (bytes_read_total == total_bytes_expected) {
        //std::cout << "Successfully read all " << bytes_read_total << " bytes into cv::Mat." << std::endl;
    } else {
        std::cerr << "Error: Incomplete read. Expected " << total_bytes_expected
                  << ", but only read " << bytes_read_total << " bytes." << std::endl;
    }
   
}

int MlppHelper::read_image_from_mlpp_dmabuf( void )
{
    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return -1;
    }
    // if there is a dmabuf alread used close it first
    if (dmabuf_fd > 0)
        close(dmabuf_fd);
    
    int mlpp_dmabuf_fd; // Changed to int instead of uchar*

    // Call read()
    ssize_t bytes_read_this_call = read(mlpre_fd, &mlpp_dmabuf_fd, sizeof(int));

    if (bytes_read_this_call == -1) {
        perror("Error reading from file/device");
        //close(mlpre_fd);
        return -1;
    }
    if (bytes_read_this_call == 0) {
        // End of file/stream reached prematurely (device might close or stop sending data)
        std::cerr << "Warning: End of file/device stream reached prematurely." << std::endl;
        return -1;
    }
    
    // Verify we read the expected number of bytes
    if (bytes_read_this_call != sizeof(int)) {
        std::cerr << "Warning: Expected to read " << sizeof(int) << " bytes, but read " 
                  << bytes_read_this_call << " bytes." << std::endl;
        return -1;
    }
    // Save the last used dmabuf_fd
    dmabuf_fd = mlpp_dmabuf_fd;

    return mlpp_dmabuf_fd;
   
}


void MlppHelper::update_mlpp_params( struct ml_pp_input_params params)
{
    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }

	ssize_t bytes_written = write(mlpre_fd, &params, sizeof(params));
	if (bytes_written == (ssize_t)sizeof(params)) {
		//std::cout << "Successfully wrote " << bytes_written << " bytes (ml_pp_input_params) to device." << std::endl;
	} else {
		perror("Error writing ml_pp_input_params to device");
		std::cerr << "Expected " << sizeof(params) << " bytes, but wrote " << bytes_written << " bytes." << std::endl;
	}
}

void MlppHelper::update_mlpp_params( struct ml_pp_output_params params)
{
    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }

	ssize_t bytes_written = write(mlpre_fd, &params, sizeof(params));
	if (bytes_written == (ssize_t)sizeof(params)) {
		//std::cout << "Successfully wrote " << bytes_written << " bytes (ml_pp_input_params) to device." << std::endl;
	} else {
		perror("Error writing ml_pp_input_params to device");
		std::cerr << "Expected " << sizeof(params) << " bytes, but wrote " << bytes_written << " bytes." << std::endl;
	}
}


void MlppHelper::update_mlpp_output_params( u16 widthout, u16 heightout, 
        u16 crop_x_start, u16 crop_x_stop, u16 crop_y_start, u16 crop_y_stop, 
        u8 mean_y, u8 mean_u, u8 mean_v, float scale, u16 csc_out, u16 color_out )
{
    ml_pp_output_params mlpp_out_params;
    memset(&mlpp_out_params, 0, sizeof(mlpp_out_params));
    mlpp_out_params.width_out   = widthout;
    mlpp_out_params.height_out  = heightout;
    mlpp_out_params.crop_xstart = crop_x_start;
    mlpp_out_params.crop_xstop  = crop_x_stop;
    mlpp_out_params.crop_ystart = crop_y_start;
    mlpp_out_params.crop_ystop  = crop_y_stop;
    mlpp_out_params.sub_gy      = mean_y; 
    mlpp_out_params.sub_bcb     = mean_u;
    mlpp_out_params.sub_rcr     = mean_v;
    mlpp_out_params.color_from_csc = csc_out;   // 0 - RGB , 1 - YUV444M, 2 - BGR
    mlpp_out_params.color_out   = color_out;    // 0 - RGB/YUV444P , 1 - YUV444M, 2 - RGBX/YUV444(A), 3 - Y Only (Grey), 4 - NV12
    if (scale == 0)
    {
        mlpp_out_params.scale_y     = 0;
        mlpp_out_params.scale_cb    = 0;
        mlpp_out_params.scale_cr    = 0;
    }
    else if ( scale > 1)
    {

        mlpp_out_params.scale_y     = (int)scale;
        mlpp_out_params.scale_cb    = (int)scale;
        mlpp_out_params.scale_cr    = (int)scale;       
    }
    else
    {
        int signed_scale = (int)( 1.0 / scale);
        mlpp_out_params.scale_y     = -signed_scale; // doing this because floating point logic in driver is bad
        mlpp_out_params.scale_cb    = -signed_scale;
        mlpp_out_params.scale_cr    = -signed_scale;
    }

    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }

	ssize_t bytes_written = write(mlpre_fd, &mlpp_out_params, sizeof(mlpp_out_params));
	if (bytes_written == (ssize_t)sizeof(mlpp_out_params)) {
		//std::cout << "Successfully wrote " << bytes_written << " bytes (ml_pp_input_params) to device." << std::endl;
	} else {
		perror("Error writing ml_pp_input_params to device");
		std::cerr << "Expected " << sizeof(mlpp_out_params) << " bytes, but wrote " << bytes_written << " bytes." << std::endl;
	}
}

void MlppHelper::update_mlpp_input_params( u16 widthin, u16 heightin )
{
    update_mlpp_input_params( widthin, heightin, 3 ); // 3 - NV12
}

void MlppHelper::update_mlpp_input_params( u16 widthin, u16 heightin, u16 fmt_in )
{
    ml_pp_input_params mlpp_in_params;
    memset(&mlpp_in_params, 0, sizeof(mlpp_in_params));
    mlpp_in_params.width_in   = widthin;
    mlpp_in_params.height_in  = heightin;
    mlpp_in_params.color_in   = fmt_in; // 0 - RGB/YUV444P , 1 - YUV444M, 2 - UYVY 4:2:2, 3 - NV12

    // Check if device is open
    if (!isDeviceOpen()) {
        std::cerr << "Error: ML preprocessing device is not open. Call openDevice() first." << std::endl;
        return;
    }

    ssize_t bytes_written = write(mlpre_fd, &mlpp_in_params, sizeof(mlpp_in_params));
    if (bytes_written == (ssize_t)sizeof(mlpp_in_params)) {
        //std::cout << "Successfully wrote " << bytes_written << " bytes (ml_pp_input_params) to device." << std::endl;
    } else {
        perror("Error writing ml_pp_input_params to device");
        std::cerr << "Expected " << sizeof(mlpp_in_params) << " bytes, but wrote " << bytes_written << " bytes." << std::endl;
    }
}