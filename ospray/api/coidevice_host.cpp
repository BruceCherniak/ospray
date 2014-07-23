/********************************************************************* *\
 * INTEL CORPORATION PROPRIETARY INFORMATION                            
 * This software is supplied under the terms of a license agreement or  
 * nondisclosure agreement with Intel Corporation and may not be copied 
 * or disclosed except in accordance with the terms of that agreement.  
 * Copyright (C) 2014 Intel Corporation. All Rights Reserved.           
 ********************************************************************* */

#undef NDEBUG

// OSPRay
#include "device.h"
#include "coidevice_common.h"
#include "ospray/common/data.h"
// COI
#include "common/COIResult_common.h"
#include "source/COIEngine_source.h"
#include "source/COIEvent_source.h"
#include "source/COIProcess_source.h"
#include "source/COIBuffer_source.h"
#include "source/COIPipeline_source.h"
//std
#include <map>


#include "../fb/tilesize.h"


//#define FORCE_SINGLE_DEVICE 1

#define MAX_ENGINES 100

#define MANUAL_DATA_UPLOADS 1
#define UPLOAD_BUFFER_SIZE (2LL*1024*1024)

namespace ospray {
  namespace coi {

    // const char *coiWorker = "./ospray_coi_worker.mic";

    void coiError(COIRESULT result, const std::string &err);

    struct COIDevice;

    typedef enum { 
      OSPCOI_NEW_MODEL=0,
      OSPCOI_NEW_DATA,
      OSPCOI_NEW_TRIANGLEMESH,
      OSPCOI_COMMIT,
      OSPCOI_SET_VALUE,
      OSPCOI_NEW_MATERIAL,
      OSPCOI_SET_MATERIAL,
      OSPCOI_NEW_CAMERA,
      OSPCOI_NEW_VOLUME,
      OSPCOI_NEW_VOLUME_FROM_FILE,
      OSPCOI_NEW_TRANSFER_FUNCTION,
      OSPCOI_NEW_RENDERER,
      OSPCOI_NEW_GEOMETRY,
      OSPCOI_ADD_GEOMETRY,
      OSPCOI_NEW_FRAMEBUFFER,
      OSPCOI_RENDER_FRAME,
      OSPCOI_RENDER_FRAME_SYNC,
      OSPCOI_NEW_TEXTURE2D,
      OSPCOI_NEW_LIGHT,
      OSPCOI_REMOVE_GEOMETRY,
      OSPCOI_FRAMEBUFFER_CLEAR,
      OSPCOI_PRINT_CHECKSUMS,
      OSPCOI_PIN_UPLOAD_BUFFER,
      OSPCOI_CREATE_NEW_EMPTY_DATA,
      OSPCOI_UPLOAD_DATA_DONE,
      OSPCOI_UPLOAD_DATA_CHUNK,
      OSPCOI_NUM_FUNCTIONS
    } RemoteFctID;

    const char *coiFctName[] = {
      "ospray_coi_new_model",
      "ospray_coi_new_data",
      "ospray_coi_new_trianglemesh",
      "ospray_coi_commit",
      "ospray_coi_set_value",
      "ospray_coi_new_material",
      "ospray_coi_set_material",
      "ospray_coi_new_camera",
      "ospray_coi_new_volume",
      "ospray_coi_new_volume_from_file",
      "ospray_coi_new_transfer_function",
      "ospray_coi_new_renderer",
      "ospray_coi_new_geometry",
      "ospray_coi_add_geometry",
      "ospray_coi_new_framebuffer",
      "ospray_coi_render_frame",
      "ospray_coi_render_frame_sync",
      "ospray_coi_new_texture2d",
      "ospray_coi_new_light",
      "ospray_coi_remove_geometry",
      "ospray_coi_framebuffer_clear",
      "ospray_coi_print_checksums", // JUST FOR DEBUGGING!!!!
      "ospray_coi_pin_upload_buffer",
      "ospray_coi_create_new_empty_data",
      "ospray_coi_upload_data_done",
      "ospray_coi_upload_data_chunk",
      NULL
    };
    
    struct COIEngine {
#ifdef MANUAL_DATA_UPLOADS
      COIBUFFER uploadBuffer;
#endif
      COIENGINE       coiEngine;   // COI engine handle
      COIPIPELINE     coiPipe;     // COI pipeline handle
      COIPROCESS      coiProcess; 
      // COIDEVICE       coiDevice;   // COI device handle
      COI_ENGINE_INFO coiInfo;
      size_t          engineID;
      COIEVENT        event;
      
      COIDevice *osprayDevice;
      COIFUNCTION coiFctHandle[OSPCOI_NUM_FUNCTIONS];
      
      /*! create this engine, initialize coi with this engine ID, print
        basic device info */
      COIEngine(COIDevice *osprayDevice, size_t engineID);
      /*! load ospray worker onto device, and initialize basic ospray
        service */
      void loadOSPRay(); 
      // void callFunction(RemoteFctID ID, const DataStream &data, int *returnValue, bool sync);
    };

    struct COIDevice : public ospray::api::Device {
      std::vector<COIEngine *> engine;
      api::Handle handle;

      COIDevice();

