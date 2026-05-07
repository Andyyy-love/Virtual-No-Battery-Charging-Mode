#ifndef DRM_RESOURCE_H
#define DRM_RESOURCE_H

#include <string.h>
#include "drm_c/drm_mode_info.h"
#include "drm_c/drm_mode_fb.h"
#include "drm_c/drm_crtc.h"
#include "drm_c/drm_encoder.h"
#include "drm_c/drm_connector.h"
#include <stdbool.h>

//using namespace android;

#undef LOG_TAG
#define LOG_TAG "Charging Drm"
#define DRM_NODE_PATH "/dev/dri/card0"
#define MAX_BO_SIZE 2

static int countCrtc = 0;
static int countEncoder = 0;
static int countConnector = 0;

enum lcd_count
{
  NO_LCD,
  MAIN_LCD,
  EXT_LCD_FOLDABLE,
  MAX_LCD_DISPLAY
};

//-----------------------------------------------------------------------------
struct DrmBo
{
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t pitches[4];
  uint32_t offsets[4];
  uint32_t gemHandles[4];
  uint64_t modifier[4];
  uint32_t fbId;
  size_t size;
  int fd;
};
//-----------------------------------------------------------------------------

typedef struct
{
  int mDrmFd;
  //--- dynamic array to store data ------------
  Drm_crtc *mCrtcList; // array of Drm_crtc structure pointer
  Drm_encoder *mEncoderList;
  Drm_connector *mConnectorList;
  //---------- Pointer object to strcture ----------------
  Drm_crtc *mCrtc;
  Drm_encoder *mEncoder;
  Drm_connector *mConnector;
  drmModeFB fb;
  drmModeCrtcPtr drmCrtc;
  //----------- hold static data --------------
  int mDisplayWidth;
  int mDisplayHeight;
  struct DrmBo mBo[MAX_BO_SIZE];
  size_t mPinPon;
  //dualdisplay - parameters for 2nd LCD
  Drm_crtc *mCrtc_ext;
  Drm_encoder *mEncoder_ext;
  Drm_connector *mConnector_ext;
  drmModeFB fb_ext;
  //----------- hold static data --------------
  int mDisplayWidth_ext;
  int mDisplayHeight_ext;
  struct DrmBo mBo_ext[MAX_BO_SIZE];
  size_t mPinPon_ext;
} Drm_resource;

int init_resource(Drm_resource *drm_resource_object);
int initDrmResource(Drm_resource *drm_resource_object);
int initCrtc(Drm_resource *drm_resource_object, drmModeResPtr r);
int initEncoder(Drm_resource *drm_resource_object, drmModeResPtr r);
int initConnector(Drm_resource *drm_resource_object, drmModeResPtr r);
int findDisplay(Drm_resource *drm_resource_object);
int createFramebuffer(Drm_resource *drm_resource_object);
int allocateBuffer(Drm_resource *drm_resource_object, struct DrmBo *bo);
int createDumb(Drm_resource *drm_resource_object, struct DrmBo *bo);
int addFb(Drm_resource *drm_resource_object, struct DrmBo *bo); // same
int freeBuffer(Drm_resource *drm_resource_object, struct DrmBo *bo);
int removeFb(Drm_resource *drm_resource_object, uint32_t fbId);
int destroyDumb(Drm_resource *drm_resource_object, struct DrmBo *bo); // added extra parameter Drm_resource drm_resource_object. Try to make it global after UT/IT
void draw(Drm_resource *drm_resource_object);                         // to bind the createframbuffer via setcrtc method
int getDrmFb(Drm_resource *drm_resource_object);                      // to get drmfb data
uint32_t getDrmBPP(uint32_t format);
void freeResource(Drm_resource *drm_reso_object);
int drm_get_lcd_num(void);
// api to check if ext lcm is present
bool is_ext_lcd_present(void);
#endif