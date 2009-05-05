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

// TODO
// -- replace Condition::wait with Condition::waitRelative
// -- use read/write locks

#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>
#include <utils/threads.h>
#include <utils/MemoryHeapPmem.h>
#include <utils/String16.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif

#include "linux/msm_mdp.h"
#include <linux/fb.h>
#include <linux/ioctl.h>

#include <media/msm_camera.h>



#define CAPTURE_RAW 0


#define PRINT_TIME 0

extern "C" {

#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdlib.h>

#include <camera.h>

#include <camframe.h>


#include "comdef.h"
#include "cam_mmap.h"



#include "jpeg_encoder.h"


#define PREVIEW_FRAMES_NUM  4

typedef struct {
    int width;
    int height;
} preview_size_type;

static preview_size_type preview_sizes[] = {
 	{ 800, 480 }, //WVGA
 	{ 640, 480 }, //VGA
    { 480, 320 }, // HVGA
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 176, 144 }, // QCIF
};

#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(preview_size_type))

#define DEFAULT_PREVIEW_SETTING 4
static const char* wb_lighting[] = {
			"minus1",       //CAMERA_WB_MIN_MINUS_1,
			"auto",         //CAMERA_WB_AUTO = 1,  /* This list must match aeecamera.h */
			"custom",       //CAMERA_WB_CUSTOM,
			"incandescent", //CAMERA_WB_INCANDESCENT,
			"florescent",   //CAMERA_WB_FLUORESCENT,
			"daylight",     //CAMERA_WB_DAYLIGHT,
			"cloudy",       //CAMERA_WB_CLOUDY_DAYLIGHT,
			"twilight",     //CAMERA_WB_TWILIGHT,
			"shade",        //CAMERA_WB_SHADE,
			"maxplus1"      //CAMERA_WB_MAX_PLUS_1

};

static const char* color_effects[] = {
			"minus1", // to match camera_effect_t 
			"none", //CAMERA_EFFECT_OFF = 1,  /* This list must match aeecamera.h */
			"b/w", //CAMERA_EFFECT_MONO,
			"negative", //CAMERA_EFFECT_NEGATIVE,
			"solarize", //CAMERA_EFFECT_SOLARIZE,
			"pastel", //CAMERA_EFFECT_PASTEL,
			"mosaic", //CAMERA_EFFECT_MOSAIC,
			"resize", //CAMERA_EFFECT_RESIZE,
			"sepia", //CAMERA_EFFECT_SEPIA,
			"postersize", //CAMERA_EFFECT_POSTERIZE,
			"whiteboard", //CAMERA_EFFECT_WHITEBOARD,
			"blackboard", //CAMERA_EFFECT_BLACKBOARD,
			"aqua", //CAMERA_EFFECT_AQUA,
			"maxplus" // CAMERA_EFFECT_MAX_PLUS_1
};

#define MAX_COLOR_EFFECTS 13
#define MAX_WBLIGHTING_EFFECTS 9

static const char* anti_banding_values[] = {
	"off",	//CAMERA_ANTIBANDING_OFF, as defined in qcamera/common/camera.h
	"60hz",	//CAMERA_ANTIBANDING_60HZ,
	"50hz",	//CAMERA_ANTIBANDING_50HZ,
	"auto",	//CAMERA_ANTIBANDING_AUTO,
	"max"		//CAMERA_MAX_ANTIBANDING,
};
#define MAX_ANTI_BANDING_VALUES	5
#define MAX_ZOOM_STEPS 6

static unsigned clp2(unsigned x) {
        x = x - 1;
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >>16);
        return x + 1;
    }

static inline void print_time()
{
#if PRINT_TIME
    struct timeval time; 
    gettimeofday(&time, NULL);
    LOGV("time: %lld us.", time.tv_sec * 1000000LL + time.tv_usec);
#endif
}

#if DLOPEN_LIBMMCAMERA
#include <dlfcn.h>
#endif
#include <sys/system_properties.h>

    
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

    


    /* callbacks */

#if DLOPEN_LIBMMCAMERA == 1
   
    

    /* Function pointers*/

    
    void* (*LINK_cam_conf)(
        void * data);

    void * (*LINK_cam_frame)(
        void * data);

    boolean (*LINK_jpeg_encoder_init)( );

	void (*LINK_jpeg_encoder_join)( );

    boolean (*LINK_jpeg_encoder_encode)(const char* file_name, const cam_ctrl_dimension_t *dimen, 
                                const byte* thumbnailbuf, int thumbnailfd,
                                const byte* snapshotbuf, int snapshotfd) ;
   
    void (**LINK_camframe_callback)(
        struct msm_frame_t * frame);

    void (*LINK_camframe_terminate)(
        void);

    void (**LINK_jpegfragment_callback)(uint8_t * buff_ptr , uint32 buff_size);

    void (**LINK_jpeg_callback)(void);

	 boolean (*LINK_jpeg_encoder_setMainImageQuality)(uint32_t quality);
#endif

}

#include "QualcommCameraHardware.h"

namespace android {

    static Mutex singleton_lock;
    
    static void mm_camframe_callback(struct msm_frame_t * frame);
    static void receivejpegfragment_callback(uint8_t * buff_ptr ,uint32 buff_size);
    static void receivejpeg_callback(void);
    static byte *hal_mmap (uint32 size, int *pmemFd);
	static int hal_munmap (int pmem_fd, void *addr, size_t size);

	
	static byte *hal_mmap (uint32 size, int *pmemFd)
	{
	  void	   *ret; /* returned virtual address */
	  int	 pmem_fd   = 0;
	
	  pmem_fd = open("/dev/pmem_adsp", O_RDWR);
	
	  if (pmem_fd < 0) {
		LOGI("do_mmap: Open device /dev/pmem_adsp failed!\n");
		return NULL;
	  }
	
	  /* to make it page size aligned */
	  size = (size + 4095) & (~4095);
     size=clp2(size);	
	  LOGV("do_mmap: pmem mmap size:%ld\n",size);
	
	  ret = mmap(NULL,
				 size,
				 PROT_READ	| PROT_WRITE,
				 MAP_SHARED,
				 pmem_fd,
				 0);
	
	  if (ret == MAP_FAILED) {
		LOGI("do_mmap: pmem mmap() failed: %s (%d)\n", strerror(errno), errno); 
		return NULL;
	  }
	
	  *pmemFd = pmem_fd;
	  return (byte *)ret;
	}
	
	static int hal_munmap (int pmem_fd, void *addr, size_t size)
	{
	  int rc;
	
	  size = (size + 4095) & (~4095);
     size = clp2(size);	
	  LOGV("munmapped size = %d, virt_addr = 0x%x\n", 
			   size, (uint32)addr);
	
	  rc = (munmap(addr, size));
	
	  close(pmem_fd);
	  pmem_fd = -1;
	
	  return rc;
	}
    

    QualcommCameraHardware::QualcommCameraHardware()
        : mParameters(),
          mPreviewHeight(-1),
          mPreviewWidth(-1),
          mRawHeight(-1),
          mRawWidth(-1),
          mCameraState(QCS_IDLE),
          mShutterCallback(0),
          mRawPictureCallback(0),
          mJpegPictureCallback(0),
          mPictureCallbackCookie(0),
          mAutoFocusCallback(0),
          mAutoFocusCallbackCookie(0),
          mPreviewCallback(0),
          mPreviewCallbackCookie(0),
          mRecordingCallback(0),
          mRecordingCallbackCookie(0),
          mPreviewFrameSize(0),
          mRawSize(0),
          mPreviewstatus(0),
		  mbrightness(0),
		  mZoomValueCurr(1),
		  mZoomValuePrev(1),
		  mZoomInitialised(FALSE),
		  mCameraRunning(FALSE)
    {

        LOGV("constructor E");

        {
            char env[PROP_VALUE_MAX];
            int len = __system_property_get("libcamera.debug.wait", env);
            if (len) {
                int wait = atoi(env);
                LOGE("PID %d (sleep %d seconds)",
                     getpid(), wait);
                sleep(wait);
            }
        }

        LOGV("constructor X");
    }


     static int camerafd;
	 static cam_parm_info_t pZoom;
     pthread_t cam_conf_thread, frame_thread;
     static struct msm_frame_t frames[PREVIEW_FRAMES_NUM];
     static cam_ctrl_dimension_t *dimension = NULL;
     static int pmemThumbnailfd = 0, pmemSnapshotfd = 0;
     static byte *thumbnail_buf, *main_img_buf;