      void callFunction(RemoteFctID ID, const DataStream &data, 
                        int *returnValue=NULL, 
                        bool sync=true)
      { 
        double t0 = getSysTime();
        // cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;
        // cout << "calling coi function " << coiFctName[ID] << endl;
        // cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;
#if 1
        static COIEVENT event[MAX_ENGINES]; //at most 100 engines...
        static long numEventsOutstanding = 0;
        assert(engine.size() < MAX_ENGINES);
        for (int i=0;i<engine.size();i++) {
          if (numEventsOutstanding == 0) {
            bzero(&event[i],sizeof(event[i]));
          }
          COIRESULT result = COIPipelineRunFunction(engine[i]->coiPipe,
                                                    engine[i]->coiFctHandle[ID],
                                                    0,NULL,NULL,//buffers
                                                    0,NULL,//dependencies
                                                    data.buf,data.ofs,//data
                                                    returnValue?returnValue:NULL,
                                                    returnValue?sizeof(int):0,
                                                    &event[i]);
          if (result != COI_SUCCESS) {
            coiError(result,"error in coipipelinerunfct");
          }
        }
        numEventsOutstanding++;
        if (sync || returnValue) {
          for (int i=0;i<engine.size();i++) {
            COIEventWait(1,&event[i],-1,1/*wait for all*/,NULL,NULL);
          }
          numEventsOutstanding = 0;
        }
#else
        for (int i=0;i<engine.size();i++) engine[i]->callFunction(ID,data,sync); 
#endif
        // double t1 = getSysTime();
        // static double sum_t = 0;
        // sum_t += (t1-t0);
        // static long lastPing = 0;
        // long numSecs = long(sum_t);
        // if (numSecs > lastPing)
        //   cout << "#osp:coi: time spent in callfunctions (general) " << sum_t << " secs" << endl;
        // lastPing = numSecs;
      }

      /*! create a new frame buffer */
      virtual OSPFrameBuffer frameBufferCreate(const vec2i &size, 
                                               const OSPFrameBufferFormat mode,
                                               const uint32 channels);


      /*! clear the specified channel(s) of the frame buffer specified in 'whichChannels'
        
        if whichChannel&OSP_FB_COLOR!=0, clear the color buffer to
        '0,0,0,0'.  

        if whichChannel&OSP_FB_DEPTH!=0, clear the depth buffer to
        +inf.  

        if whichChannel&OSP_FB_ACCUM!=0, clear the accum buffer to 0,0,0,0,
        and reset accumID.
      */
      virtual void frameBufferClear(OSPFrameBuffer _fb,
                                    const uint32 fbChannelFlags); 

      /*! map frame buffer */
      virtual const void *frameBufferMap(OSPFrameBuffer fb, 
                                         OSPFrameBufferChannel);

      /*! unmap previously mapped frame buffer */
      virtual void frameBufferUnmap(const void *mapped,
                                    OSPFrameBuffer fb);

      /*! create a new model */
      virtual OSPModel newModel();

      // /*! finalize a newly specified model */
      // virtual void finalizeModel(OSPModel _model) { NOTIMPLEMENTED; }

      /*! commit the given object's outstanding changes */
      virtual void commit(OSPObject object);
      /*! remove an existing geometry from a model */
      virtual void removeGeometry(OSPModel _model, OSPGeometry _geometry);

      /*! add a new geometry to a model */
      virtual void addGeometry(OSPModel _model, OSPGeometry _geometry);

      /*! create a new data buffer */
      virtual OSPData newData(size_t nitems, OSPDataType format, void *init, int flags);

      /*! load module */
      virtual int loadModule(const char *name);

      /*! assign (named) string parameter to an object */
      virtual void setString(OSPObject object, const char *bufName, const char *s);
      /*! assign (named) data item as a parameter to an object */
      virtual void setObject(OSPObject target, const char *bufName, OSPObject value);
      /*! assign (named) float parameter to an object */
      virtual void setFloat(OSPObject object, const char *bufName, const float f);
      /*! assign (named) vec3f parameter to an object */
      virtual void setVec3f(OSPObject object, const char *bufName, const vec3f &v);
      /*! assign (named) int parameter to an object */
      virtual void setInt(OSPObject object, const char *bufName, const int f);
      /*! assign (named) vec3i parameter to an object */
      virtual void setVec3i(OSPObject object, const char *bufName, const vec3i &v);
      /*! add untyped void pointer to object - this will *ONLY* work in local rendering!  */
      virtual void setVoidPtr(OSPObject object, const char *bufName, void *v) { NOTIMPLEMENTED; }

      /*! create a new triangle mesh geometry */
      virtual OSPTriangleMesh newTriangleMesh();

      /*! create a new renderer object (out of list of registered renderers) */
      virtual OSPRenderer newRenderer(const char *type);

      /*! create a new geometry object (out of list of registered geometrys) */
      virtual OSPGeometry newGeometry(const char *type);

      /*! create a new camera object (out of list of registered cameras) */
      virtual OSPCamera newCamera(const char *type);

      /*! create a new volume object (out of list of registered volumes) */
      virtual OSPVolume newVolume(const char *type);

      /*! create a new volume object (out of list of registered volume types) with data from a file */
      virtual OSPVolume newVolumeFromFile(const char *filename, const char *type);

      /*! create a new transfer function object (out of list of registered transfer function types) */
      virtual OSPTransferFunction newTransferFunction(const char *type);

      /*! have given renderer create a new Light */
      virtual OSPLight newLight(OSPRenderer _renderer, const char *type);

      /*! create a new Texture2D object */
      virtual OSPTexture2D newTexture2D(int width, int height, OSPDataType type, void *data, int flags);
      
      /*! call a renderer to render a frame buffer */
      virtual void renderFrame(OSPFrameBuffer _sc, 
                               OSPRenderer _renderer, 
                               const uint32 fbChannelFlags);

      //! release (i.e., reduce refcount of) given object
      /*! note that all objects in ospray are refcounted, so one cannot
        explicitly "delete" any object. instead, each object is created
        with a refcount of 1, and this refcount will be
        increased/decreased every time another object refers to this
        object resp releases its hold on it; if the refcount is 0 the
        object will automatically get deleted. For example, you can
        create a new material, assign it to a geometry, and immediately
        after this assignation release its refcount; the material will
        stay 'alive' as long as the given geometry requires it. */
      virtual void release(OSPObject _obj) {  }

