// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * vcu_helper.cpp
 * Video Codec Unit (VCU) helper functions for hardware video encoding and decoding.
 * Author: LT Thomas
 */

#include "vcu_helper.h"

// Callback for qtdemux dynamic pad connection
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstData *gst_data = (GstData*)data;
    GstPad *sink_pad = NULL;
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    // Validate input parameters
    if (!gst_data || !pad || !element) {
        g_print("Invalid parameters in on_pad_added callback\n");
        return;
    }

    g_print("Received new pad '%s' from '%s'\n", GST_PAD_NAME(pad), GST_ELEMENT_NAME(element));

    // Check the media type of the new pad
    new_pad_caps = gst_pad_get_current_caps(pad);
    if (!new_pad_caps) {
        g_print("Failed to get caps from pad\n");
        return;
    }

    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    if (!new_pad_struct) {
        g_print("Failed to get structure from caps\n");
        goto exit;
    }

    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (!new_pad_type) {
        g_print("Failed to get structure name\n");
        goto exit;
    }
    
    // We only want to link video/x-h264 pads
    if (!g_str_has_prefix(new_pad_type, "video/x-h264")) {
        g_print("It has type '%s' which is not H.264. Ignoring.\n", new_pad_type);
        goto exit;
    }

    // Validate parser element exists
    if (!gst_data->parser) {
        g_print("Parser element is NULL\n");
        goto exit;
    }

    // Get the sink pad from h264parse
    sink_pad = gst_element_get_static_pad(gst_data->parser, "sink");
    if (!sink_pad) {
        g_print("Failed to get sink pad from parser\n");
        goto exit;
    }
    
    // If our parser is already linked, we have nothing to do here
    if (gst_pad_is_linked(sink_pad)) {
        g_print("Parser sink pad is already linked. Ignoring.\n");
        goto exit;
    }

    // Attempt the link
    ret = gst_pad_link(pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("Type is '%s' but link failed with error: %d\n", new_pad_type, ret);
    } else {
        g_print("Link succeeded (type '%s').\n", new_pad_type);
        // Small delay to allow pipeline to stabilize after dynamic linking
        g_usleep(10000); // 10ms delay
        g_print("Pad linking stabilization complete.\n");
    }

exit:
    // Clean up references
    if (new_pad_caps)
        gst_caps_unref(new_pad_caps);
    
    if (sink_pad)
        gst_object_unref(sink_pad);
}


// GStreamer bus message handler
gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            //g_print("End of stream\n");
            break;
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            //g_printerr("Error: %s\n", error->message);
            g_error_free(error);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);
            // Safely check if data is GstData* and has valid pipeline
            if (data) {
                GstData* gst_data = (GstData*)data;
                // Verify this looks like a GstData structure (has valid pipeline pointer)
                if (gst_data->pipeline && GST_IS_PIPELINE(gst_data->pipeline) && 
                    GST_MESSAGE_SRC(msg) == GST_OBJECT(gst_data->pipeline)) {
                    g_print("Pipeline state changed from %s to %s\n",
                        gst_element_state_get_name(old_state),
                        gst_element_state_get_name(new_state));
                }
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}

// Allocation query callback for DMABUF support (from VCU2 example)
static GstPadProbeReturn
appsink_query_cb(GstPad * pad G_GNUC_UNUSED, GstPadProbeInfo * info,
    gpointer user_data G_GNUC_UNUSED)
{
    GstQuery *query = (GstQuery*)info->data;

    if (GST_QUERY_TYPE(query) != GST_QUERY_ALLOCATION)
        return GST_PAD_PROBE_OK;

    // Add GstVideoMeta support for hardware accelerated decoders
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    
    // Debug: Check what allocators are being requested
    guint n_allocators = gst_query_get_n_allocation_params(query);
    g_print("DMABUF Debug: Allocation query has %u allocators\n", n_allocators);
    
    // Add DMABUF allocator preference for zero-copy operation
    GstAllocator *dmabuf_allocator = gst_dmabuf_allocator_new();
    if (dmabuf_allocator) {
        GstAllocationParams params;
        gst_allocation_params_init(&params);
        gst_query_add_allocation_param(query, dmabuf_allocator, &params);
        gst_object_unref(dmabuf_allocator);
        g_print("DMABUF Debug: Added DMABUF allocator to allocation query\n");
    }
    
    g_print("DMABUF Debug: Added GstVideoMeta support for DMABUF allocation\n");

    return GST_PAD_PROBE_HANDLED;
}