    void QualcommCameraHardware::initDefaultParameters()
    {
        CameraParameters p;

	/**********INITIALIZING CAMERA DIMENSIONS********/
	   
	   preview_size_type* ps = &preview_sizes[DEFAULT_PREVIEW_SETTING];
	   p.setPreviewSize(ps->width, ps->height);

       dimension = (cam_ctrl_dimension_t *)malloc(sizeof(cam_ctrl_dimension_t));
       if (!dimension) {
       LOGE("main: malloc failed!\n");
	   return ; 
                               }
       memset(dimension, 0, sizeof(cam_ctrl_dimension_t)); 
  
       dimension->picture_width       = PICTURE_WIDTH;
       dimension->picture_height      = PICTURE_HEIGHT;
       dimension->display_width       = ps->width;
       dimension->display_height      = ps->height;
       dimension->ui_thumbnail_width  = THUMBNAIL_WIDTH;
       dimension->ui_thumbnail_height = THUMBNAIL_HEIGHT;

       p.setPictureSize(dimension->picture_width, dimension->picture_height );
       // Set default values
       p.set("effect", "none");
       p.set("whitebalance", "auto");
       p.set("antibanding", "off");
       p.set("luma-adaptation", "18");
       p.set("zoom", "1.0");
       p.set("jpeg-quality", "85");
	   
      if (setParameters(p) != NO_ERROR) {
            LOGE("Failed to set default parameters?!");
        }
    }

#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)


    void QualcommCameraHardware::startCameraIfNecessary()
    {
 	
 #if DLOPEN_LIBMMCAMERA == 1

            LOGV("loading libmmcamera");
            libmmcamera = ::dlopen("libmmcamera.so", RTLD_NOW);
            if (!libmmcamera) {
                LOGE("FATAL ERROR: could not dlopen libmmcamera.so: %s", dlerror());
                return;
            }
 
			libmmcamera_target = ::dlopen("libmm-qcamera-tgt.so", RTLD_NOW);
            if (!libmmcamera_target) {
                LOGE("FATAL ERROR: could not dlopen libmm-qcamera-tgt.so: %s", dlerror());
                return;
            }
         
    
            *(void **)&LINK_cam_conf =
                ::dlsym(libmmcamera_target, "cam_conf");

            *(void **)&LINK_cam_frame =
                ::dlsym(libmmcamera, "cam_frame");
            *(void **)&LINK_camframe_terminate =
                ::dlsym(libmmcamera, "camframe_terminate");

	        *(void **)&LINK_jpeg_encoder_init =
                ::dlsym(libmmcamera, "jpeg_encoder_init");

	        *(void **)&LINK_jpeg_encoder_encode =
                ::dlsym(libmmcamera, "jpeg_encoder_encode");

			*(void **)&LINK_jpeg_encoder_join =
                ::dlsym(libmmcamera, "jpeg_encoder_join");
         
            *(void **)&LINK_camframe_callback =
                ::dlsym(libmmcamera, "camframe_callback");

            *LINK_camframe_callback = mm_camframe_callback;

            *(void **)&LINK_jpegfragment_callback =
                ::dlsym(libmmcamera, "jpegfragment_callback");

            *LINK_jpegfragment_callback = receivejpegfragment_callback;

            *(void **)&LINK_jpeg_callback =
                ::dlsym(libmmcamera, "jpeg_callback");

            *LINK_jpeg_callback = receivejpeg_callback;

				*(void**)&LINK_jpeg_encoder_setMainImageQuality =
					::dlsym(libmmcamera, "jpeg_encoder_setMainImageQuality");

 #endif // DLOPEN_LIBMMCAMERA == 1

          camerafd = open(MSM_CAMERA, O_RDWR);
    if (camerafd < 0) {
          LOGE("interface_init: msm_camera opened failed!\n");
           return;
           }

    if (!LINK_jpeg_encoder_init()) {
          LOGE("jpeg_encoding_init failed.\n");
          return;
              }

          pthread_create(&cam_conf_thread,
                 NULL,
                 LINK_cam_conf,
                 NULL);
          usleep(500*1000);
          LOGV("init camera: initializing camera");

	     sp<CameraHardwareInterface> p =
            singleton.promote();
        if (UNLIKELY(p == 0)) {
            LOGE("camera object has been destroyed--returning immediately");
            return;
           }

 
    }

    status_t QualcommCameraHardware::dump(int fd, const Vector<String16>& args) const
    {
        const size_t SIZE = 256;
        char buffer[SIZE];
        String8 result;
        
        // Dump internal primitives.
        result.append("QualcommCameraHardware::dump: state (%d)\n", mCameraState);
        snprintf(buffer, 255, "preview width(%d) x height (%d)\n", mPreviewWidth, mPreviewHeight);
        result.append(buffer);
        snprintf(buffer, 255, "raw width(%d) x height (%d)\n", mRawWidth, mRawHeight);
        result.append(buffer);
        snprintf(buffer, 255, "preview frame size(%d), raw size (%d), jpeg size (%d) and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize, mJpegSize, mJpegMaxSize);
        result.append(buffer);
        write(fd, result.string(), result.size());
        
        // Dump internal objects.
        if (mPreviewHeap != 0) {
            mPreviewHeap->dump(fd, args);
        }
        if (mRawHeap != 0) {
            mRawHeap->dump(fd, args);
        }
        if (mJpegHeap != 0) {
            mJpegHeap->dump(fd, args);
        }
        mParameters.dump(fd, args);
        return NO_ERROR;
    }


     
  boolean QualcommCameraHardware::native_set_dimension (
    int camfd, 
    void *pDim)
{
  boolean rc = TRUE;
  int ioctlRetVal;
  struct msm_ctrl_cmd_t ctrlCmd;

  cam_ctrl_dimension_t *pDimension = (cam_ctrl_dimension_t *)pDim;

  ctrlCmd.type 	     = CAMERA_SET_PARM_DIMENSION;
  ctrlCmd.timeout_ms = 200000;
  ctrlCmd.length     = sizeof(cam_ctrl_dimension_t);
  ctrlCmd.value      = pDimension;


  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
      LOGE("native_set_dimension: ioctl failed... ioctl return value is %d \n",
        ioctlRetVal);
    rc = FALSE;
  }

  memcpy(pDimension, 
         (cam_ctrl_dimension_t *)ctrlCmd.value,
         sizeof(cam_ctrl_dimension_t));

  rc = ctrlCmd.status;

	return rc;
}


void QualcommCameraHardware::reg_unreg_buf(
  int camfd,
  int width,
  int height,
  int pmempreviewfd,
  byte *prev_buf,
  enum msm_pmem_t type,
  boolean unregister,
  boolean active)
{
  uint32 y_size;
  struct msm_pmem_info_t pmemBuf;
  uint32 ioctl_cmd;
  int ioctlRetVal;

  y_size    = width * height;

  pmemBuf.type     = type; 
  pmemBuf.fd       = pmempreviewfd;
  pmemBuf.vaddr    = prev_buf;
  pmemBuf.y_off    = 0;
  pmemBuf.cbcr_off = PAD_TO_WORD(y_size);
  pmemBuf.active   = active;

  if ( unregister ) {
    ioctl_cmd = MSM_CAM_IOCTL_UNREGISTER_PMEM;
  } else {
    ioctl_cmd = MSM_CAM_IOCTL_REGISTER_PMEM;
  }

    LOGV("Entered reg_unreg_buf: camfd = %d, ioctl_cmd = %d, pmemBuf.cbcr_off=%d, active=%d\n",
        camfd, ioctl_cmd, pmemBuf.cbcr_off, active);
  if ((ioctlRetVal = ioctl(camfd, ioctl_cmd, &pmemBuf)) < 0) {
      LOGE("reg_unreg_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM ioctl failed... ioctl return value is %d \n",
        ioctlRetVal);
  }
  
}


boolean QualcommCameraHardware::native_register_preview_bufs(
  int camfd, 
  void *pDim, 
  struct msm_frame_t *frame,
  boolean active)
{
  cam_ctrl_dimension_t *dimension = (cam_ctrl_dimension_t *)pDim;
    LOGV("dimension->display_width = %d, display_height = %d\n", 
        dimension->display_width, dimension->display_height);

  reg_unreg_buf(camfd, 
        dimension->display_width, 
        dimension->display_height, 
        frame->fd, 
        (byte *)frame->buffer,
        MSM_PMEM_OUTPUT2,
        FALSE,
        active);

  return TRUE;
}

boolean QualcommCameraHardware::native_unregister_preview_bufs(
  int camfd,
  void *pDim, 
  int pmempreviewfd, 
  byte *prev_buf)
{
  cam_ctrl_dimension_t *dimension = (cam_ctrl_dimension_t *)pDim;

  reg_unreg_buf(camfd,
        dimension->display_width,
        dimension->display_height,
        pmempreviewfd,
        prev_buf,
        MSM_PMEM_OUTPUT2,
        TRUE, 
        TRUE); 

  return TRUE;
}

boolean QualcommCameraHardware::native_start_preview(int camfd)
{
  int ioctlRetVal = TRUE;
	struct msm_ctrl_cmd_t ctrlCmd;

	ctrlCmd.timeout_ms = 200000;
	ctrlCmd.type       = CAMERA_START_PREVIEW; 
	ctrlCmd.length     = 0;
	ctrlCmd.value      = NULL;

  ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd);
  if(ioctlRetVal < 0) {
      LOGE("native_start_preview: MSM_CAM_IOCTL_CTRL_COMMAND failed. ioctlRetVal=%d \n",
              ioctlRetVal);
    ioctlRetVal = ctrlCmd.status;
    return FALSE;
  }
  return TRUE;
}