      //! assign given material to given geometry
      virtual void setMaterial(OSPGeometry _geom, OSPMaterial _mat);
      /*! have given renderer create a new material */
      virtual OSPMaterial newMaterial(OSPRenderer _renderer, const char *type);
    };


    
    /*! create this engine, initialize coi with this engine ID, print
      basic device info */
    COIEngine::COIEngine(COIDevice *osprayDevice, size_t engineID)
      : osprayDevice(osprayDevice), engineID(engineID)
    {
      COIRESULT result;
      COI_ISA_TYPE isa = COI_ISA_MIC;      

      cout << "#osp:coi: engine #" << engineID << ": " << flush;
      result = COIEngineGetHandle(isa,engineID,&coiEngine);
      Assert(result == COI_SUCCESS);
      
      result = COIEngineGetInfo(coiEngine,sizeof(coiInfo),&coiInfo);
      Assert(result == COI_SUCCESS);
      
      cout << coiInfo.NumCores << " cores @ " << coiInfo.CoreMaxFrequency << "MHz, "
           << (coiInfo.PhysicalMemory/1000000000) << "GB memory" << endl;
    }

    /*! load ospray worker onto device, and initialize basic ospray
      service */
    void COIEngine::loadOSPRay()
    {
      COIRESULT result;
      const char *coiWorker = getenv("OSPRAY_COI_WORKER");
      if (coiWorker == NULL) {
        std::cerr << "OSPRAY_COI_WORKER not defined." << std::endl;
        std::cerr << "Note: In order to run the OSPray COI device on the Xeon Phis it needs to know the full path of the 'ospray_coi_worker.mic' executable that contains the respective ospray xeon worker binary. Please define an environment variabel named 'OSPRAY_COI_WORKER' to contain the filename - with full directory path - of this executable." << std::endl;
        exit(1);
      }
      const char *sinkLDPath = getenv("SINK_LD_LIBRARY_PATH");
      if (coiWorker == NULL) {
        std::cerr << "SINK_LD_LIBRARY_PATH not defined." << std::endl;
        std::cerr << "Note: In order for the COI version of OSPRay to find all the shared libraries (ospray, plus whatever modules the application way want to load) you have to specify the search path where COI is supposed to find those libraries on the HOST filesystem (it will then load them onto the device as required)." << std::endl;
        std::cerr << "Please define an environment variable named SINK_LD_LIBRARY_PATH that points to the directory containing the respective ospray mic libraries." << std::endl;
        exit(1);
      }
      result = COIProcessCreateFromFile(coiEngine,
                                        coiWorker,0,NULL,0,NULL,1/*proxy!*/,
                                        NULL,0,NULL,
                                        &coiProcess);
      if (result != COI_SUCCESS)
        coiError(result,"could not load worker binary");
      Assert(result == COI_SUCCESS);
      
      result = COIPipelineCreate(coiProcess,NULL,0,&coiPipe);
      if (result != COI_SUCCESS)
        coiError(result,"could not create command pipe");
      Assert(result == COI_SUCCESS);
      

      struct {
        int32 ID, count;
      } deviceInfo;
      deviceInfo.ID = engineID;
      deviceInfo.count = osprayDevice->engine.size();
      const char *fctName = "ospray_coi_initialize";
      COIFUNCTION fctHandle;
      result = COIProcessGetFunctionHandles(coiProcess,1,
                                            &fctName,&fctHandle);
      if (result != COI_SUCCESS)
        coiError(result,std::string("could not find function '")+fctName+"'");

      result = COIPipelineRunFunction(coiPipe,fctHandle,
                                      0,NULL,NULL,//buffers
                                      0,NULL,//dependencies
                                      &deviceInfo,sizeof(deviceInfo),//data
                                      NULL,0,
                                      NULL);
      Assert(result == COI_SUCCESS);


      result = COIProcessGetFunctionHandles(coiProcess,OSPCOI_NUM_FUNCTIONS,
                                            coiFctName,coiFctHandle);
      if (result != COI_SUCCESS)
        coiError(result,std::string("could not get coi api function handle(s) '"));
      

#ifdef MANUAL_DATA_UPLOADS
      size_t size = UPLOAD_BUFFER_SIZE;
      char *uploadBufferMem = new char[UPLOAD_BUFFER_SIZE];
      result = COIBufferCreate(size,COI_BUFFER_NORMAL,0,
                               uploadBufferMem,1,&coiProcess,&uploadBuffer);
      if (result != COI_SUCCESS) {
        cout << "error in allocating coi buffer : " << COIResultGetName(result) << endl;
        FATAL("error in allocating coi buffer");
      }
      {
        COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
        DataStream args;
        COIEVENT event;
        args.write((int)1);
        cout << "CREATING UPLOAD BUFFER" << endl;
        result = COIPipelineRunFunction(coiPipe,
                                        coiFctHandle[OSPCOI_PIN_UPLOAD_BUFFER],
                                        1,&uploadBuffer,&coiBufferFlags,//buffers
                                        0,NULL,//dependencies
                                        args.buf,args.ofs,//data
                                        NULL,0,
                                        &event);
        COIEventWait(1,&event,-1,1,NULL,NULL);
        if (result != COI_SUCCESS) {
          cout << "error in pinning coi upload buffer : " << COIResultGetName(result) << endl;
          FATAL("error in allocating coi buffer");
        }
      }
#endif
    }