// Constructor
VcuHelper::VcuHelper()  {
    // Initialize file descriptor to invalid state
    // Initialize GStreamer
    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }
}

// Destructor
VcuHelper::~VcuHelper() {
    // Ensure device is properly closed when object is destroyed
    //closeDevice();
}

int VcuHelper::check_eof(GstData* gst_data) {
    // Process GStreamer bus messages with longer timeout for multi-stream setup
    GstMessage *msg = gst_bus_timed_pop_filtered(gst_data->bus, 5000 * GST_USECOND,  // 5ms timeout
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_STREAM_START));
    
    if (msg != NULL) {
        GstMessageType msg_type = GST_MESSAGE_TYPE(msg);
        
        // Check for EOS to stop the pipeline
        if (msg_type == GST_MESSAGE_EOS) {
            const gchar* src_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(msg));
            cout << "End of stream reached from " << src_name << ", stopping pipeline\n" << endl;
            gst_message_unref(msg);
            return 1; // Indicate end of stream
        }
        
        // Check for ERROR messages that should also stop the pipeline
        if (msg_type == GST_MESSAGE_ERROR) {
            GError *error;
            gchar *debug_info;
            const gchar* src_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(msg));
            gst_message_parse_error(msg, &error, &debug_info);
            g_print("Pipeline error from %s: %s\n", src_name, error->message);
            g_print("Debug info: %s\n", debug_info ? debug_info : "none");
            g_error_free(error);
            g_free(debug_info);
            gst_message_unref(msg);
            return 1; // Indicate error condition
        }
        
        // Monitor state changes for EOF detection
        if (msg_type == GST_MESSAGE_STATE_CHANGED) {
            GstState old_state, new_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);
            
            // Check if any source element has gone to NULL state unexpectedly
            if (new_state == GST_STATE_NULL && old_state == GST_STATE_PLAYING) {
                const gchar* src_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(msg));
                if (g_str_has_prefix(src_name, "source")) {
                    g_print("Source %s transitioned to NULL - possible EOF\n", src_name);
                    gst_message_unref(msg);
                    return 1;
                }
            }
        }
        
        // Process other messages normally
        bus_call(gst_data->bus, msg, gst_data);
        gst_message_unref(msg);
    }
    
    // Additional check: verify pipeline state for EOF detection
    if (gst_data->pipeline) {
        GstState current_state, pending_state;
        GstStateChangeReturn state_ret = gst_element_get_state(gst_data->pipeline, &current_state, &pending_state, 0);
        
        // If pipeline has stopped unexpectedly, consider it EOF
        if (current_state == GST_STATE_NULL || 
            (state_ret == GST_STATE_CHANGE_FAILURE && current_state != GST_STATE_PLAYING)) {
            g_print("Pipeline state indicates EOF - current state: %s\n", 
                   gst_element_state_get_name(current_state));
            return 1;
        }
    }
    
    return 0;
}

void VcuHelper::cleanup_pipeline(GstData* gst_data) {
    g_print("Starting pipeline cleanup...\n");

    // Add this: Remove bus watch to prevent callbacks during cleanup
    if (gst_data->bus) {
        gst_bus_remove_watch(gst_data->bus);
    }
    
    // Send EOS to flush remaining buffers
    if (gst_data->pipeline) {
        gst_element_send_event(gst_data->pipeline, gst_event_new_eos());
        
        // Wait for EOS message with timeout
        GstMessage *msg = gst_bus_timed_pop_filtered(gst_data->bus, 2 * GST_SECOND, GST_MESSAGE_EOS);
        if (msg) {
            g_print("EOS received during cleanup\n");
            gst_message_unref(msg);
        } else {
            g_print("EOS timeout during cleanup\n");
        }
    }
    
    // Stop pipeline
    if (gst_data->pipeline) {
        GstStateChangeReturn ret = gst_element_set_state(gst_data->pipeline, GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_print("Failed to stop pipeline gracefully\n");
        } else {
            // Wait for state change to complete
            gst_element_get_state(gst_data->pipeline, NULL, NULL, 2 * GST_SECOND);
            g_print("Pipeline stopped successfully\n");
        }
    }
    
    // Cleanup resources
    if (gst_data->bus) {
        gst_object_unref(gst_data->bus);
        gst_data->bus = NULL;
    }
    
    if (gst_data->pipeline) {
        gst_object_unref(gst_data->pipeline);
        gst_data->pipeline = NULL;
    }
    
    if (gst_data->allocator) {
        gst_object_unref(gst_data->allocator);
        gst_data->allocator = NULL;
        g_print("Allocator cleaned up\n");
    }
    
    g_print("Pipeline cleanup completed\n");
}