boolean QualcommCameraHardware::native_register_snapshot_bufs(
  int camfd, 
  void *pDim, 
  int pmemthumbnailfd, 
  int pmemsnapshotfd, 
  byte *thumbnail_buf, 
  byte *main_img_buf)
{
  cam_ctrl_dimension_t *dimension = (cam_ctrl_dimension_t *)pDim;

  reg_unreg_buf(camfd, 
        dimension->thumbnail_width, 
        dimension->thumbnail_height, 
        pmemthumbnailfd, 
        thumbnail_buf,
        MSM_PMEM_THUMBAIL,
        FALSE,
        TRUE);
  
  /* For original snapshot*/
  reg_unreg_buf(camfd, 
        dimension->orig_picture_dx, 
        dimension->orig_picture_dy, 
        pmemsnapshotfd, 
        main_img_buf,
        MSM_PMEM_MAINIMG,
        FALSE,
        TRUE);

  return TRUE;
}

boolean QualcommCameraHardware::native_unregister_snapshot_bufs(
  int camfd,
  void *pDim,
  int pmemThumbnailfd, 
  int pmemSnapshotfd, 
  byte *thumbnail_buf, 
  byte *main_img_buf)
{
  cam_ctrl_dimension_t *dimension = (cam_ctrl_dimension_t *)pDim;

  reg_unreg_buf(camfd, 
        dimension->thumbnail_width, 
        dimension->thumbnail_height, 
        pmemThumbnailfd, 
        thumbnail_buf,
        MSM_PMEM_THUMBAIL,
        TRUE,
        TRUE);
  
  /* For original snapshot*/
  reg_unreg_buf(camfd, 
        dimension->orig_picture_dx, 
        dimension->orig_picture_dy, 
        pmemSnapshotfd, 
        main_img_buf,
        MSM_PMEM_MAINIMG,
        TRUE,
        TRUE);
 
  return TRUE;
}



boolean QualcommCameraHardware::native_get_picture (int camfd)
{
  int ioctlRetVal = TRUE;
	struct msm_ctrl_cmd_t ctrlCmd;

  ctrlCmd.timeout_ms = 50000;
  ctrlCmd.length     = 0;
  ctrlCmd.value      = NULL;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_GET_PICTURE, &ctrlCmd)) < 0) { 
      LOGE("native_get_picture: MSM_CAM_IOCTL_GET_PICTURE failed... ioctl return value is %d \n", ioctlRetVal);
    ioctlRetVal = ctrlCmd.status;
    return FALSE;
  }

	return TRUE;
}


boolean QualcommCameraHardware::native_stop_preview (int camfd)
{
  int ioctlRetVal = TRUE;
	struct msm_ctrl_cmd_t ctrlCmd;
	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_STOP_PREVIEW; 
	ctrlCmd.length     = 0;
	ctrlCmd.value      = NULL;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
      LOGE("native_stop_preview: ioctl failed. ioctl return value is %d \n", ioctlRetVal);
    ioctlRetVal = ctrlCmd.status;
    return FALSE;
  }

	return TRUE;
}


boolean QualcommCameraHardware::native_start_snapshot (int camfd)
{
  int ioctlRetVal;
	struct msm_ctrl_cmd_t ctrlCmd;

	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_START_SNAPSHOT; 
	ctrlCmd.length     = 0;
	ctrlCmd.value      = NULL;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) { 
      LOGE("native_start_snapshot: ioctl failed. ioctl return value is %d \n", ioctlRetVal);
    ioctlRetVal = ctrlCmd.status;
    return FALSE;
  }

	return TRUE;
}

boolean QualcommCameraHardware::native_stop_snapshot (int camfd)
{
  int ioctlRetVal;
	struct msm_ctrl_cmd_t ctrlCmd;

	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_STOP_SNAPSHOT; 
	ctrlCmd.length     = 0;
	ctrlCmd.value      = NULL;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
      LOGE("native_stop_snapshot: ioctl failed. ioctl return value is %d \n", ioctlRetVal);
    return FALSE;
  }

	return TRUE;
}