    // void COIEngine::callFunction(RemoteFctID ID, const DataStream &data, bool sync)
    // {
    //   // double t0 = ospray::getSysTime();
    //   if (sync) {
    //     bzero(&event,sizeof(event));
    //     COIRESULT result = COIPipelineRunFunction(coiPipe,coiFctHandle[ID],
    //                                               0,NULL,NULL,//buffers
    //                                               0,NULL,//dependencies
    //                                               data.buf,data.ofs,//data
    //                                               NULL,0,
    //                                               &event);
    //     if (result != COI_SUCCESS) {
    //       coiError(result,"error in coipipelinerunfct");
    //     }
    //     COIEventWait(1,&event,-1,1,NULL,NULL);
    //   } else {
    //     bzero(&event,sizeof(event));
    //     COIRESULT result = COIPipelineRunFunction(coiPipe,coiFctHandle[ID],
    //                                               0,NULL,NULL,//buffers
    //                                               0,NULL,//dependencies
    //                                               data.buf,data.ofs,//data
    //                                               NULL,0,
    //                                               NULL); //&event);
    //     if (result != COI_SUCCESS) {
    //       coiError(result,"error in coipipelinerunfct");
    //       // COIEventWait(1,&event,-1,1,NULL,NULL);
    //     }
    //   }
    //   // double t1 = ospray::getSysTime();
    //   // cout << "fct " << ID << "@" << engineID << " : " << (t1-t0) << "s" << endl;
    // }

    
    void coiError(COIRESULT result, const std::string &err)
    {
      cerr << "=======================================================" << endl;
      cerr << "#osp:coi(fatal error): " << err << endl;
      if (result != COI_SUCCESS) 
        cerr << "#osp:coi: " << COIResultGetName(result) << endl << flush;
      cerr << "=======================================================" << endl;
      throw std::runtime_error("#osp:coi(fatal error):"+err);
    }
      
    ospray::api::Device *createCoiDevice(int *ac, const char **av)
    {
      cout << "=======================================================" << endl;
      cout << "#osp:mic: trying to create coi device" << endl;
      cout << "=======================================================" << endl;
      COIDevice *device = new COIDevice;
      return device;
    }

    COIDevice::COIDevice()
    {
      COIRESULT result;
      COI_ISA_TYPE isa = COI_ISA_MIC;      

      uint32_t numEngines;
      result = COIEngineGetCount(isa,&numEngines);
      if (numEngines == 0) {
        coiError(result,"no coi devices found");
      }

#if FORCE_SINGLE_DEVICE
      cout << "FORCING AT MOST ONE ENGINE" << endl;
      numEngines = 1;
#endif

      Assert(result == COI_SUCCESS);
      cout << "#osp:coi: found " << numEngines << " COI engines" << endl;
      Assert(numEngines > 0);

      char *maxEnginesFromEnv = getenv("OSPRAY_COI_MAX_ENGINES");
      if (maxEnginesFromEnv) {
        numEngines = std::min((int)numEngines,(int)atoi(maxEnginesFromEnv));
        cout << "#osp:coi: max engines after considering 'OSPRAY_COI_MAX_ENGINES' : " << numEngines << endl;
      }


      for (int i=0;i<numEngines;i++)
        engine.push_back(new COIEngine(this,i));

      cout << "#osp:coi: loading ospray onto COI devices..." << endl;
      for (int i=0;i<numEngines;i++)
        engine[i]->loadOSPRay();

      cout << "#osp:coi: all engines initialized and ready to run." << endl;
    }


    int COIDevice::loadModule(const char *name) 
    { 
      cout << "#osp:coi: loading module " << name << endl;
      // cout << "#osp:coi: loading module '" << name << "' not implemented... ignoring" << endl;
      DataStream args;
      args.write(name);

      std::string libName = "libospray_module_"+std::string(name)+"_mic.so";

      COIRESULT result;
      for (int i=0;i<engine.size();i++) {
        COILIBRARY coiLibrary;
        result = COIProcessLoadLibraryFromFile(engine[i]->coiProcess,
                                               libName.c_str(),
                                               NULL,NULL,
                                               // 0,
                                               &coiLibrary);
        if (result != COI_SUCCESS)
          coiError(result,"could not load device library "+libName);
        Assert(result == COI_SUCCESS);
      }
      return 0; 
    }

    /*! create a new triangle mesh geometry */
    OSPTriangleMesh COIDevice::newTriangleMesh()
    {
      Handle ID = Handle::alloc();
      DataStream args;
      args.write(ID);

      callFunction(OSPCOI_NEW_TRIANGLEMESH,args);

      return (OSPTriangleMesh)(int64)ID;
    }


    OSPModel COIDevice::newModel()
    {
      Handle ID = Handle::alloc();
      DataStream args;
      args.write(ID);
      callFunction(OSPCOI_NEW_MODEL,args);
      return (OSPModel)(int64)ID;
    }