void VcuHelper::setup_gst_pipeline(const std::string filename, 
                                   GstFlowReturn (*new_sample_callback)(GstAppSink *appsink, gpointer user_data), 
                                   GstData* gst_data, float desired_video_rate) {

    gst_data->source = gst_element_factory_make("filesrc", "source");
    gst_data->demux = gst_element_factory_make("qtdemux", "demux");
    gst_data->parser = gst_element_factory_make("h264parse", "parser");
    gst_data->decoder = gst_element_factory_make("omxh264dec", "decoder");
    gst_data->videorate = gst_element_factory_make("videorate", "videorate");
    gst_data->capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    gst_data->appsink = gst_element_factory_make("appsink", "appsink");
    gst_data->pipeline = gst_pipeline_new("video-pipeline");
    
    // Check which elements failed to create
    if (!gst_data->pipeline) g_printerr("Failed to create pipeline\n");
    if (!gst_data->source) g_printerr("Failed to create filesrc\n");
    if (!gst_data->demux) g_printerr("Failed to create qtdemux\n");
    if (!gst_data->parser) g_printerr("Failed to create h264parse\n");
    if (!gst_data->decoder) g_printerr("Failed to create omxh264dec\n");
    if (!gst_data->videorate) g_printerr("Failed to create videorate\n");
    if (!gst_data->capsfilter) g_printerr("Failed to create capsfilter\n");
    if (!gst_data->appsink) g_printerr("Failed to create appsink\n");
    
    if (!gst_data->pipeline || !gst_data->source || !gst_data->demux || !gst_data->parser ||
        !gst_data->videorate || !gst_data->decoder ||  !gst_data->capsfilter || !gst_data->appsink) {
        std::cerr << "Failed to create GStreamer elements" << std::endl;
        return;
    }
    
    // Setup zero-copy DMABUF allocator
    gst_data->allocator = gst_dmabuf_allocator_new();
    if (!gst_data->allocator) {
        g_printerr("Failed to create DMABUF allocator, trying FD allocator\n");
        gst_data->allocator = gst_fd_allocator_new();
    }
    if (!gst_data->allocator) {
        g_printerr("Failed to create any allocator, using default\n");
        gst_data->allocator = gst_allocator_find(NULL);
    }
    
    if (gst_data->allocator) {
        g_print("Created allocator: %s\n", G_OBJECT_TYPE_NAME(gst_data->allocator));
    }
    
    // Set source location
    g_object_set(gst_data->source, "location", filename.c_str(), NULL);
    
    g_object_set(gst_data->decoder, "low-latency", 0, NULL);
    //g_object_set(gst_data->decoder, "split-input", TRUE, NULL);
    
    // Configure videorate for desired FPS with proper rate control
    g_object_set(gst_data->videorate, 
                 "max-rate", (int)desired_video_rate,  // Maximum output rate
                 "drop-only", TRUE,  // Only drop frames, no duplication to prevent bursts
                 "silent", FALSE,     // Enable logging for debugging
                 "average-period", GST_SECOND / 4,  // Shorter averaging period for tighter control
                 NULL);
    
    // Configure appsink for playback with DMABUF allocation
    g_object_set(gst_data->appsink, 
                 "emit-signals", TRUE,
                 "sync", TRUE,  // Disable synchronization to prevent catch-up
                 "max-buffers", 1,  // Reduced for tighter control
                 "drop", TRUE,  // Enable dropping to prevent catch-up bursts
                 "enable-last-sample", FALSE,
                 NULL);
    
    // Try to force DMABUF allocation on appsink
    if (gst_data->allocator && GST_IS_DMABUF_ALLOCATOR(gst_data->allocator)) {
        g_object_set(gst_data->appsink, "allocator", gst_data->allocator, NULL);
        g_print("Set DMABUF allocator on appsink\n");
    }
    
    // Also try to set allocator on decoder for hardware zero-copy
    if (gst_data->allocator && GST_IS_DMABUF_ALLOCATOR(gst_data->allocator)) {
        g_object_set(gst_data->decoder, "allocator", gst_data->allocator, NULL);
        g_print("Set DMABUF allocator on decoder\n");
    }
    
    // Set up caps with desired FPS and NV12 format
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                       "format", G_TYPE_STRING, "NV12",
                                       "framerate", GST_TYPE_FRACTION, (int)desired_video_rate, 1,
                                       NULL);
    g_object_set(gst_data->capsfilter, "caps", caps, NULL);
    
    // Debug: Print the caps we're setting
    gchar *caps_str = gst_caps_to_string(caps);
    g_print("Pipeline Setup - Setting capsfilter caps: %s\n", caps_str);
    g_free(caps_str);
    
    gst_caps_unref(caps);
    
    // Connect to the new-sample signal
    g_signal_connect(gst_data->appsink, "new-sample", G_CALLBACK(new_sample_callback), gst_data);
    
    // Add allocation query pad probe for DMABUF support
    GstPad *appsink_pad = gst_element_get_static_pad(gst_data->appsink, "sink");
    gst_pad_add_probe(appsink_pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, 
                      appsink_query_cb, NULL, NULL);
    gst_object_unref(appsink_pad);
    
    