boolean QualcommCameraHardware::native_jpeg_encode (
  void *pDim,
  int pmemThumbnailfd, 
  int pmemSnapshotfd, 
  byte *thumbnail_buf, 
  byte *main_img_buf)
{
  char jpegFileName[256] = {0};
  static int snapshotCntr = 0;

  cam_ctrl_dimension_t *dimension = (cam_ctrl_dimension_t *)pDim;

  sprintf(jpegFileName, "snapshot_%d.jpg", ++snapshotCntr);

#ifndef SURF8K
  LOGV("native_jpeg_encode , current jpeg main img quality =%d", mParameters.getJpegMainimageQuality());
    if (! LINK_jpeg_encoder_setMainImageQuality(mParameters.getJpegMainimageQuality())) {
      LOGE("native_jpeg_encode set jpeg main image quality :%d@%s: jpeg_encoder_encode failed.\n",
        __LINE__, __FILE__);
      return FALSE;
  }
#endif
  if ( !LINK_jpeg_encoder_encode(jpegFileName, dimension,  
                            thumbnail_buf, pmemThumbnailfd,
      main_img_buf, pmemSnapshotfd)) {
      LOGV("native_jpeg_encode:%d@%s: jpeg_encoder_encode failed.\n", __LINE__, __FILE__);
    return FALSE;
  }
  return TRUE;
}


    static int frame_count = 1;
    static struct msm_frame_t  lastframe ;
    
    bool QualcommCameraHardware::initPreview()
    {

        LOGV("initPreview: preview size=%dx%d", mPreviewWidth, mPreviewHeight);

		int cnt = 0;

        // Init the SF buffers
		mPreviewFrameSize = mPreviewWidth * mPreviewHeight * 3/2; 
        mPreviewHeap =
            new PreviewPmemPool(kRawFrameHeaderSize +
                                mPreviewWidth * mPreviewHeight * 3/2,
                                kPreviewBufferCount,
                                mPreviewFrameSize,
                                kRawFrameHeaderSize,
                                "preview");
        
        if (!mPreviewHeap->initialized()) {
            mPreviewHeap = NULL;
            return false;
        }

	 dimension->picture_width		= PICTURE_WIDTH;
	 dimension->picture_height		= PICTURE_HEIGHT;
	 if(native_set_dimension(camerafd, dimension) == TRUE) {
		LOGI("hal display_width = %d height = %d\n",
			 (int)dimension->display_width, (int)dimension->display_height);
        frame_size= (clp2(dimension->display_width * dimension->display_height *3/2));
          boolean activeBuffer;

          for (cnt = 0; cnt < PREVIEW_FRAMES_NUM; cnt++) {
              frames[cnt].fd = mPreviewHeap->mHeapnew[cnt]->heapID();
              frames[cnt].buffer = (unsigned long)mPreviewHeap->mHeapnew[cnt]->base();
              LOGE("hal_mmap #%d start = %x end = %x", (int)cnt, (int)frames[cnt].buffer,
                   (int)(frames[cnt].buffer + frame_size - 1));

              frames[cnt].y_off = 0;
              frames[cnt].cbcr_off = dimension->display_width * dimension->display_height;

         if (frames[cnt].buffer == 0) {
		  LOGV("main: malloc failed!\n");
		  return 0;
	   }

        if (cnt == PREVIEW_FRAMES_NUM-1) {
           activeBuffer = FALSE; 
          } else {
           activeBuffer = TRUE; 
          }
        frames[cnt].path = MSM_FRAME_ENC;
 
        LOGV("do_mmap pbuf = 0x%x, pmem_fd = %d, active = %d\n", 
         (unsigned int)frames[cnt].buffer, frames[cnt].fd, activeBuffer);
        native_register_preview_bufs(camerafd, 
                                    dimension, 
                                    &frames[cnt],
                                    activeBuffer);
    }
  }

    if (frame_count ==1) {
              frame_count--;
              lastframe = frames[PREVIEW_FRAMES_NUM-1];
		pthread_create(&frame_thread,
                   NULL,
                   LINK_cam_frame,
                   &frames[PREVIEW_FRAMES_NUM-1]); 
            }

        return true;
    }

    void QualcommCameraHardware::deinitPreview()
    {
        mPreviewHeap = NULL;
    }

    static int pict_count = 1;
  
    bool QualcommCameraHardware::initRaw(bool initJpegHeap)
    {
        LOGV("initRaw E");
     
          LOGV("initRaw: picture size=%dx%d",
             mRawWidth, mRawHeight);
       dimension->picture_width   = mRawWidth;
       dimension->picture_height  = mRawHeight;    

    if ((native_set_dimension(camerafd, dimension) != TRUE)) {
	 	 return FALSE;
	 	}

	 mRawSize =
            mRawWidth * mRawHeight * 1.5 ; 

        mJpegMaxSize = mRawWidth * mRawHeight * 1.5; 
      
        LOGE("initRaw: clearing old mJpegHeap.");
        mJpegHeap = NULL;

        LOGV("initRaw: initializing mRawHeap.");
        mRawHeap =
            new RawPmemPool("/dev/pmem_adsp",
                            kRawFrameHeaderSize + mJpegMaxSize, 
                            kRawBufferCount,
                            mRawSize,
                            kRawFrameHeaderSize,
                            "snapshot camera");

        if (!mRawHeap->initialized()) {
            LOGE("initRaw X failed: error initializing mRawHeap");
            mRawHeap = NULL;
            return false;
        }

    if (pict_count) {

        pict_count--;
        thumbnail_buf = hal_mmap(THUMBNAIL_BUFFER_SIZE, 
                                        &pmemThumbnailfd);

            LOGV("do_mmap thumbnail pbuf = 0x%x, pmem_fd = %d\n", 
                    (unsigned int)thumbnail_buf, pmemThumbnailfd);
            if (thumbnail_buf == NULL)
              LOGE("cannot allocate thumbnail memory");

	        main_img_buf = (byte *)mRawHeap->mHeap->base();
            pmemSnapshotfd = mRawHeap->mHeap->getHeapID();
			
            LOGV("do_mmap snapshot pbuf = 0x%x, pmem_fd = %d\n", (unsigned int)main_img_buf, pmemSnapshotfd);
            if (main_img_buf == NULL)
              LOGE("cannot allocate main memory");
          
            native_register_snapshot_bufs(camerafd, 
                                           dimension, 
                                           pmemThumbnailfd, 
                                           pmemSnapshotfd, 
                                           thumbnail_buf, 
                                           main_img_buf);
      	}

        


        if (initJpegHeap) {
            LOGV("initRaw: initializing mJpegHeap.");
            mJpegHeap =
                new AshmemPool(mJpegMaxSize,
                               kJpegBufferCount,
                               0, // we do not know how big the picture wil be
                               0,
                               "jpeg");
            if (!mJpegHeap->initialized()) {
                LOGE("initRaw X failed: error initializing mJpegHeap.");
                mJpegHeap = NULL;
                mRawHeap = NULL;
                return false;
            }
        }

        LOGV("initRaw X success");
        return true;
    }

    void QualcommCameraHardware::release()
    {
        LOGV("release E");
        Mutex::Autolock l(&mLock);

	 int cnt , rc;
	 struct msm_ctrl_cmd_t ctrlCmd;
   
        Mutex::Autolock singletonLock(&singleton_lock);
 
         LOGI("Exiting the app\n");


  ctrlCmd.timeout_ms = 50000;
  ctrlCmd.length = 0;
  ctrlCmd.value = NULL;
  ctrlCmd.type = (uint16)CAMERA_EXIT;
    if (ioctl(camerafd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
    LOGE("ioctl with CAMERA_EXIT failed\n");
  }
    if (pthread_join(cam_conf_thread, NULL) != 0) {
    LOGE("config_thread exit failure!\n");
    } else {
  LOGE("pthread_cancel succeeded on conf_thread\n");
  }

  LINK_camframe_terminate();

    if (pthread_join(frame_thread, NULL) != 0) {
    LOGE("frame_thread exit failure!\n");
    } else
  LOGE("pthread_cancel succeeded on frame_thread\n");

    if (!frame_count) {
      for (cnt = 0; cnt < PREVIEW_FRAMES_NUM-1; ++cnt) {
    LOGV("unregisterPreviewBuf %d\n", cnt);
    native_unregister_preview_bufs(camerafd, 
                            dimension, 
                            frames[cnt].fd, 
                            (byte *)frames[cnt].buffer);
    LOGV("do_munmap preview buffer %d, fd=%d, prev_buf=0x%x, size=%d\n", 
            cnt, frames[cnt].fd, (unsigned int)frames[cnt].buffer,frame_size);
    rc = hal_munmap(frames[cnt].fd, (byte *)frames[cnt].buffer,frame_size);
    LOGV("do_munmap done with return value %d\n", rc);
	
    }
     LOGV("unregisterPreviewBuf %d\n", cnt);
    native_unregister_preview_bufs(camerafd, 
                            dimension, 
                            lastframe.fd, 
                            (byte *)lastframe.buffer);
    rc = hal_munmap(lastframe.fd, (byte *)lastframe.buffer,frame_size);
    LOGV("do_munmap done with return value %d\n", rc);


  frame_count = 1;
 }

    if (pmemThumbnailfd > 0 && pmemSnapshotfd > 0 && !pict_count) {
    native_unregister_snapshot_bufs(camerafd,
                            dimension,
                            pmemThumbnailfd, pmemSnapshotfd,  
                            thumbnail_buf, main_img_buf);

    rc = hal_munmap(pmemThumbnailfd, thumbnail_buf, 
            THUMBNAIL_BUFFER_SIZE);
    if ( TRUE ) {
      LOGV("do_munmap thumbnail buffer return value: %d\n", rc);
    }
   pict_count++;
                  
  } 

  free(dimension);
  close(camerafd);


        
            
#if DLOPEN_LIBMMCAMERA
            if (libmmcamera) {
                unsigned ref = ::dlclose(libmmcamera);
                LOGV("dlclose(libmmcamera) refcount %d", ref);
            }

			if (libmmcamera_target) {
                unsigned ref = ::dlclose(libmmcamera_target);
                LOGV("dlclose(libmmcamera_target) refcount %d", ref);
            }


#endif
         
        LOGV("release X");
    }

    QualcommCameraHardware::~QualcommCameraHardware()
    {
        LOGV("~QualcommCameraHardware E");
        singleton.clear();
        LOGV("~QualcommCameraHardware X");
    }
    
    sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
    {
        LOGV("getRawHeap");
        return mRawHeap != NULL ? mRawHeap->mHeap : NULL;
    }

    sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
    {
        LOGV("getPreviewHeap");
        return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
    }

    sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeapnew(int i) const
    {
        LOGV("getPreviewHeap");
        return mPreviewHeap != NULL ? mPreviewHeap->mHeapnew[i] : NULL;
    }

    status_t QualcommCameraHardware::startPreview(preview_callback cb,
                   void* user)
    {
        LOGV("startPreview E");
        Mutex::Autolock l(&mLock);
		
		{
            Mutex::Autolock cbLock(&mCallbackLock);
      if (mPreviewstatus) {
            mPreviewCallback = cb;
            mPreviewCallbackCookie = user;
            return NO_ERROR;
		} 
		} 

         
        if (!initPreview()) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }

        {
            Mutex::Autolock cbLock(&mCallbackLock);
            mPreviewCallback = cb;
            mPreviewCallbackCookie = user;
            mPreviewstatus = TRUE;
			mCameraRunning = mParameters.getCameraEnabledVal();
        }

    if (!native_start_preview(camerafd)) {
               LOGE("main: start_preview failed!\n");
      		   return UNKNOWN_ERROR;
 
        	}
		mCameraRunning = mParameters.getCameraEnabledVal();
		LOGV(" Camera App is Running %d %s ",mCameraRunning, (mCameraRunning ? "Yes" : "No"));
#ifndef SURF8K
    if (mCameraRunning == 1) {
                        mZoomValuePrev = 1;
                        mZoomValueCurr = 1;
			setSensorPreviewEffect(camerafd, mParameters.getEffect());
			setSensorWBLighting(camerafd, mParameters.getWBLighting());		
			setAntiBanding(camerafd, mParameters.getAntiBanding());
		}
#endif
        LOGV("waiting for QCS_PREVIEW_IN_PROGRESS");

        LOGV("startPreview X");
        return NO_ERROR;
    }

    void QualcommCameraHardware::stopPreviewInternal()
    {
        LOGV("stopPreviewInternal E");
		
         int cnt , rc;
   
        if (mAutoFocusCallback != NULL) {
            // WARNING: clear mAutoFocusCallback though it doesnt work now
            
            mAutoFocusCallback = NULL;
        
        }

        {
            Mutex::Autolock cbLock(&mCallbackLock);
            mPreviewCallback = NULL;
            mPreviewCallbackCookie = NULL;
            mCameraRunning = 0;
            if(mRecordingCallback != NULL)
               return;
            if(!mPreviewstatus)
               return;
            mPreviewstatus = NULL;
            mZoomInitialised = FALSE;

        }
        

        native_stop_preview(camerafd);


        LOGV("stopPreviewInternal: Freeing preview heap.");
        mPreviewHeap = NULL;
        mPreviewCallback = NULL;




        LOGV("stopPreviewInternal: X Preview has stopped.");
    }


    void QualcommCameraHardware::stopPreview() {
        LOGV("stopPreview: E");
        Mutex::Autolock l(&mLock);
		{
		  	LOGE("WAIT FOR QCS IDLE IN STOP PREVIEW");
		   Mutex::Autolock lock(&mStateLock);
               while(mCameraState != QCS_IDLE)
		    	mStateWait.wait(mStateLock);
		  	}
           LOGE("WAIT FOR QCS IDLE COMPLETE IN STOP PREVIEW");
        stopPreviewInternal();
        LOGV("stopPreview: X");
    }

    status_t QualcommCameraHardware::autoFocus(autofocus_callback af_cb,
                                               void *user)
    {
        LOGV("Starting auto focus.");
        Mutex::Autolock l(&mLock);
    
        mAutoFocusCallback = af_cb;
        mAutoFocusCallbackCookie = user;
		//WARNING: RETURNING TRUE  for autofocus callback
        mAutoFocusCallback(TRUE,mAutoFocusCallbackCookie);
        
        return NO_ERROR;
    }


    status_t QualcommCameraHardware::takePicture(shutter_callback shutter_cb,
                                                 raw_callback raw_cb,
                                                 jpeg_callback jpeg_cb,
                                                 void* user)
    {
        LOGV("takePicture: E raw_cb = %p, jpeg_cb = %p",
             raw_cb, jpeg_cb);
		 Mutex::Autolock l(&mLock);
        print_time();

	    int rc ;

  
    if (jpeg_cb !=NULL) {
		   Mutex::Autolock lock(&mStateLock);
               while(mCameraState != QCS_IDLE)
		    	mStateWait.wait(mStateLock);
                mCameraState = QCS_WAITING_JPEG;
		  	}
            stopPreviewInternal();
     
        if (!initRaw(jpeg_cb!=NULL)) {
            LOGE("initRaw failed.  Not taking picture.");
            return UNKNOWN_ERROR;
        }

		
        {
            Mutex::Autolock cbLock(&mCallbackLock);
			LOGE("TAKE PICTURE LOCK ACQUIRED");
            mShutterCallback = shutter_cb;
            mRawPictureCallback = raw_cb;
            mJpegPictureCallback = jpeg_cb;
            mPictureCallbackCookie = user;
        }

        if (native_start_snapshot(camerafd) == FALSE) {
            LOGE("main:%d start_preview failed!\n", __LINE__);
            return UNKNOWN_ERROR;
          }
		  usleep(100*1000);
          receiveRawPicture();
         
        LOGV("takePicture: X");
        print_time();
        return NO_ERROR;
    }

    status_t QualcommCameraHardware::cancelPicture(
        bool cancel_shutter, bool cancel_raw, bool cancel_jpeg)
    {
        LOGV("cancelPicture: E cancel_shutter = %d, cancel_raw = %d, cancel_jpeg = %d",
             cancel_shutter, cancel_raw, cancel_jpeg);
            Mutex::Autolock l(&mLock);
     
            {
                Mutex::Autolock cbLock(&mCallbackLock);
                if (cancel_shutter) mShutterCallback = NULL;
                if (cancel_raw) mRawPictureCallback = NULL;
                if (cancel_jpeg) mJpegPictureCallback = NULL;
            }

        LINK_jpeg_encoder_join();


        LOGV("cancelPicture: X");
        return NO_ERROR;
    }

    status_t QualcommCameraHardware::setParameters(
        const CameraParameters& params)
    {
        LOGV("setParameters: E params = %p", &params);

        Mutex::Autolock l(&mLock);
   
        mParameters = params;
        
        
		int width, height;
		params.getPreviewSize(&width, &height);
		LOGV("requested size %d x %d", width, height);
		preview_size_type* ps = preview_sizes;
		size_t i;
		for (i = 0; i < PREVIEW_SIZE_COUNT; ++i, ++ps) {
			if (width >= ps->width && height >= ps->height) break;
			}
		if (i == PREVIEW_SIZE_COUNT) ps--;
		LOGV("actual size %d x %d", ps->width, ps->height);
		mParameters.setPreviewSize(ps->width, ps->height);

		dimension->display_width       = ps->width;
        dimension->display_height      = ps->height;
        
        mParameters.getPreviewSize(&mPreviewWidth, &mPreviewHeight);
        mParameters.getPictureSize(&mRawWidth, &mRawHeight);

        mPreviewWidth = (mPreviewWidth + 1) & ~1;
        mPreviewHeight = (mPreviewHeight + 1) & ~1;
        mRawHeight = (mRawHeight + 1) & ~1;
        mRawWidth = (mRawWidth + 1) & ~1;

		mCameraRunning = mParameters.getCameraEnabledVal();
		LOGV(" Camera App is Running %d %s ",mCameraRunning, (mCameraRunning ? "Yes" : "No"));

#ifndef SURF8K
	
    if (mCameraRunning == 1) {
      if (mbrightness !=  mParameters.getBrightness()) {
	     		LOGV(" new brightness value : %d ", mParameters.getBrightness());
	 		    mbrightness =  mParameters.getBrightness();
	 	 	    // also validate mbrightness here
	 		    setBrightness(mbrightness);
	 	    }
			mZoomValueCurr = mParameters.getZoomValue();
      if (mZoomValuePrev != mZoomValueCurr) {
				boolean ZoomDirectionIn = TRUE;
        if (mZoomValuePrev > mZoomValueCurr) {
					ZoomDirectionIn = FALSE;
        } else {
					ZoomDirectionIn = TRUE;
				}
				LOGV(" New ZOOM value : %2.2f Direction = %s ", mZoomValueCurr, (ZoomDirectionIn ? "ZoomIn" : "ZoomOut"));
				mZoomValuePrev = mZoomValueCurr;
				performZoom(ZoomDirectionIn);
			}
		}	
#endif			

        LOGV("setParameters: X ");
        return NO_ERROR ;
    }

    CameraParameters QualcommCameraHardware::getParameters() const
    {
        LOGV("getParameters: EX");
        return mParameters;
    }

    extern "C" sp<CameraHardwareInterface> openCameraHardware()
    {
        LOGV("openCameraHardware: call createInstance");
        return QualcommCameraHardware::createInstance();
    }

    wp<QualcommCameraHardware> QualcommCameraHardware::singleton;

    // If the hardware already exists, return a strong pointer to the current
    // object. If not, create a new hardware object, put it in the singleton,
    // and return it.
    sp<CameraHardwareInterface> QualcommCameraHardware::createInstance()
    {
        LOGV("createInstance: E");

        Mutex::Autolock lock(&singleton_lock);
        if (singleton != 0) {
            sp<CameraHardwareInterface> hardware = singleton.promote();
            if (hardware != 0) {
                LOGV("createInstance: X return existing hardware=%p", &(*hardware));
                return hardware;
            }
        }

        {
            struct stat st;
            int rc = stat("/dev/oncrpc", &st);
            if (rc < 0) {
                LOGV("createInstance: X failed to create hardware: %s", strerror(errno));
                return NULL;
            }
        }

        QualcommCameraHardware *cam = new QualcommCameraHardware();
        sp<QualcommCameraHardware> hardware(cam);
        singleton = hardware;
        
        cam->initDefaultParameters();
        cam->startCameraIfNecessary();
        LOGV("createInstance: X created hardware=%p", &(*hardware));
        return hardware;
    }

    // For internal use only, hence the strong pointer to the derived type.
    sp<QualcommCameraHardware> QualcommCameraHardware::getInstance()
    {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
        //    LOGV("getInstance: X old instance of hardware");
            return sp<QualcommCameraHardware>(static_cast<QualcommCameraHardware*>(hardware.get()));
        } else {
            LOGV("getInstance: X new instance of hardware");
            return sp<QualcommCameraHardware>();
        }
    }

    