    OSPData COIDevice::newData(size_t nitems, OSPDataType format, 
                               void *init, int flags)
    {
      COIRESULT result;
      DataStream args;
      Handle ID = Handle::alloc();

      if (nitems == 0) {
        throw std::runtime_error("cowardly refusing to create empty buffer...");
      }

      args.write(ID);
      args.write((int32)nitems);
      args.write((int32)format);
      args.write((int32)flags);

// #if 1
      double t0 = getSysTime();
      COIEVENT event[engine.size()];
      COIBUFFER coiBuffer[engine.size()];

      size_t size = nitems*ospray::sizeOf(format);

      // cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;
      // cout << "HOST: NEW DATA" << endl;
      // PRINT(nitems);
      // PRINT(format);
      // PRINT(nitems*ospray::sizeOf(format));
      cout << "checksum before uploading data" << computeCheckSum(init,nitems*ospray::sizeOf(format)) << endl;
      // cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;


#if MANUAL_DATA_UPLOADS
      callFunction(OSPCOI_CREATE_NEW_EMPTY_DATA,args);
      // PING; PRINT(init);
      for (size_t begin=0;begin<size;begin+=UPLOAD_BUFFER_SIZE) {
        size_t blockSize = std::min((ulong)UPLOAD_BUFFER_SIZE,(ulong)(size-begin));
        // PRINT(blockSize);
        char *beginPtr = ((char*)init)+begin;
        // cout << "host: first int64 of uploaded data: " << (int*)*(int*)beginPtr << endl;
        for (int i=0;i<engine.size();i++) {
          COIEVENT event;
          result = COIBufferWrite(engine[i]->uploadBuffer,
                                  0,beginPtr,blockSize,
                                  COI_COPY_USE_DMA,
                                  0,NULL,&event);
          if (result != COI_SUCCESS)
            cout << "error in allocating coi buffer : " << COIResultGetName(result) << endl;
          COIEventWait(1,&event,-1,1,NULL,NULL);
        }
        cout << "checksum of BLOCK before upload: " << computeCheckSum(beginPtr,blockSize) << endl;
    //         COIBUFFER           in_DestBuffer,
    //         uint64_t            in_Offset,
    // const   void*               in_pSourceData,
    //         uint64_t            in_Length,
    //         COI_COPY_TYPE       in_Type,
    //         uint32_t            in_NumDependencies,
    // const   COIEVENT*           in_pDependencies,
    //         COIEVENT*           out_pCompletion);
        
        DataStream args;
        args.write(ID);
        args.write((int64)begin);
        args.write((int64)blockSize);


        //        callFunction(OSPCOI_UPLOAD_DATA_CHUNK,args);
        cout << "callling osp_coi_data_chunk" << endl;
        for (int i=0;i<engine.size();i++) {
          COIEVENT event;
          bzero(&event,sizeof(event));
          COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
          result = COIPipelineRunFunction(engine[i]->coiPipe,
                                          engine[i]->coiFctHandle[OSPCOI_UPLOAD_DATA_CHUNK],
                                          1,&engine[i]->uploadBuffer,&coiBufferFlags,//buffers
                                          0,NULL,//dependencies
                                          args.buf,args.ofs,//data
                                          NULL,0,
                                          NULL); //&event);
          if (result != COI_SUCCESS)
            cout << "error in allocating coi buffer : " << COIResultGetName(result) << endl;
          // COIEventWait(1,&event,-1,1,NULL,NULL);
          // PING;
        }        
        Assert(result == COI_SUCCESS);
      }
      cout << "checksum of entire data on HOST " << computeCheckSum(init,size) << endl;
      callFunction(OSPCOI_UPLOAD_DATA_DONE,args);
#else
      for (int i=0;i<engine.size();i++) {
        // PRINT(nitems);
// #if 0
//         result = COIBufferCreate(nitems*ospray::sizeOf(format)+128,
//                                  COI_BUFFER_NORMAL,COI_MAP_WRITE_ENTIRE_BUFFER,
//                                  NULL,1,&engine[i]->coiProcess,&coiBuffer[i]);

//         size_t size = nitems*ospray::sizeOf(format);
//         size_t delta = 128*1024*1024;
//         for (size_t ofs=0;ofs<size;ofs+=delta) {
//           COIEVENT done;
//           bzero(&done,sizeof(done));
//           COIBufferWrite(coiBuffer[i],ofs,((char*)init)+ofs,std::min(size-ofs,delta),
//                          COI_COPY_USE_DMA,0,NULL,&done);
//           COIEventWait(1,&done,-1,1,NULL,NULL);
//         }
// #else
        {
          DataStream args;
          int i = 0;
          args.write(i);
          cout << "checksums BEFORE COIBufferCreate" << endl;
          callFunction(OSPCOI_PRINT_CHECKSUMS,args);
        }
        
        size_t size = nitems*ospray::sizeOf(format);



        result = COIBufferCreate(size,COI_BUFFER_NORMAL,
                                 size>128*1024*1024?COI_OPTIMIZE_HUGE_PAGE_SIZE:0,
                                 init,1,&engine[i]->coiProcess,&coiBuffer[i]);
        if (result != COI_SUCCESS) {
          cout << "error in allocating coi buffer : " << COIResultGetName(result) << endl;
          FATAL("error in allocating coi buffer");
        }

        {
          DataStream args;
          int i = 0;
          args.write(i);
          cout << "checksums AFTER COIBufferCreate (not passing the newly created buffer)" << endl;
          callFunction(OSPCOI_PRINT_CHECKSUMS,args);
        }
        {
          DataStream args;
          int i = 0;
          args.write(i);
          cout << "checksums AFTER COIBufferCreate (WITH newly created buffer)" << endl;
          bzero(&event[i],sizeof(event[i]));
          COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
          result = COIPipelineRunFunction(engine[i]->coiPipe,
                                          engine[i]->coiFctHandle[OSPCOI_PRINT_CHECKSUMS],
                                          1,&coiBuffer[i],&coiBufferFlags,//buffers
                                          0,NULL,//dependencies
                                          args.buf,args.ofs,//data
                                          NULL,0,
                                          &event[i]);
          COIEventWait(1,&event[i],-1,1,NULL,NULL);
        }

// #endif
        Assert(result == COI_SUCCESS);

        // if (init) {
        //   bzero(&event,sizeof(event));
        //   result = COIBufferWrite(coiBuffer,//engine[i]->coiProcess,
        //                           0,init,nitems*ospray::sizeOf(format),
        //                  COI_COPY_USE_DMA,0,NULL,COI_EVENT_ASYNC);
        //   Assert(result == COI_SUCCESS);
        // }
      }

      for (int i=0;i<engine.size();i++) {
        cout << "callling osp_coi_new_data" << endl;
        bzero(&event[i],sizeof(event[i]));
        COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
        result = COIPipelineRunFunction(engine[i]->coiPipe,
                                        engine[i]->coiFctHandle[OSPCOI_NEW_DATA],
                                        1,&coiBuffer[i],&coiBufferFlags,//buffers
                                        0,NULL,//dependencies
                                        args.buf,args.ofs,//data
                                        NULL,0,
                                        &event[i]);
        
        Assert(result == COI_SUCCESS);
      }
      for (int i=0;i<engine.size();i++) {
        COIEventWait(1,&event[i],-1,1,NULL,NULL);
      }
#endif
      double t1 = getSysTime();
      static double sum_t = 0;
      sum_t += (t1-t0);
      //      cout << "time spent in buffercreate " << (t1-t0) << " total " << sum_t << endl;
// #else
//       for (int i=0;i<engine.size();i++) {
//         COIBUFFER coiBuffer;
//         // PRINT(nitems);
//         result = COIBufferCreate(nitems*ospray::sizeOf(format),
//                                  COI_BUFFER_NORMAL,COI_OPTIMIZE_HUGE_PAGE_SIZE,//COI_MAP_READ_WRITE,
//                                  init,1,&engine[i]->coiProcess,&coiBuffer);
//         Assert(result == COI_SUCCESS);

//         COIEVENT event;
//         if (init) {
//           bzero(&event,sizeof(event));
//           result = COIBufferWrite(coiBuffer,//engine[i]->coiProcess,
//                                   0,init,nitems*ospray::sizeOf(format),
//                                   COI_COPY_USE_DMA,0,NULL,&event);
//           Assert(result == COI_SUCCESS);
//           COIEventWait(1,&event,-1,1,NULL,NULL);
//         }

//         bzero(&event,sizeof(event));
//         COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
//         result = COIPipelineRunFunction(engine[i]->coiPipe,
//                                         engine[i]->coiFctHandle[OSPCOI_NEW_DATA],
//                                         1,&coiBuffer,&coiBufferFlags,//buffers
//                                         0,NULL,//dependencies
//                                         args.buf,args.ofs,//data
//                                         NULL,0,
//                                         &event);
        
//         Assert(result == COI_SUCCESS);
//         COIEventWait(1,&event,-1,1,NULL,NULL);
//       }
// #endif
      return (OSPData)(int64)ID;
    }