#if 0   // Add debugging probe to check decoder output format using C-style callback
    GstPad *decoder_src_pad = gst_element_get_static_pad(gst_data->decoder, "src");
    if (decoder_src_pad) {
        gst_pad_add_probe(decoder_src_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                         [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                             if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_CAPS) {
                                 GstCaps *caps;
                                 gst_event_parse_caps(GST_PAD_PROBE_INFO_EVENT(info), &caps);
                                 gchar *caps_str = gst_caps_to_string(caps);
                                 g_print("Decoder Output Format: %s\n", caps_str);
                                 
                                 // Additional debugging for NV12 format verification
                                 GstVideoInfo video_info;
                                 if (gst_video_info_from_caps(&video_info, caps)) {
                                     g_print("Decoder Format Details:\n");
                                     g_print("  Format: %s\n", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&video_info)));
                                     g_print("  Width: %d, Height: %d\n", GST_VIDEO_INFO_WIDTH(&video_info), GST_VIDEO_INFO_HEIGHT(&video_info));
                                     g_print("  Stride[0]: %d, Stride[1]: %d\n", GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0), GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 1));
                                     g_print("  Offset[0]: %zu, Offset[1]: %zu\n", GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 0), GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 1));
                                     g_print("  Size: %zu bytes\n", GST_VIDEO_INFO_SIZE(&video_info));
                                 }
                                 
                                 g_free(caps_str);
                             }
                             return GST_PAD_PROBE_OK;
                         }, NULL, NULL);
        gst_object_unref(decoder_src_pad);
    }
#endif  
    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(gst_data->pipeline), 
                     gst_data->source, gst_data->demux, gst_data->parser, 
                     gst_data->decoder, gst_data->videorate, gst_data->capsfilter, gst_data->appsink, NULL);
    
    // Connect pad-added signal for qtdemux dynamic pad handling
    g_signal_connect(gst_data->demux, "pad-added", G_CALLBACK(on_pad_added), gst_data);
    
    // Link elements step by step with error checking
    g_print("Linking source to demux...\n");
    if (!gst_element_link(gst_data->source, gst_data->demux)) {
        std::cerr << "Failed to link source to demux" << std::endl;
        return;
    }
    
    // Note: qtdemux will connect to parser via pad-added callback when ready
    // Link parser to decoder
    g_print("Linking parser to decoder...\n");
    if (!gst_element_link(gst_data->parser, gst_data->decoder)) {
        std::cerr << "Failed to link parser to decoder" << std::endl;
        return;
    }
    
    g_print("Linking decoder to videorate...\n");
    if (!gst_element_link(gst_data->decoder, gst_data->videorate)) {
        std::cerr << "Failed to link decoder to videorate" << std::endl;
        return;
    }
    
    g_print("Linking videorate to capsfilter...\n");
    if (!gst_element_link(gst_data->videorate, gst_data->capsfilter)) {
        std::cerr << "Failed to link videorate to capsfilter" << std::endl;
        return;
    }
        
    g_print("Linking capsfilter to appsink...\n");
    if (!gst_element_link(gst_data->capsfilter, gst_data->appsink)) {
        std::cerr << "Failed to link capsfilter to appsink" << std::endl;
        return;
    }
    
    g_print("All links successful\n");
    
    // Setup bus message handling
    gst_data->bus = gst_pipeline_get_bus(GST_PIPELINE(gst_data->pipeline));
    gst_bus_add_watch(gst_data->bus, bus_call, gst_data);
    
    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(gst_data->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        
        // Get more detailed error information
        GstMessage *msg = gst_bus_timed_pop_filtered(gst_data->bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR);
        if (msg != NULL) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);
        }
        
        gst_object_unref(gst_data->pipeline);
        return;
    }
}