#if 1
    static void dump_to_file(const char *fname,
                             uint8_t *buf, uint32_t size)
    {
        int nw, cnt = 0;
        uint32_t written = 0;

        LOGD("opening file [%s]\n", fname);
        int fd = open(fname, O_RDWR | O_CREAT);
        if (fd < 0) {
            LOGE("failed to create file [%s]: %s", fname, strerror(errno));
            return;
        }

        LOGD("writing %d bytes to file [%s]\n", size, fname);
        while (written < size) {
            nw = ::write(fd,
                         buf + written,
                         size - written);
            if (nw < 0) {
                LOGE("failed to write to file [%s]: %s",
                     fname, strerror(errno));
                break;
            }
            written += nw;
            cnt++;
        }
        LOGD("done writing %d bytes to file [%s] in %d passes\n",
             size, fname, cnt);
        ::close(fd);
    }
#endif // CAMERA_RAW

 static ssize_t previewframe_offset = 3;
 static int dcount = 0;

    void QualcommCameraHardware::receivePreviewFrame(struct msm_frame_t *frame)
    {
        Mutex::Autolock cbLock(&mCallbackLock);

        if (mPreviewCallback == NULL)
           return;

        if ((unsigned int)mPreviewHeap->mHeapnew[previewframe_offset]->base() !=
                        (unsigned int)frame->buffer)
            for (previewframe_offset = 0; previewframe_offset < 4; previewframe_offset++) {
                if ((unsigned int)mPreviewHeap->mHeapnew[previewframe_offset]->base() ==
                    (unsigned int)frame->buffer)
                    break;
            }
       
    if (mPreviewCallback != NULL) {
			mPreviewCallback(mPreviewHeap->mBuffers[previewframe_offset],
                             previewframe_offset, mPreviewCallbackCookie);
        }
    if (mRecordingCallback != NULL) {
               mReleaseRecordingFrame = FALSE;
               mRecordingCallback(mPreviewHeap->mBuffers[previewframe_offset],
                             mRecordingCallbackCookie);             
    }
        previewframe_offset--;
        previewframe_offset &= 3;

    if (mRecordingCallback != NULL) {
		//	 Mutex::Autolock rLock(&mRecordFrameLock);
      while (mReleaseRecordingFrame!=TRUE) {
				LOGV("block for release frame request/command");
				mRecordWait.wait(mCallbackLock);
			 }
        }
     
    }

	status_t QualcommCameraHardware::startRecording(
        recording_callback rcb, void *ruser)
    {
        LOGV("start Recording E");
        Mutex::Autolock l(&mLock);
         
		{
            Mutex::Autolock cbLock(&mCallbackLock);
      if (mPreviewstatus) {
            mRecordingCallback = rcb;
            mRecordingCallbackCookie = ruser;
            return NO_ERROR;
		} 
		} 

    if (!initPreview()) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }

        {
            Mutex::Autolock cbLock(&mCallbackLock);
            mRecordingCallback = rcb;
            mRecordingCallbackCookie = ruser;
            mPreviewstatus = TRUE;
			mCameraRunning = mParameters.getCameraEnabledVal();
        }

    if (!native_start_preview(camerafd)) {
               LOGE("main: start_preview failed!\n");
      		   return UNKNOWN_ERROR;
 
        }
        LOGV("waiting for QCS_PREVIEW_IN_PROGRESS");

        LOGV("Start Recording X");
        return NO_ERROR;
	}

    void QualcommCameraHardware::stopRecording() 
	{
        LOGV("stopRecording: E");
        Mutex::Autolock l(&mLock);		
		{
            Mutex::Autolock cbLock(&mCallbackLock);
            mRecordingCallback = NULL;
            mRecordingCallbackCookie = NULL;
            mReleaseRecordingFrame = TRUE;
            mRecordWait.signal();
            mCameraRunning = 0;
            if(mPreviewCallback != NULL)
                   return;
            mPreviewstatus = NULL;
		}

		native_stop_preview(camerafd);

		LOGV("stopRecording: Freeing preview heap.");
		mPreviewHeap = NULL;
		mRecordingCallback = NULL;
        LOGV("stopRecording: X");
    }

	void QualcommCameraHardware::releaseRecordingFrame(
		   const sp<IMemory>& mem __attribute__((unused)))
	   {
	       LOGV("Release Recording Frame E");
		   Mutex::Autolock l(&mLock);
		   LOGV("Release Recording Frame mlock acquired");
           Mutex::Autolock rLock(&mCallbackLock);
		   LOGV("Release Recording Frame callback lock");

           mReleaseRecordingFrame = TRUE;
		   mRecordWait.signal();
           LOGV("Release Recording Frame X");
	  }

	bool QualcommCameraHardware::recordingEnabled() 
	{
			Mutex::Autolock l(&mLock);
			return (mPreviewCallback != NULL && mRecordingCallback != NULL);
	}
    void
    QualcommCameraHardware::notifyShutter()
    {
        LOGV("notifyShutter: E");
        print_time();
        if (mShutterCallback)
            mShutterCallback(mPictureCallbackCookie);
        print_time();
        LOGV("notifyShutter: X");
    }

   