    /*! call a renderer to render a frame buffer */
    void COIDevice::renderFrame(OSPFrameBuffer _sc, 
                                OSPRenderer _renderer, 
                                const uint32 fbChannelFlags)
    {
      DataStream args;
      args.write((Handle&)_sc);
      args.write((Handle&)_renderer);
      args.write((uint32&)fbChannelFlags);
#if FORCE_SINGLE_DEVICE
      callFunction(OSPCOI_RENDER_FRAME,args,NULL,true);
#else
      callFunction(OSPCOI_RENDER_FRAME,args,NULL,false);
      callFunction(OSPCOI_RENDER_FRAME_SYNC,args,NULL,true);
#endif
    }


    void COIDevice::commit(OSPObject obj)
    {
      Handle handle = (Handle &)obj;
      DataStream args;
      args.write(handle);
      callFunction(OSPCOI_COMMIT,args);
    }

    void COIDevice::removeGeometry(OSPModel _model, OSPGeometry _geometry)
    {
      DataStream args;
      args.write((Handle&)_model);
      args.write((Handle&)_geometry);
      callFunction(OSPCOI_REMOVE_GEOMETRY,args);
      // Model *model = (Model *)_model;
      // Assert2(model, "null model in LocalDevice::removeGeometry");

      // Geometry *geometry = (Geometry *)_geometry;
      // Assert2(model, "null geometry in LocalDevice::removeGeometry");

      // GeometryLocator locator;
      // locator.ptr = geometry;
      // Model::GeometryVector::iterator it = std::find_if(model->geometry.begin(), model->geometry.end(), locator);
      // if(it != model->geometry.end()) {
      //   model->geometry.erase(it);
      // }
    }


    /*! add a new geometry to a model */
    void COIDevice::addGeometry(OSPModel _model, OSPGeometry _geometry)
    {
      Handle handle = Handle::alloc();
      DataStream args;
      args.write((Handle&)_model);
      args.write((Handle&)_geometry);
      callFunction(OSPCOI_ADD_GEOMETRY,args);
    }


    //! assign given material to given geometry
    void COIDevice::setMaterial(OSPGeometry _geom, OSPMaterial _mat)
    {
      DataStream args;
      args.write((Handle&)_geom);
      args.write((Handle&)_mat);
      callFunction(OSPCOI_SET_MATERIAL,args);
    }

    /*! have given renderer create a new material */
    OSPMaterial COIDevice::newMaterial(OSPRenderer _renderer, const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(_renderer);
      args.write(type);
      int retValue = -1;
      callFunction(OSPCOI_NEW_MATERIAL,args,&retValue);
      if (retValue)
        // could create material ...
        return (OSPMaterial)(int64)handle;
      else {
        // could NOT create materail 
        handle.free();
        return (OSPMaterial)NULL;
      }
    }