void VcuHelper::start_pipeline(GstData* gst_data) {
    GstState state;
    int ret;
    ret = gst_element_get_state(gst_data->pipeline, &state, NULL, 5 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to go to PLAYING state.\n");
        
        // Get error message if available
        GstMessage *msg = gst_bus_timed_pop_filtered(gst_data->bus, 0, GST_MESSAGE_ERROR);
        if (msg != NULL) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error: %s\n", err->message);
            g_printerr("Debug: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);
        }
        
        gst_object_unref(gst_data->pipeline);
        return;
    }
}


void VcuHelper::setup_multi_appsink_pipeline(const std::vector<std::string>& filenames, 
                                            const std::vector<float>& framerates,
                                            GstFlowReturn (*new_sample_callback)(GstAppSink *appsink, gpointer user_data), 
                                            std::vector<GstData*>& gst_data_list) {
    // Create a single pipeline for all sources
    GstElement* pipeline = gst_pipeline_new("multi-video-pipeline");
    if (!pipeline) {
        g_printerr("Failed to create main pipeline\n");
        return;
    }

    std::vector<GstElement*> elements;

    for (size_t i = 0; i < filenames.size(); i++) {
        GstData* gst_data = new GstData();
        std::string suffix = std::to_string(i);

        // Validate framerate vector size
        if (framerates.size() != filenames.size()) {
            g_printerr("Framerates vector size (%zu) doesn't match filenames size (%zu)\n", framerates.size(), filenames.size());
            delete gst_data;
            continue;
        }
        
        float desired_video_rate = framerates[i];
        
        gst_data->source = gst_element_factory_make("filesrc", ("source" + suffix).c_str());
        gst_data->demux = gst_element_factory_make("qtdemux", ("demux" + suffix).c_str());
        gst_data->parser = gst_element_factory_make("h264parse", ("parser" + suffix).c_str());
        gst_data->decoder = gst_element_factory_make("omxh264dec", ("decoder" + suffix).c_str());
        gst_data->videorate = gst_element_factory_make("videorate", ("videorate" + suffix).c_str());
        gst_data->capsfilter = gst_element_factory_make("capsfilter", ("capsfilter" + suffix).c_str());
        gst_data->appsink = gst_element_factory_make("appsink", ("appsink"+ suffix).c_str());

        // Check which elements failed to create
        if (!gst_data->source) g_printerr("Failed to create filesrc\n");
        if (!gst_data->demux) g_printerr("Failed to create qtdemux\n");
        if (!gst_data->parser) g_printerr("Failed to create h264parse\n");
        if (!gst_data->decoder) g_printerr("Failed to create omxh264dec\n");
        if (!gst_data->videorate) g_printerr("Failed to create videorate\n");
        if (!gst_data->capsfilter) g_printerr("Failed to create capsfilter\n");
        if (!gst_data->appsink) g_printerr("Failed to create appsink\n");

        if (!gst_data->source || !gst_data->demux || !gst_data->parser || !gst_data->videorate || !gst_data->decoder ||  !gst_data->capsfilter || !gst_data->appsink) {
            std::cerr << "Failed to create GStreamer elements" << std::endl;
            delete gst_data;
            continue;
        }

        // Setup zero-copy DMABUF allocator
        gst_data->allocator = gst_dmabuf_allocator_new();
        if (!gst_data->allocator) {
            g_printerr("Failed to create DMABUF allocator, trying FD allocator\n");
            gst_data->allocator = gst_fd_allocator_new();
        }
        if (!gst_data->allocator) {
            g_printerr("Failed to create any allocator, using default\n");
            gst_data->allocator = gst_allocator_find(NULL);
        }

        if (gst_data->allocator) {
            g_print("Created allocator: %s\n", G_OBJECT_TYPE_NAME(gst_data->allocator));
        }

        gst_data->video_index = i;
        g_object_set(gst_data->source, "location", filenames[i].c_str(), NULL);
        
        g_object_set(gst_data->decoder, "low-latency", 0, NULL);
        //g_object_set(gst_data->decoder, "split-input", TRUE, NULL);
        
        // Configure videorate for desired FPS with proper rate control
        g_object_set(gst_data->videorate, 
                     "max-rate", (int)desired_video_rate,  // Maximum output rate
                     "drop-only", TRUE,  // Only drop frames, no duplication to prevent bursts
                     "silent", FALSE,     // Enable logging for debugging
                     "average-period", GST_SECOND / 4,  // Shorter averaging period for tighter control
                     NULL);
        
        // Configure appsink for playback with DMABUF allocation
        g_object_set(gst_data->appsink, 
                     "emit-signals", TRUE,
                     "sync", TRUE,  // Disable synchronization to prevent catch-up
                     "max-buffers", 1,  // Reduced for tighter control
                     "drop", TRUE,  // Enable dropping to prevent catch-up bursts
                     "enable-last-sample", FALSE,
                     NULL);

        // Note: GstAppSink doesn't have an 'allocator' property
        // DMABUF allocation is handled through the allocation query probe
        
        // Note: GstOMXH264Dec also doesn't have an 'allocator' property  
        // DMABUF allocation is handled through caps negotiation and allocation queries

        // Connect to the new-sample signal
        g_signal_connect(gst_data->appsink, "new-sample", G_CALLBACK(new_sample_callback), gst_data);

        // Set up caps with desired FPS and NV12 format
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                           "format", G_TYPE_STRING, "NV12",
                                           "framerate", GST_TYPE_FRACTION, (int)desired_video_rate, 1,
                                           NULL);
        g_object_set(gst_data->capsfilter, "caps", caps, NULL);
        
        // Debug: Print the caps we're setting
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("Multi-Pipeline Setup - Setting capsfilter caps for stream %zu: %s\n", i, caps_str);
        g_free(caps_str);
        
        gst_caps_unref(caps);

        // Add allocation query pad probe for DMABUF support
        GstPad *appsink_pad = gst_element_get_static_pad(gst_data->appsink, "sink");
        gst_pad_add_probe(appsink_pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, 
                        appsink_query_cb, NULL, NULL);
        gst_object_unref(appsink_pad);

        // Add elements to main pipeline
        gst_bin_add_many(GST_BIN(pipeline), 
                        gst_data->source, gst_data->demux, gst_data->parser, 
                        gst_data->decoder, gst_data->videorate, gst_data->capsfilter, gst_data->appsink, NULL);

        // Connect pad-added signal for qtdemux dynamic pad handling
        g_signal_connect(gst_data->demux, "pad-added", G_CALLBACK(on_pad_added), gst_data);

        // Link elements step by step with error checking
        g_print("Linking source to demux for stream %zu...\n", i);
        if (!gst_element_link(gst_data->source, gst_data->demux)) {
            std::cerr << "Failed to link source to demux for stream " << i << std::endl;
            delete gst_data;
            continue;
        }

        // Note: qtdemux will connect to parser via pad-added callback when ready
        // Link parser to decoder
        g_print("Linking parser to decoder for stream %zu...\n", i);
        if (!gst_element_link(gst_data->parser, gst_data->decoder)) {
            std::cerr << "Failed to link parser to decoder for stream " << i << std::endl;
            delete gst_data;
            continue;
        }

        g_print("Linking decoder to videorate for stream %zu...\n", i);
        if (!gst_element_link(gst_data->decoder, gst_data->videorate)) {
            std::cerr << "Failed to link decoder to videorate for stream " << i << std::endl;
            delete gst_data;
            continue;
        }
        
        g_print("Linking videorate to capsfilter for stream %zu...\n", i);
        if (!gst_element_link(gst_data->videorate, gst_data->capsfilter)) {
            std::cerr << "Failed to link videorate to capsfilter for stream " << i << std::endl;
            delete gst_data;
            continue;
        }

        g_print("Linking capsfilter to appsink for stream %zu...\n", i);
        if (!gst_element_link(gst_data->capsfilter, gst_data->appsink)) {
            std::cerr << "Failed to link capsfilter to appsink for stream " << i << std::endl;
            delete gst_data;
            continue;
        }

        g_print("All links successful for stream %zu\n", i);

        // Set the shared pipeline pointer
        gst_data->pipeline = pipeline;
        
        gst_data_list.push_back(gst_data);
    }

    // Setup bus message handling for the main pipeline
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, pipeline); // Pass pipeline as user_data
    
    // Set bus pointer for all GstData structures
    for (auto& gst_data : gst_data_list) {
        gst_data->bus = bus;
    }

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the multi-video pipeline to the playing state.\n");
        // Get more detailed error information
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR);
        if (msg != NULL) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);
        }
        gst_object_unref(pipeline);
        return;
    }

    g_print("Multi-video pipeline started successfully\n");
}