static ssize_t snapshot_offset = 0;

    void
    QualcommCameraHardware::receiveRawPicture()
    {
        LOGV("receiveRawPicture: E");
        print_time();

     //   Mutex::Autolock cbLock(&mCallbackLock);

		int ret,rc,rete;
		notifyShutter();
        if (mRawPictureCallback != NULL) {

         if(native_get_picture(camerafd)== FALSE) {
            LOGE("main:%d getPicture failed!\n", __LINE__);
	     return;
          }

          ssize_t offset = (mRawWidth * mRawHeight  * 1.5 * snapshot_offset);
          
		 #if CAPTURE_RAW	
	     dump_to_file("data/photo_qc.raw",
                         (uint8_t *)main_img_buf , mRawWidth * mRawHeight * 1.5 );
         #endif

                LOGV("I am in receivesnapshot frames%d",(int)snapshot_offset);

                mRawPictureCallback(mRawHeap->mBuffers[offset],
                                    mPictureCallbackCookie);
            
    } else LOGV("Raw-picture callback was canceled--skipping.");


// : PASSINT THE RAW IMAGE TO JPEG ENCODE ENGINE        

        if (mJpegPictureCallback != NULL) {

			mJpegSize = 0;
 
      if (!(native_jpeg_encode(dimension, pmemThumbnailfd, pmemSnapshotfd,thumbnail_buf,main_img_buf))) {
	        LOGE("jpeg encoding failed\n");
		}
           }
        if (mJpegPictureCallback == NULL) {
            LOGV("JPEG callback was cancelled--not encoding image.");
            // We need to keep the raw heap around until the JPEG is fully
            // encoded, because the JPEG encode uses the raw image contained in
            // that heap.
      if (pmemThumbnailfd > 0 && pmemSnapshotfd > 0 && !pict_count) {
               native_unregister_snapshot_bufs(camerafd,
                            dimension,
                            pmemThumbnailfd, pmemSnapshotfd,
                            thumbnail_buf, main_img_buf);

               rc = hal_munmap(pmemThumbnailfd, thumbnail_buf,
                         THUMBNAIL_BUFFER_SIZE);
            if ( TRUE ) {
               LOGV("do_munmap thumbnail buffer return value: %d\n", rc);
                }

             pict_count++;
            }
            mRawHeap = NULL;
        }                    
        print_time();
        LOGV("receiveRawPicture: X");
    }


   void
    QualcommCameraHardware::receiveJpegPictureFragment(
        uint8_t * buff_ptr , uint32 buff_size)
    {
        
        uint32 remaining = mJpegHeap->mHeap->virtualSize();
        remaining -= mJpegSize;
	 uint8_t * base = (uint8_t *)mJpegHeap->mHeap->base();

        LOGV("receiveJpegPictureFragment");
          

        if (buff_size > remaining) {
            LOGE("receiveJpegPictureFragment: size %d exceeds what "
                 "remains in JPEG heap (%d), truncating",
                 buff_size,
                 remaining);
            buff_size = remaining;
        }
        memcpy(base + mJpegSize, buff_ptr, buff_size);
        mJpegSize += buff_size;
    }

    void
    QualcommCameraHardware::receiveJpegPicture(void)
    {
        LOGV("receiveJpegPicture: E image (%d bytes out of %d)",
             mJpegSize, mJpegHeap->mBufferSize);
        print_time();
        Mutex::Autolock cbLock(&mCallbackLock);

        int index = 0,rc;

        if (mJpegPictureCallback) {
            // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
            // that the JPEG image's size will probably change from one snapshot
            // to the next, so we cannot reuse the MemoryBase object.
            sp<MemoryBase> buffer = new
                MemoryBase(mJpegHeap->mHeap,
                           index * mJpegHeap->mBufferSize +
                           mJpegHeap->mFrameOffset,
                           mJpegSize);
            
            mJpegPictureCallback(buffer, mPictureCallbackCookie);
            buffer = NULL;
    } else LOGV("JPEG callback was cancelled--not delivering image.");

        // NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
        // to keep the heap around until the encoding is complete.

           
          mJpegHeap = NULL;
            

    if (pmemThumbnailfd > 0 && pmemSnapshotfd > 0 && !pict_count) {
               native_unregister_snapshot_bufs(camerafd,
                            dimension,
                            pmemThumbnailfd, pmemSnapshotfd,  
                            thumbnail_buf, main_img_buf);

               rc = hal_munmap(pmemThumbnailfd, thumbnail_buf, 
                         THUMBNAIL_BUFFER_SIZE);
            if ( TRUE ) {
               LOGV("do_munmap thumbnail buffer return value: %d\n", rc);
                }
             
	     pict_count++;
            }   
	   mRawHeap = NULL;     

	                  {
					   
					   Mutex::Autolock lock(&mStateLock);
					   LOGV(" LOCK ACQUIRED in receive jpegpicture");
					   mCameraState = QCS_IDLE;
					   mStateWait.signal();	
					   LOGV(" SIGNALLED QCS_IDLE in receivejpegpicture");
					   
					  }
        print_time();
        LOGV("receiveJpegPicture: X callback done.");
    }