    /*! create a new texture2D */
    OSPTexture2D COIDevice::newTexture2D(int width, int height, OSPDataType type, void *data, int flags)
    {
      COIRESULT result;
      DataStream args;
      Handle ID = Handle::alloc();

      if (width * height == 0) {
        throw std::runtime_error("cowardly refusing to create empty texture...");
      }

      args.write(ID);
      args.write((int32)width);
      args.write((int32)height);
      args.write((int32)type);
      args.write((int32)flags);
      int64 numBytes = sizeOf(type)*width*height;
      // double t0 = getSysTime();
      for (int i=0;i<engine.size();i++) {
        COIBUFFER coiBuffer;
        // PRINT(nitems);
        result = COIBufferCreate(numBytes,COI_BUFFER_NORMAL,COI_OPTIMIZE_HUGE_PAGE_SIZE,//COI_MAP_READ_ONLY,
                                 data,1,&engine[i]->coiProcess,&coiBuffer);
        Assert(result == COI_SUCCESS);
        COIEVENT event;
        bzero(&event,sizeof(event));
        COI_ACCESS_FLAGS coiBufferFlags = COI_SINK_READ;
        result = COIPipelineRunFunction(engine[i]->coiPipe,
                                        engine[i]->coiFctHandle[OSPCOI_NEW_TEXTURE2D],
                                        1,&coiBuffer,&coiBufferFlags,//buffers
                                        0,NULL,//dependencies
                                        args.buf,args.ofs,//data
                                        NULL,0,
                                        &event);
        Assert(result == COI_SUCCESS);
        COIEventWait(1,&event,-1,1,NULL,NULL);
      }
      // double t1 = getSysTime();
      // static double sum_t = 0;
      // sum_t += (t1-t0);
      //      cout << "time spent in createtexture2d " << (t1-t0) << " total " << sum_t << endl;
      return (OSPTexture2D)(int64)ID;
    }

    /*! have given renderer create a new light */
    OSPLight COIDevice::newLight(OSPRenderer _renderer, const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      
      args.write(handle);
      args.write((Handle&)_renderer);
      args.write(type);
      callFunction(OSPCOI_NEW_LIGHT,args);
      return (OSPLight)(int64)handle;
    }

    /*! create a new geometry object (out of list of registered geometrys) */
    OSPGeometry COIDevice::newGeometry(const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(type);
      callFunction(OSPCOI_NEW_GEOMETRY,args);
      return (OSPGeometry)(int64)handle;
    }

    /*! create a new camera object (out of list of registered cameras) */
    OSPCamera COIDevice::newCamera(const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(type);
      callFunction(OSPCOI_NEW_CAMERA,args);
      return (OSPCamera)(int64)handle;
    }

    /*! create a new volume object (out of list of registered volumes) */
    OSPVolume COIDevice::newVolume(const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(type);
      callFunction(OSPCOI_NEW_VOLUME,args);
      return (OSPVolume)(int64)handle;
    }

    /*! create a new volume object (out of list of registered volume types) with data from a file */
    OSPVolume COIDevice::newVolumeFromFile(const char *filename, const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(filename);
      args.write(type);
      callFunction(OSPCOI_NEW_VOLUME_FROM_FILE, args);
      return (OSPVolume)(int64) handle;
    }

    /*! create a new transfer function object (out of list of registered transfer function types) */
    OSPTransferFunction COIDevice::newTransferFunction(const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(type);
      callFunction(OSPCOI_NEW_TRANSFER_FUNCTION, args);
      return (OSPTransferFunction)(int64) handle;
    }

    /*! create a new renderer object (out of list of registered renderers) */
    OSPRenderer COIDevice::newRenderer(const char *type)
    {
      Assert(type);
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(type);
      callFunction(OSPCOI_NEW_RENDERER,args);
      return (OSPRenderer)(int64)handle;
    }

    struct COIFrameBuffer {
      COIBUFFER *coiBuffer; // one per engine
      void *hostMem;
      vec2i size;
    };

    std::map<int64,COIFrameBuffer *> fbList;

    /*! create a new frame buffer */
    OSPFrameBuffer COIDevice::frameBufferCreate(const vec2i &size, 
                                                const OSPFrameBufferFormat mode,
                                                const uint32 channels)
    {
      COIRESULT result;
      Handle handle = Handle::alloc();
      DataStream args;
      args.write(handle);
      args.write(size);
      args.write((uint32)mode);
      args.write(channels);

      Assert(mode == OSP_RGBA_I8);
      COIFrameBuffer *fb = new COIFrameBuffer;
      fbList[handle] = fb;
      fb->hostMem = new int32[size.x*size.y];
      fb->coiBuffer = new COIBUFFER[engine.size()];
      fb->size = size;
      for (int i=0;i<engine.size();i++) {
        result = COIBufferCreate(size.x*size.y*sizeof(int32),
                                 COI_BUFFER_NORMAL,COI_OPTIMIZE_HUGE_PAGE_SIZE,//COI_MAP_READ_WRITE,
                                 NULL,1,&engine[i]->coiProcess,&fb->coiBuffer[i]);
        Assert(result == COI_SUCCESS);
        
        COIEVENT event; bzero(&event,sizeof(event));
        COI_ACCESS_FLAGS coiBufferFlags = 
          (COI_ACCESS_FLAGS)((int)COI_SINK_READ 
                             | (int)COI_SINK_WRITE);
        result = COIPipelineRunFunction(engine[i]->coiPipe,
                                        engine[i]->coiFctHandle[OSPCOI_NEW_FRAMEBUFFER],
                                        1,&fb->coiBuffer[i],
                                        &coiBufferFlags,//buffers
                                        0,NULL,//dependencies
                                        args.buf,args.ofs,//data
                                        NULL,0,
                                        &event);
        Assert(result == COI_SUCCESS);
        COIEventWait(1,&event,-1,1,NULL,NULL);
      }
      return (OSPFrameBuffer)(int64)handle;
    }

