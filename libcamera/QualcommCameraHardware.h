/*
** Copyright 2008, Google Inc.
** Copyright (c) 2009, Code Aurora Forum.All rights reserved.
** 
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H

#include <ui/CameraHardwareInterface.h>
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapBase.h>

extern "C" {
    #include <camera.h>
    #include <jpege.h>
    #include <linux/android_pmem.h>
    #include <media/msm_camera.h>
    #include <camframe.h>
    #include "comdef.h"
}

namespace android {

class QualcommCameraHardware : public CameraHardwareInterface {
public:

    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getPreviewHeapnew(int i) const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    startPreview(preview_callback cb, void* user);
    virtual void        stopPreview();
    virtual status_t    startRecording(recording_callback cb, void* user);
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);
    virtual status_t    autoFocus(autofocus_callback, void *user);
    virtual status_t    takePicture(shutter_callback,
                                    raw_callback,
                                    jpeg_callback,
                                    void* user);
    virtual status_t    cancelPicture(bool cancel_shutter,
                                      bool cancel_raw, bool cancel_jpeg);
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;

    virtual void release();

    static sp<CameraHardwareInterface> createInstance();
    static sp<QualcommCameraHardware> getInstance();

    

    boolean native_set_dimension (int camfd, void *pDim);
	void reg_unreg_buf(int camfd,int width,int height,int pmempreviewfd,byte *prev_buf,enum msm_pmem_t type,boolean unregister,boolean active);
    boolean native_register_preview_bufs(int camfd, void *pDim, struct msm_frame_t *frame,boolean active);
	boolean native_unregister_preview_bufs(int camfd,void *pDim, int pmempreviewfd, byte *prev_buf);
    
	boolean native_start_preview(int camfd);
	boolean native_stop_preview(int camfd);

	boolean native_register_snapshot_bufs(int camfd, void *pDim, int pmemthumbnailfd, int pmemsnapshotfd, byte *thumbnail_buf, byte *main_img_buf);
    boolean native_unregister_snapshot_bufs(int camfd,void *pDim,int pmemThumbnailfd, int pmemSnapshotfd, byte *thumbnail_buf, byte *main_img_buf);
    boolean native_get_picture(int camfd, struct crop_info_t *cropInfo);
	boolean native_start_snapshot(int camfd);
    boolean native_stop_snapshot(int camfd);
    boolean native_jpeg_encode (void *pDim,  int pmemThumbnailfd,   int pmemSnapshotfd,   byte *thumbnail_buf,   byte *main_img_buf
, void *pCrop);

	boolean native_set_zoom(int camfd, void *pZm);
	boolean native_get_zoom(int camfd, void *pZm);


    void receivePreviewFrame(struct msm_frame_t *frame);
    void receiveJpegPicture(void);
    void receiveJpegPictureFragment(
        uint8_t * buff_ptr , uint32 buff_size);
	bool        previewEnabled(); 
	

private:

    QualcommCameraHardware();
    virtual ~QualcommCameraHardware();
    void stopPreviewInternal();

    static wp<QualcommCameraHardware> singleton;

    /* These constants reflect the number of buffers that libmmcamera requires
       for preview and raw, and need to be updated when libmmcamera
       changes.
    */
    static const int kPreviewBufferCount = 4;
    static const int kRawBufferCount = 1;
    static const int kJpegBufferCount = 1;
    static const int kRawFrameHeaderSize = 0;

    //TODO: put the picture dimensions in the CameraParameters object;
    CameraParameters mParameters;
    int mPreviewHeight;
    int mPreviewWidth;
    int mRawHeight;
    int mRawWidth;
	unsigned int frame_size;
	int mbrightness;
	float mZoomValuePrev, mZoomValueCurr;
	boolean mZoomInitialised;
	int mCameraRunning;
   
    
    // This class represents a heap which maintains several contiguous
    // buffers.  The heap may be backed by pmem (when pmem_pool contains
    // the name of a /dev/pmem* file), or by ashmem (when pmem_pool == NULL).

    struct MemPool : public RefBase {
        MemPool(int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name);

        virtual ~MemPool() = 0;

        void completeInitialization();
        void completeInitializationnew();
        bool initialized() const { 
            return ((mHeapnew[3] != NULL && mHeapnew[3]->base() != MAP_FAILED) ||
                   (mHeap != NULL && mHeap->base() != MAP_FAILED));
        }

        virtual status_t dump(int fd, const Vector<String16>& args) const;

        int mBufferSize;
        int mNumBuffers;
        int mFrameSize;
        int mFrameOffset;
        sp<MemoryHeapBase> mHeap;
        sp<MemoryHeapBase> mHeapnew[4];
        sp<MemoryBase> *mBuffers;

        const char *mName;
    };

    struct AshmemPool : public MemPool {
        AshmemPool(int buffer_size, int num_buffers,
                   int frame_size,
                   int frame_offset,
                   const char *name);
    };

    struct PmemPool : public MemPool {
        PmemPool(const char *pmem_pool,
                int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name,
		int flag);
        PmemPool(const char *pmem_pool,
                int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name);
        virtual ~PmemPool(){ }
        int mFd;
        uint32_t mAlignedSize;
        struct pmem_region mSize;
        int ptypeflag;  // 1 = single heap, 0 = multiple heaps
    };

    struct PreviewPmemPool : public PmemPool {
        virtual ~PreviewPmemPool();
        PreviewPmemPool(int buffer_size, int num_buffers,
                        int frame_size,
                        int frame_offset,
                        const char *name);
        PreviewPmemPool(int buffer_size, int num_buffers,
                        int frame_size,
                        int frame_offset,
                        const char *name,
			int flag);
    };

    struct RawPmemPool : public PmemPool {
        virtual ~RawPmemPool();
        RawPmemPool(const char *pmem_pool,
                    int buffer_size, int num_buffers,
                    int frame_size,
                    int frame_offset,
                    const char *name);
    };

    sp<PreviewPmemPool> mPreviewHeap;
    sp<RawPmemPool> mRawHeap;
    sp<AshmemPool> mJpegHeap;
	
    void startCameraIfNecessary();
    bool initPreview();
    void deinitPreview();
    bool initRaw(bool initJpegHeap);

    void initDefaultParameters();
    
	 void  setSensorPreviewEffect(int, const char*);
	 void  setSensorWBLighting(int, const char*);
     void  setAntiBanding(int, const char*); 
	 void  setBrightness(int);
     void  performZoom(boolean);
    
    //THE STATE MACHINE REMOVED FROM HAL
	//RETAINING THE STATE ENUM STRUCTURE and mStatelock mutex IF 
	// STATE TRANSITION READDED

    enum qualcomm_camera_state {
		QCS_UNDEFINED,
        QCS_INIT,
        QCS_IDLE,
        QCS_ERROR,
        QCS_PREVIEW_IN_PROGRESS,
        QCS_WAITING_RAW,
        QCS_WAITING_JPEG,
        /* internal states */
        QCS_INTERNAL_PREVIEW_STOPPING,
        QCS_INTERNAL_PREVIEW_REQUESTED,
        QCS_INTERNAL_RAW_REQUESTED
    };

    volatile qualcomm_camera_state mCameraState;
	Mutex mStateLock;
    Mutex mLock;
    bool mReleaseRecordingFrame;

    void notifyShutter();

 
    boolean receiveRawPicture(void);

    Mutex mCallbackLock;
	Mutex mRecordLock;
	Mutex mRecordFrameLock;
	Condition mRecordWait;
    Condition mStateWait;

    /* mJpegSize keeps track of the size of the accumulated JPEG.  We clear it
       when we are about to take a picture, so at any time it contains either
       zero, or the size of the last JPEG picture taken.
    */
    uint32_t mJpegSize;
   

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void                *mPictureCallbackCookie;

    autofocus_callback  mAutoFocusCallback;
    void                *mAutoFocusCallbackCookie;

    preview_callback    mPreviewCallback;
    void                *mPreviewCallbackCookie;
    recording_callback  mRecordingCallback;
    void                *mRecordingCallbackCookie;
    unsigned int        mPreviewFrameSize;
    int                 mRawSize;
    int                 mJpegMaxSize;
	bool 				mPreviewstatus;	

#if DLOPEN_LIBMMCAMERA == 1
    void *libmmcamera;
    void *libmmcamera_target;
#endif
};

}; // namespace android

#endif