bool QualcommCameraHardware::previewEnabled()
{
        Mutex::Autolock l(&mLock);
	return mPreviewCallback != NULL;
}
    
void  QualcommCameraHardware::setSensorPreviewEffect(int camfd, const char* effect)
{
   LOGV("In setSensorPreviewEffect ... ");
	int ioctlRetVal = TRUE, effectsValue = 1;
	struct msm_ctrl_cmd_t ctrlCmd;

	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_SET_PARM_EFFECT;
	ctrlCmd.length     = sizeof(uint32);
        ctrlCmd.value      = NULL;
    for (int i=1; i<MAX_COLOR_EFFECTS; i++) {
      if (! strcmp(color_effects[i], effect)) {
   		LOGV("In setSensorPreviewEffect ..., color effect match : %s : %d ", effect, i);
		effectsValue = i;
			ctrlCmd.value      = (void *)&effectsValue;
			break;
		}
	}
        if(ctrlCmd.value != NULL)
        {
                if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
                LOGV("setSensorPreviewEffect : ioctl failed. ioctl return value is %d \n", ioctlRetVal);
                }
        }
        else
        {
                LOGV(" setSensorPreviewEffect : No match found for %s ", effect);
        }
}

void QualcommCameraHardware::setSensorWBLighting(int camfd, const char* lighting)
{
	 int ioctlRetVal = TRUE, lightingValue = 1;
	 struct msm_ctrl_cmd_t ctrlCmd;
	
	 ctrlCmd.timeout_ms = 5000;
	 ctrlCmd.type		= CAMERA_SET_PARM_WB;
	 ctrlCmd.length 	= sizeof(uint32);
         ctrlCmd.value      = NULL;
    for (int i = 1; i < MAX_WBLIGHTING_EFFECTS; i++) {
      if (! strcmp(wb_lighting[i], lighting)) {
		 	 LOGV("In setSensorWBLighting : Match : %s : %d ", lighting, i);
		 	 lightingValue = i;
			 ctrlCmd.value		= (void *)&lightingValue;
			 break;
		 }
	 }
         if(ctrlCmd.value != NULL)
         {
                 if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
                         LOGV("setSensorWBLighting  : ioctl failed. ioctl return value is %d \n", ioctlRetVal);
                 }
         }
         else
         {
                 LOGV(" setSensorWBLighting : No match found for %s ", lighting);
         }
	
}

void  QualcommCameraHardware::setAntiBanding(int camfd, const char* antibanding)
{
	 int ioctlRetVal = TRUE, antibandvalue = 0;
	struct msm_ctrl_cmd_t ctrlCmd;

	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_SET_PARM_ANTIBANDING;
	ctrlCmd.length     = sizeof(int32);

    for (int i=0; i<MAX_ANTI_BANDING_VALUES; i++) {
      if (! strcmp(anti_banding_values[i], antibanding)) {
   		LOGV("In setAntiBanding ..., setting match : %s : %d ", antibanding, i);
			antibandvalue = i;
			ctrlCmd.value      = (void *)&antibandvalue;

			if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
          LOGE("setAntiBanding : ioctl failed. ioctl return value is %d \n", ioctlRetVal);
			}
			break;
		}
	}
}