    /*! map frame buffer */
    const void *COIDevice::frameBufferMap(OSPFrameBuffer _fb,
                                          OSPFrameBufferChannel channel)
    {
      if (channel != OSP_FB_COLOR)
        throw std::runtime_error("can only map color buffers on coi devices");
      COIRESULT result;
      Handle handle = (Handle &)_fb;
      COIFrameBuffer *fb = fbList[handle];//(COIFrameBuffer *)_fb;
      
#if FORCE_SINGLE_DEVICE
      COIEVENT event; bzero(&event,sizeof(event));
      // double t0 = ospray::getSysTime();
      result = COIBufferRead(fb->coiBuffer[0],0,fb->hostMem,
                             fb->size.x*fb->size.y*sizeof(int32),
                             COI_COPY_USE_DMA,0,NULL,&event);
      Assert(result == COI_SUCCESS);
      COIEventWait(1,&event,-1,1,NULL,NULL);
      // double t1 = ospray::getSysTime();
      // double t_read_buffer = t1 - t0;
      // PRINT(t_read_buffer);
#else
      const int numEngines = engine.size();
      int32 *devBuffer[numEngines];
      COIEVENT doneCopy[numEngines];
      // -------------------------------------------------------
      // trigger N copies...
      // -------------------------------------------------------
      for (int i=0;i<numEngines;i++) {
        bzero(&doneCopy[i],sizeof(COIEVENT));
        devBuffer[i] = new int32[fb->size.x*fb->size.y];
        result = COIBufferRead(fb->coiBuffer[i],0,devBuffer[i],
                               fb->size.x*fb->size.y*sizeof(int32),
                               COI_COPY_USE_DMA,0,NULL,&doneCopy[i]);
        Assert(result == COI_SUCCESS);
      }
      // -------------------------------------------------------
      // do 50 assemblies...
      // -------------------------------------------------------
      for (int engineID=0;engineID<numEngines;engineID++) {
        const size_t sizeX = fb->size.x;
        const size_t sizeY = fb->size.y;
        COIEventWait(1,&doneCopy[engineID],-1,1,NULL,NULL);
        uint32 *src = (uint32*)devBuffer[engineID];
        uint32 *dst = (uint32*)fb->hostMem;

        const size_t numTilesX = divRoundUp(sizeX,TILE_SIZE);
        const size_t numTilesY = divRoundUp(sizeY,TILE_SIZE);
// #pragma omp parallel for
        for (size_t tileY=0;tileY<numTilesY;tileY++) {
// #pragma omp parallel for
          for (size_t tileX=0;tileX<numTilesX;tileX++) {
            const size_t tileID = tileX+numTilesX*tileY;
            if (engineID != (tileID % numEngines)) 
              continue;
            const size_t x0 = tileX*TILE_SIZE;            
            const size_t x1 = std::min(x0+TILE_SIZE,sizeX);
            const size_t y0 = tileY*TILE_SIZE;            
            const size_t y1 = std::min(y0+TILE_SIZE,sizeY);
            for (size_t y=y0;y<y1;y++)
              for (size_t x=x0;x<x1;x++) {
                const size_t idx = x+y*sizeX;
                dst[idx] = src[idx];
              }
          }
        }

        delete[] devBuffer[engineID];
      }
#endif
      return fb->hostMem;
    }

    /*! unmap previously mapped frame buffer */
    void COIDevice::frameBufferUnmap(const void *mapped,
                                     OSPFrameBuffer fb)
    {
    }



    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setObject(OSPObject target, const char *bufName, OSPObject value)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_OBJECT);
      args.write((Handle&)value);
      callFunction(OSPCOI_SET_VALUE,args);
    }

    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setString(OSPObject target, const char *bufName, const char *s)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_STRING);
      args.write(s);
      callFunction(OSPCOI_SET_VALUE,args);
    }
    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setFloat(OSPObject target, const char *bufName, const float f)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_float);
      args.write(f);
      callFunction(OSPCOI_SET_VALUE,args);
    }
    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setInt(OSPObject target, const char *bufName, const int32 i)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_int32);
      args.write(i);
      callFunction(OSPCOI_SET_VALUE,args);
    }
    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setVec3f(OSPObject target, const char *bufName, const vec3f &v)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_vec3f);
      args.write(v);
      callFunction(OSPCOI_SET_VALUE,args);
    }
    /*! assign (named) data item as a parameter to an object */
    void COIDevice::setVec3i(OSPObject target, const char *bufName, const vec3i &v)
    {
      Assert(bufName);

      DataStream args;
      args.write((Handle&)target);
      args.write(bufName);
      args.write(OSP_vec3i);
      args.write(v);
      callFunction(OSPCOI_SET_VALUE,args);
    }
    /*! clear the specified channel(s) of the frame buffer specified in 'whichChannels'
        
      if whichChannel&OSP_FB_COLOR!=0, clear the color buffer to
      '0,0,0,0'.  

      if whichChannel&OSP_FB_DEPTH!=0, clear the depth buffer to
      +inf.  

      if whichChannel&OSP_FB_ACCUM!=0, clear the accum buffer to 0,0,0,0,
      and reset accumID.
    */
    void COIDevice::frameBufferClear(OSPFrameBuffer _fb,
                                     const uint32 fbChannelFlags)
    {
      DataStream args;
      args.write((Handle&)_fb);
      args.write(fbChannelFlags);
      callFunction(OSPCOI_FRAMEBUFFER_CLEAR,args);
    }
  }
}