void VcuHelper::cleanup_multi_pipeline(std::vector<GstData*>& gst_data_list){
    if (gst_data_list.empty()) {
        g_print("No pipeline data to cleanup\n");
        return;
    }
    
    // Get shared pipeline and bus from first element (they're all the same)
    GstElement* shared_pipeline = gst_data_list[0]->pipeline;
    GstBus* shared_bus = gst_data_list[0]->bus;
    
    g_print("Starting multi-pipeline cleanup...\n");

    // Remove bus watch to prevent callbacks during cleanup (only once for shared bus)
    if (shared_bus && GST_IS_BUS(shared_bus)) {
        gst_bus_remove_watch(shared_bus);
        g_print("Removed bus watch\n");
    }
    
    // Send EOS to flush remaining buffers (only once for shared pipeline)
    if (shared_pipeline && GST_IS_ELEMENT(shared_pipeline)) {
        gst_element_send_event(shared_pipeline, gst_event_new_eos());
        
        // Wait for EOS message with timeout
        if (shared_bus && GST_IS_BUS(shared_bus)) {
            GstMessage *msg = gst_bus_timed_pop_filtered(shared_bus, 2 * GST_SECOND, GST_MESSAGE_EOS);
            if (msg) {
                g_print("EOS received during cleanup\n");
                gst_message_unref(msg);
            } else {
                g_print("EOS timeout during cleanup\n");
            }
        }
    }
    
    // Stop pipeline (only once for shared pipeline)
    if (shared_pipeline && GST_IS_ELEMENT(shared_pipeline)) {
        GstStateChangeReturn ret = gst_element_set_state(shared_pipeline, GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_print("Failed to stop pipeline gracefully\n");
        } else {
            // Wait for state change to complete
            gst_element_get_state(shared_pipeline, NULL, NULL, 2 * GST_SECOND);
            g_print("Shared pipeline stopped successfully\n");
        }
    }
    
    // Clean up individual GstData structures
    for (auto& gst_data : gst_data_list) {
        g_print("Cleaning up resources for video %d...\n", gst_data->video_index);
        
        // Clean up individual allocator (each stream has its own)
        if (gst_data->allocator) {
            gst_object_unref(gst_data->allocator);
            gst_data->allocator = NULL;
            g_print("Allocator cleaned up for video %d\n", gst_data->video_index);
        }
        
        // Don't unref pipeline or bus here - they're shared and will be cleaned up once below
        gst_data->pipeline = NULL;
        gst_data->bus = NULL;
        
        // Delete the GstData object
        delete gst_data;
        
        g_print("Resources cleaned up for video %d\n", gst_data->video_index);
    }
    
    // Clean up shared resources (only once)
    if (shared_bus && GST_IS_BUS(shared_bus)) {
        gst_object_unref(shared_bus);
        g_print("Shared bus cleaned up\n");
    }
    
    if (shared_pipeline && GST_IS_ELEMENT(shared_pipeline)) {
        gst_object_unref(shared_pipeline);
        g_print("Shared pipeline cleaned up\n");
    }
    
    // Clear the list after cleanup
    gst_data_list.clear();
    
    g_print("Multi-pipeline cleanup completed successfully\n");
}