void QualcommCameraHardware::setBrightness(int brightness)
{
	int ioctlRetVal = TRUE;
	struct msm_ctrl_cmd_t ctrlCmd;
	LOGV("In setBrightness ...,  :  %d ", brightness);
	ctrlCmd.timeout_ms = 5000;
	ctrlCmd.type       = CAMERA_SET_PARM_BRIGHTNESS;
	ctrlCmd.length     = sizeof(int);
	ctrlCmd.value      = (void *)&brightness;

	if((ioctlRetVal = ioctl(camerafd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
	LOGV("setBrightness : ioctl failed. ioctl return value is %d \n", ioctlRetVal);
	}
}
boolean QualcommCameraHardware::native_get_zoom(int camfd, void *pZm)
{
  boolean rc = TRUE;
  int ioctlRetVal;
  struct msm_ctrl_cmd_t ctrlCmd;

  cam_parm_info_t *pZoom = (cam_parm_info_t *)pZm;

  ctrlCmd.type		 = CAMERA_GET_PARM_ZOOM;
  ctrlCmd.timeout_ms = 200000;
  ctrlCmd.length	 = sizeof(cam_parm_info_t);
  ctrlCmd.value 	 = pZoom;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
      LOGE("native_get_zoom: ioctl failed... ioctl return value is %d \n", ioctlRetVal);
	rc = FALSE;
  }
    LOGV(" native_get_ZOOM :: Current val=%ld Max val=%ld Min val = %ld Step val=%ld \n", pZoom->current_value, 
	pZoom->maximum_value, pZoom->minimum_value ,pZoom->step_value);

  memcpy(pZoom,(cam_parm_info_t *)ctrlCmd.value, sizeof(cam_parm_info_t));

  rc = ctrlCmd.status;

  return rc;

}
boolean QualcommCameraHardware::native_set_zoom(int camfd, void *pZm)
{
  boolean rc = TRUE;
  int ioctlRetVal;
  struct msm_ctrl_cmd_t ctrlCmd;

  int32 *pZoom = (int32 *)pZm;

  ctrlCmd.type		 = CAMERA_SET_PARM_ZOOM;
  ctrlCmd.timeout_ms = 200000;
  ctrlCmd.length	 = sizeof(int32);
  ctrlCmd.value 	 = pZoom;

  if((ioctlRetVal = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
      LOGE("native_set_zoom: ioctl failed... ioctl return value is %d \n", ioctlRetVal);
	rc = FALSE;
  }

  memcpy(pZoom,(int32 *)ctrlCmd.value,sizeof(int32));

  rc = ctrlCmd.status;

  return rc;

}
void QualcommCameraHardware::performZoom(boolean ZoomDir)
{
    if (mZoomInitialised == FALSE) {
      /*native_get_zoom(camerafd, (void *)&pZoom);*/
      pZoom.current_value = CAMERA_DEF_ZOOM;
      pZoom.step_value    = CAMERA_ZOOM_STEP;  
      pZoom.maximum_value = CAMERA_MAX_ZOOM;
      pZoom.minimum_value = CAMERA_MIN_ZOOM;
      if (pZoom.maximum_value != 0) {
			mZoomInitialised = TRUE;
			pZoom.step_value = (int) (pZoom.maximum_value/MAX_ZOOM_STEPS);
			if( pZoom.step_value > 3 )
 				pZoom.step_value = 3;
		}
	}
    if (ZoomDir) {
      LOGV(" performZoom :: Got Zoom value of %ld %ld %ld Zoom In", pZoom.current_value,
			pZoom.step_value, pZoom.maximum_value);	
      if ((pZoom.current_value + pZoom.step_value) < pZoom.maximum_value) {
			pZoom.current_value += pZoom.step_value;
        LOGV(" performZoom :: Setting Zoom value of %ld ",pZoom.current_value);
			native_set_zoom(camerafd, (void *)&pZoom.current_value);

      } else {
        LOGE(" performZoom :: Not able to Zoom In %ld %ld %ld",pZoom.current_value,
				pZoom.step_value, pZoom.maximum_value);			
		}
    } else {
      LOGV(" performZoom :: Got Zoom value of %ld %ld %ld Zoom Out",pZoom.current_value,
			pZoom.step_value, pZoom.minimum_value);	
      if ((pZoom.current_value - pZoom.step_value) >= pZoom.minimum_value) {
			pZoom.current_value -= pZoom.step_value;
        LOGV(" performZoom :: Setting Zoom value of %ld ",pZoom.current_value);
			native_set_zoom(camerafd, (void *)&pZoom.current_value);
      } else {
        LOGE(" performZoom :: Not able to Zoom Out %ld %ld %ld",pZoom.current_value,
				pZoom.step_value, pZoom.maximum_value);			
		}	
	}
}
    QualcommCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                             int frame_size,
                                             int frame_offset,
                                             const char *name) :
        mBufferSize(buffer_size),
        mNumBuffers(num_buffers),
        mFrameSize(frame_size),
        mFrameOffset(frame_offset),
        mBuffers(NULL), mName(name)
    {
        // empty
    }

    void QualcommCameraHardware::MemPool::completeInitialization()
    {
        // If we do not know how big the frame will be, we wait to allocate
        // the buffers describing the individual frames until we do know their
        // size.

        if (mFrameSize > 0) {
            mBuffers = new sp<MemoryBase>[mNumBuffers];
            for (int i = 0; i < mNumBuffers; i++) {
                mBuffers[i] = new
                    MemoryBase(mHeap,
                               i * mBufferSize + mFrameOffset,
                               mFrameSize);
            }
        }
    }

void QualcommCameraHardware::MemPool::completeInitializationnew()
{
LOGD("QualcommCameraHardware::MemPool::completeInitializationnew");

    if (mFrameSize > 0) {
        mBuffers = new sp<MemoryBase>[mNumBuffers];
        for (int i = 0; i < mNumBuffers; i++) {
LOGI("SFbufs: i = %d mBufferSize = %d mFrameOffset = %d mFrameSize = %d\n", i, mBufferSize, mFrameOffset, mFrameSize);
            mBuffers[i] = new
                MemoryBase(mHeapnew[i], 0, mFrameSize);
        }
    }
}

    QualcommCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                                   int frame_size,
                                                   int frame_offset,
                                                   const char *name) :
        QualcommCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
            LOGV("constructing MemPool %s backed by ashmem: "
                 "%d frames @ %d bytes, offset %d, "
                 "buffer size %d",
                 mName,
                 num_buffers, frame_size, frame_offset, buffer_size);

            int page_mask = getpagesize() - 1;
            int ashmem_size = buffer_size * num_buffers;
            ashmem_size += page_mask;
            ashmem_size &= ~page_mask;

            mHeap = new MemoryHeapBase(ashmem_size);

            completeInitialization();
    }

    QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                               int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name,
                                               int flag) :
        QualcommCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
        LOGV("constructing MemPool %s backed by pmem pool %s: "
             "%d frames @ %d bytes, offset %d, buffer size %d",
             mName,
             pmem_pool, num_buffers, frame_size, frame_offset,
             buffer_size);

        ptypeflag = flag;
        
        // Make a new mmap'ed heap that can be shared across processes.
        
        mAlignedSize = clp2(buffer_size * num_buffers);
        
        sp<MemoryHeapBase> masterHeap = 
            new MemoryHeapBase(pmem_pool, mAlignedSize, 0);
        sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, 0);
        if (pmemHeap->getHeapID() >= 0) {
            pmemHeap->slap();
            masterHeap.clear();
            mHeap = pmemHeap;
            pmemHeap.clear();
            
            mFd = mHeap->getHeapID();
            if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
                LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                     pmem_pool,
                     ::strerror(errno), errno);
                mHeap.clear();
                return;
            }
            
            LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) is %ld",
                 pmem_pool,
                 mSize.len);
            
            completeInitialization();
    } else LOGE("pmem pool %s error: could not create master heap!",
                  pmem_pool);
    }
   
    QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                               int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name) :
        QualcommCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
        // Create separate heaps for each buffer

        sp<MemoryHeapBase> masterHeap;
        sp<MemoryHeapPmem> pmemHeap;

        ptypeflag = 0;
        
        buffer_size = clp2(buffer_size);
        for (int i = 0; i < num_buffers; i++) {
            masterHeap = new MemoryHeapBase(pmem_pool, buffer_size, 0);
            pmemHeap = new MemoryHeapPmem(masterHeap, 0);
LOGE("pmemheap: id = %d base = %x", (int)pmemHeap->getHeapID(), (unsigned int)pmemHeap->base());
            if (pmemHeap->getHeapID() >= 0) {
                pmemHeap->slap();
                masterHeap.clear();
                mHeapnew[i] = pmemHeap;
                pmemHeap.clear();
                
                mFd = mHeapnew[i]->getHeapID();
                if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
                    LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                     pmem_pool,
                     ::strerror(errno), errno);
                    mHeapnew[i].clear();
                    return;
                }
                
                LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) is %ld",
                     pmem_pool,
                     mSize.len);
            }
            else {
                LOGE("pmem pool %s error: could not create master heap!", pmem_pool);
            }
        }
        completeInitializationnew();
    }


    QualcommCameraHardware::PreviewPmemPool::PreviewPmemPool(
            int buffer_size, int num_buffers,
            int frame_size,
            int frame_offset,
            const char *name,
            int flag) :
        QualcommCameraHardware::PmemPool("/dev/pmem_adsp",
                                         buffer_size,
                                         num_buffers,
                                         frame_size,
                                         frame_offset,
                                         name,
                                         flag)
    {
        LOGV("constructing PreviewPmemPool");
        if (initialized()) {
            //NOTE : SOME PREVIEWPMEMPOOL SPECIFIC CODE MAY BE ADDED
        }
    }

    QualcommCameraHardware::PreviewPmemPool::PreviewPmemPool(
            int buffer_size, int num_buffers,
            int frame_size,
            int frame_offset,
            const char *name) :
        QualcommCameraHardware::PmemPool("/dev/pmem_adsp",
	                                 buffer_size,
                                         num_buffers,
                                         frame_size,
                                         frame_offset,
                                         name)
    {
LOGD("QualcommCameraHardware::PreviewPmemPool::PreviewPmemPool");
        LOGV("constructing PreviewPmemPool");
        if (initialized()) {
            //NOTE : SOME PREVIEWPMEMPOOL SPECIFIC CODE MAY BE ADDED
        }
    }

    QualcommCameraHardware::PreviewPmemPool::~PreviewPmemPool()
    {
        LOGV("destroying PreviewPmemPool");
        if(initialized()) {
			LOGV("destroying PreviewPmemPool");
        }
    }

    QualcommCameraHardware::RawPmemPool::RawPmemPool(
            const char *pmem_pool,
            int buffer_size, int num_buffers,
            int frame_size,
            int frame_offset,
            const char *name) :
        QualcommCameraHardware::PmemPool(pmem_pool,
                                         buffer_size,
                                         num_buffers,
                                         frame_size,
                                         frame_offset,
                                         name, 1)
    {
        LOGV("constructing RawPmemPool");

        if (initialized()) {

		//NOTE : SOME RAWPMEMPOOL SPECIFIC CODE MAY BE ADDED
            
        }
    }

    QualcommCameraHardware::RawPmemPool::~RawPmemPool()
    {
        LOGV("destroying RawPmemPool");
        if(initialized()) {
            LOGV("releasing RawPmemPool memory");
            
        }
    }
    
    QualcommCameraHardware::MemPool::~MemPool()
    {
        LOGV("destroying MemPool %s", mName);
        if (mFrameSize > 0)
            delete [] mBuffers;
        if (mHeap != NULL)
            mHeap.clear();
        LOGV("destroying MemPool %s completed", mName);        
    }
    
    status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
    {
        const size_t SIZE = 256;
        char buffer[SIZE];
        String8 result;
        snprintf(buffer, 255, "QualcommCameraHardware::AshmemPool::dump\n");
        result.append(buffer);
        if (mName) {
            snprintf(buffer, 255, "mem pool name (%s)\n", mName);
            result.append(buffer);
        }
        if (mHeap != 0) {
            snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                     mHeap->getBase(), mHeap->getSize(),
                     mHeap->getFlags(), mHeap->getDevice());
            result.append(buffer);
        }
        snprintf(buffer, 255, "buffer size (%d), number of buffers (%d),"
                 " frame size(%d), and frame offset(%d)\n",
                 mBufferSize, mNumBuffers, mFrameSize, mFrameOffset);
        result.append(buffer);
        write(fd, result.string(), result.size());
        return NO_ERROR;
    }
    
    static void mm_camframe_callback(struct msm_frame_t * frame)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receivePreviewFrame(frame);
        }
        
    }
   
   static void receivejpegfragment_callback(uint8_t * buff_ptr , uint32 buff_size)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveJpegPictureFragment(buff_ptr,buff_size);
        }
        
    }

   static void receivejpeg_callback(void)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveJpegPicture();
        }
        
    }    

}; // namespace android
