#include "stdafx.h"
#include "SC2_Class.h"
#include "stdio.h"
#include "SC2_Demo.h"
#include "resource.h"


UINT CamTimer(LPVOID pParam)           // Worker thread which calls a CSC2Class function.
{
  CSC2Class* pObject = (CSC2Class*)pParam;
  
  if (pObject == NULL)                 // pObject is the camera class pointer
    return 1;

  while (pObject->m_bThreadRun)        // Thread is alive
  {
    pObject->SC2Thread();
  }
  return 0;
}


// Constructor
CSC2Class::CSC2Class()
{
  m_iColorCam = -1;                     //-1 means unknown
  m_bThreadRun = FALSE;
  m_wthWorker = NULL;
  m_bPlay = FALSE;
  
  hLutDialog = NULL;             //-1 means no Convert Dialog is open
  m_bConnectionLost = false;
  
  m_myHdc = NULL;                      // initialize Pointers to NULL
  hBWConvert = NULL;                   // Handle of BW LUT
  hColConvert = NULL;                  // Handle of Color LUT
  m_hSC2Dlg = NULL;                    // handle to camera dialog
  m_hBmp = NULL;                       // pointer to memory bitmap
  
  m_pic12 = NULL;
  m_pic8 = NULL;
  
  m_wXResAct = 0;                      // initialize resolution to 0 * 0
  m_wYResAct = 0;

  m_bFirstArm = FALSE;
  
  strGeneral.wSize = sizeof(strGeneral);// initialize all structure size members
  strGeneral.strCamType.wSize = sizeof(strGeneral.strCamType);
  strCamType.wSize = sizeof(strCamType);
  strSensor.wSize = sizeof(strSensor);
  strSensor.strDescription.wSize = sizeof(strSensor.strDescription);
  strDescription.wSize = sizeof(strDescription);
  strTiming.wSize = sizeof(strTiming);
  strStorage.wSize = sizeof(strStorage);
  strRecording.wSize = sizeof(strRecording);
  strImage.wSize = sizeof(strImage);
  strImage.strSegment[0].wSize = sizeof(strImage.strSegment[0]);
  strImage.strSegment[1].wSize = sizeof(strImage.strSegment[0]);
  strImage.strSegment[2].wSize = sizeof(strImage.strSegment[0]);
  strImage.strSegment[3].wSize = sizeof(strImage.strSegment[0]);

  m_hMutex = CreateMutex( 
        NULL,              // default security attributes
        FALSE,             // initially not owned
        NULL);             // unnamed mutex
}

// Destructor
CSC2Class::~CSC2Class()
{
}

//  FUNCTION: SC2Thread(LPVOID param)
//
//  PURPOSE: gets preview images from camera, converts them using the appropriate LUTs and displays them
//  CALLER:  none, startet by the application in StartRecord, StartPlay

DWORD CSC2Class::SC2Thread()
{
  int ierr = 0;
  int ierrorcnt = 0;
  WORD wimage = 0;

  while (m_bThreadRun)
  {
    DWORD dwStatusDll, dwStatusDrv;
    if (!m_bPlay)                      // Play or Record?
      wimage = 0;                      // Record reads out most recent image.
    else
    {
      wimage++;                        // Play reads out recorded images.
      if (wimage > m_dwValidImageCnt)
        wimage = 1;
    }
    ierr = PCO_AddBuffer(hCam, wimage, wimage, 0);// Add a buffer to the driver queue (preview)
    if (ierr != PCO_NOERROR)
    {
      ::PostMessage(m_hwndMain, WM_APP+101, 0, ierr);
      break;
    }
    ierr = WaitForSingleObject(m_hEvent, 2000);// Wait until picture arrives (adapt it to your needs).
    switch (ierr)
    {
      case WAIT_OBJECT_0:
        ierr = PCO_GetBufferStatus(hCam, 0, &dwStatusDll, &dwStatusDrv);
        if (ierr != PCO_NOERROR)
        {
          ::PostMessage(m_hwndMain, WM_APP+101, 0, ierr);
          break;
        }
        if(dwStatusDrv != 0)
          ierr = PCO_ERROR_TIMEOUT | PCO_ERROR_APPLICATION;
        else
          ierr = 0;

        if (ierr != PCO_NOERROR)
        {
          ::PostMessage(m_hwndMain, WM_APP+101, 0, ierr);
          break;
        }

        break;
      
      case WAIT_TIMEOUT:
      case WAIT_FAILED:
      default:
        ierrorcnt++;
        if(ierrorcnt >= 5)
          m_bThreadRun = FALSE;
        ierr = PCO_ERROR_TIMEOUT | PCO_ERROR_APPLICATION;
        break;
    }
    ResetEvent(m_hEvent);              // Prepare event for next image.
    Convert();                         // Convert image got.
  }
  m_bThreadRun = false;
  ::PostMessage(m_hwndMain, WM_APP+101, 0, ierr);// Tell app: We are down.
  return 0;
}

//  FUNCTION: Convert()
//
//  PURPOSE: converts images from 16bit to color or bw bitmap.
//  CALLER: SC2Thread, ClearBuffers, AutoGain
int CSC2Class::Convert()
{
  int iRetCode = PCO_NOERROR;

  DWORD dwWaitResult = WaitForSingleObject( 
            m_hMutex,    // handle to mutex
            2000);  // no time-out interval
 
  if((dwWaitResult == WAIT_ABANDONED) || (dwWaitResult == STATUS_TIMEOUT))
    return PCO_ERROR_TIMEOUT | PCO_ERROR_APPLICATION;

  if (m_pic12 && m_pic8)
  {
    int imode = 0;
    if (m_iColorCam == 1 && hColConvert)// It's a color camera
    {
      int imosaiker[4] = {BAYER_UPPER_LEFT_IS_RED, BAYER_UPPER_LEFT_IS_GREEN_RED,
                      BAYER_UPPER_LEFT_IS_GREEN_BLUE, BAYER_UPPER_LEFT_IS_BLUE};
      //int imosaiker[4] = {0, 0x01000000, 0x02000000, 0x03000000};
      int imosaik;
      
      if ((m_iColorPatternType > 3) ||(m_iColorPatternType < 0))
        m_iColorPatternType = 0;
      imosaik = imosaiker[m_iColorPatternType];
      try
      {
        iRetCode = PCO_Convert16TOCOL(hColConvert, imode, imosaik, m_wXResAct, m_wYResAct, m_pic12, m_pic8);
        // Convert raw data using the color Lookup table

      }
      catch (...)
      {
      }
    }
    else if (hBWConvert)                 // It's a BW camera
    {
      try
      {
        iRetCode = PCO_Convert16TO8(hBWConvert, imode, 0, m_wXResAct, m_wYResAct, m_pic12, m_pic8);
        // Convert raw data using the BW Lookup table
      }
      catch (...)
      {
      }
    }
    if (hLutDialog != NULL)
      PCO_SetDataToDialog(hLutDialog, m_wXResAct, m_wYResAct, m_pic12, m_pic8);
    ::PostMessage(m_hwndMain, WM_APP+100, 0, 0);
  }
  ReleaseMutex(m_hMutex);
  return iRetCode;
}

// This method is called if connection to the camera is lost
void CSC2Class::Disconnected(int iline)
{
  CString csh;

  csh.Format("Error in line %d. Execution terminated. Please restart (and/or debug).", iline);
  AfxMessageBox((LPCTSTR)csh, MB_OK);
  if (m_bConnectionLost)
    return;  // Make sure that method is only called once
  
  m_bConnectionLost = true;
  m_wXResAct = 0;        // reset resolutions
  m_wYResAct = 0;
  CloseCamDialog();      // close any open dialogs
  CloseConvertDialog();
  StopPlay();          // Stop playback if it runs
  StopRecord();        // Stop record it it runs
}

// This method is called if a camera is found
void CSC2Class::Connected()
{
  m_bConnectionLost = false;  // Just allow 'Disconnected()' to be called again
}

//  FUNCTION: GetDemosaickingMode(int wroix0, int wroiy0)
//
//  PURPOSE: determines the color conversion mode.
//  CALLER: PrepareBuffers
int CSC2Class::GetDemosaickingMode(int wroix0, int wroiy0)
{
  int im;
  int imodes[4][2][2] = 
  {
    3, 2, 1, 0, // upper left is red
    2, 3, 0, 1, // upper left is green in red line
    1, 0, 3, 2, // upper left is green in blue line
    0, 1, 2, 3
  };// upper left is blue
  
  im = strSensor.strDescription.wColorPatternDESC & 0x0F;
  if ((im < 1) ||(im > 4))
    im = 1;
  im--;                           // zero based index!
  im = imodes[im][wroiy0 & 0x01][wroix0 & 0x01];
  return im;
}

// Opens BW or Color dialog
void CSC2Class::OpenConvertDialog()
{
  int hwnd = NULL;                     // The hwnd of the dialog will be written in here
  int status = 0;                      // The status of dialog creation will be written in here

  if ((m_wXResAct == 0) &&(m_wYResAct == 0))// No conversion happened up to now, so do not open dialog.
    return;

  if (m_iColorCam == 1)
  {
    if (hLutDialog != NULL)
      PCO_GetStatusConvertDialog(hLutDialog, &hwnd, &status);
    if (!hwnd)
      PCO_OpenConvertDialog(&hLutDialog, m_hwndMain, "Color Convert", WM_APP + 102, hColConvert, 0, 0);
                                       // Opens the Colour Convert Dialog
  }
  else if (m_iColorCam == 0)
  {
    if (hLutDialog != NULL)
      PCO_GetStatusConvertDialog(hLutDialog, &hwnd, &status);
    if (!hwnd)
      PCO_OpenConvertDialog(&hLutDialog, m_hwndMain, "BW Convert",  WM_APP + 102, hBWConvert, 0, 0);
                                       // Opens the BW Convert Dialog
  }
  if(hLutDialog)
    PCO_SetDataToDialog(hLutDialog, m_wXResAct, m_wYResAct, m_pic12, m_pic8);
}

// returns the ID of an open dialog
int CSC2Class::GetDialog(bool bconv)
{
  if(bconv)
  {
    int err = PCO_NOERROR;
    int hwnd = NULL;                     // The hwnd of the dialog will be written in here
    int status = 0;                      // The status of dialog creation will be written in here

    if (hLutDialog != NULL)
      err = PCO_GetStatusConvertDialog(hLutDialog, &hwnd, &status);// Check if Dialog is already open
    if(hwnd == NULL)
      hLutDialog = NULL;
    if(hLutDialog == NULL)
      return -1;
    return 0;
  }

  if (m_hSC2Dlg)
  {
    DWORD dwStatus = 0;
    int err = PCO_GetStatusDialogCam(m_hSC2Dlg, &dwStatus);

    if(dwStatus == 0)
    {
      m_hSC2Dlg = NULL;
      return -1;
    }
    else
      return 0;
  }
  return -1;
}

// Closes any open convert dialog
void CSC2Class::CloseConvertDialog()
{
   if (hLutDialog >= 0)
   {
     PCO_CloseConvertDialog(hLutDialog);// Close Colour Convert Dialog
     hLutDialog = NULL;
   }
}

// opens dialog for camera control
void CSC2Class::OpenCamDialog()
{
  POINT topleft;
  topleft.x = 0;
  topleft.y = 0;
  ClientToScreen(m_hwndMain, &topleft);// Get position of upper left corner of the client area
                                       // so that dialog window can be positioned there.
  int iRetCode = PCO_OpenDialogCam(&m_hSC2Dlg, hCam, m_hwndMain, 0, 0, 0, topleft.x, topleft.y, "SC2 Camera Dialog");
  if (iRetCode != PCO_NOERROR)
    Disconnected(__LINE__);
}

// closes the camera dialog
void CSC2Class::CloseCamDialog()
{
  if (m_hSC2Dlg)
  {
    int iRetCode = PCO_CloseDialogCam(m_hSC2Dlg);
    m_hSC2Dlg = NULL;
    if (iRetCode != PCO_NOERROR)
      Disconnected(__LINE__);
  }
}

// returns the color resolution in bits
int CSC2Class::GetBitPix()
{
  return strDescription.wDynResDESC;
}

// Opens and initializes camera. Also checks which camera (Color/BW) is present.
int CSC2Class::OpenCamera(HWND hwnd)
{
  int err = PCO_NOERROR;

  if (hwnd)
  {  
    m_hwndMain = hwnd;                 // store handle
    m_myHdc = GetDC(hwnd);             // and Device Context
  }
  err = GetLastError();

  if(++err > 1)
    err = GetLastError();
  if(err > 3)
    hCam = NULL;
  int iRetCode = PCO_OpenCamera(&hCam, 0);// Try to open camera
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  else                                 // if successfull...
  {
    iRetCode = PCO_GetGeneral(hCam, &strGeneral);// Get infos from camera
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    iRetCode = PCO_GetCameraType(hCam, &strCamType);
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    iRetCode = PCO_GetSensorStruct(hCam, &strSensor);
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    iRetCode = PCO_GetCameraDescription(hCam, &strDescription);
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    iRetCode = PCO_GetTimingStruct(hCam, &strTiming);
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    iRetCode = PCO_GetRecordingStruct(hCam, &strRecording);
    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    WORD wMetaDataSize, wMetaDataVersion;
    PCO_SensorInfo strSensorInfo;

    strSensorInfo.wSize = sizeof(PCO_SensorInfo);
    strSensorInfo.hCamera = hCam;
    strSensorInfo.iCamNum = 0;
    strSensorInfo.iConversionFactor = strSensor.strDescription.wConvFactDESC[0];
    strSensorInfo.iDarkOffset = 30;//strSensor.strDescription.?;
    strSensorInfo.iDataBits = strSensor.strDescription.wDynResDESC;
    strSensorInfo.iSensorInfoBits = CONVERT_SENSOR_UPPERALIGNED;// Input data is upper aligned
    strSensorInfo.strColorCoeff.da11 = 1.0;
    strSensorInfo.strColorCoeff.da12 = 0.0;
    strSensorInfo.strColorCoeff.da13 = 0.0;
    strSensorInfo.strColorCoeff.da21 = 0.0;
    strSensorInfo.strColorCoeff.da22 = 1.0;
    strSensorInfo.strColorCoeff.da23 = 0.0;
    strSensorInfo.strColorCoeff.da31 = 0.0;
    strSensorInfo.strColorCoeff.da32 = 0.0;
    strSensorInfo.strColorCoeff.da33 = 1.0;

    PCO_SetMetaDataMode(hCam, (WORD)1, &wMetaDataSize, &wMetaDataVersion);

    if (strDescription.wSensorTypeDESC & 1)// if bit0 is 1, then sensor is a color sensor
    {

      m_iColorCam = 1;
      hColConvert = NULL;
      strSensorInfo.iSensorInfoBits |= CONVERT_SENSOR_COLORIMAGE; // Input data is a color image (see Bayer struct)

      err = PCO_ConvertCreate(&hColConvert, (PCO_SensorInfo*)&strSensorInfo.wSize, PCO_COLOR_CONVERT);
      // Create Lookup table with values 0 - 255 for each colour
      // for 14 bit input left aligned data
      //CONVERT_SET_EX(&m_pColorlut->red, 0, (1 << GetBitPix()) - 1, 0, 1.);// Write standard values into LUT (red)
    }
    else
    {
      m_iColorCam = 0;

      hColConvert = NULL;
      strSensorInfo.wSize = sizeof(PCO_SensorInfo);
      err = PCO_ConvertCreate(&hBWConvert, (PCO_SensorInfo*)&strSensorInfo.wSize, PCO_BW_CONVERT);
      // for 14 bit input left aligned data
      //CONVERT_SET_EX(m_pBwlut, 0, (1 << GetBitPix()) - 1, 0, 1.);// Write standard values into LUT
    }
    SetBWPalette();
    StopRecord();
  } 
  return iRetCode;
}

void CSC2Class::SetBWPalette()
{
  short i;
  int x;
  LOGPALETTE   *pPal;
  
  if ((HPALETTE)m_myhpal == NULL)
  {
    x = ::GetDeviceCaps(m_myHdc, SIZEPALETTE);
    
    if (x==-1)
      return;
    
    pPal = (LOGPALETTE *) new BYTE[sizeof(LOGPALETTE) +((256) * sizeof(PALETTEENTRY))];
    pPal->palNumEntries = (word)(256);
    pPal->palVersion    = 0x0300;
    
    // BW
    for (i = 0; i < 256; i++)        // set values in Palette-Buffer
    {
      pPal->palPalEntry[i].peBlue  = (unsigned char)(i);
      pPal->palPalEntry[i].peGreen = (unsigned char)(i);
      pPal->palPalEntry[i].peRed   = (unsigned char)(i);
      pPal->palPalEntry[i].peFlags =  0;
    }
    
    
    
    m_myhpal.CreatePalette(pPal);
    delete[](BYTE *)pPal;
  }
  m_oldhpal = ::SelectPalette(m_myHdc, (HPALETTE)m_myhpal, FALSE);
  ::RealizePalette(m_myHdc);
}

// prepares input buffers for receiving images
void CSC2Class::PrepareBuffers()
{
  if (m_hBmp)
    DeleteObject(m_hBmp);              // Delete the memory Bitmap if exists
  m_hBmp = NULL;
  
  if (m_pic12)
    PCO_FreeBuffer(hCam, 0);           // Frees the memory that was allocated for the buffer
  
  m_wBufferNr = -1;
  m_hEvent = NULL;
  m_pic12 = NULL;
  
  int iRetCode = PCO_AllocateBuffer(hCam, (SHORT*)&m_wBufferNr, m_wXResMax * m_wYResMax * sizeof(WORD), &m_pic12, &m_hEvent);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return;
  }
  
                                       // Allocate a new buffer
  iRetCode = PCO_CamLinkSetImageParameters(hCam, m_wXResAct, m_wYResAct);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return;
  }
  
  if (m_iColorCam == 1)                // create a memory bitmap with 24 bit RGB colors
  {
    struct
    {
      BITMAPINFOHEADER bih;
      unsigned long Colors[256];
    }
    bilut;                             // current BITMAPINFO with LUT values
    
    bilut.bih.biSize          = sizeof(BITMAPINFOHEADER);
    bilut.bih.biWidth         = m_wXResAct;
    bilut.bih.biHeight        = -m_wYResAct;
    bilut.bih.biPlanes        = 1;
    bilut.bih.biBitCount      = 24;
    bilut.bih.biCompression   = BI_RGB;
    bilut.bih.biSizeImage     = m_wXResAct*m_wYResAct;
    bilut.bih.biXPelsPerMeter = 0;
    bilut.bih.biYPelsPerMeter = 0;
    bilut.bih.biClrUsed       = 0;
    bilut.bih.biClrImportant  = 0;
    
    m_hBmp = CreateDIBSection(m_myHdc, (BITMAPINFO *)&bilut.bih, // Create a memory Bitmap
      DIB_RGB_COLORS,
      (void **)&m_pic8,
      NULL, 0);
  }
  else                                // create a memory bitmap with 8 bit grey values
  {
    struct
    {
      BITMAPINFOHEADER bih;
      unsigned short Colors[256];
    }
    bilut;                            // current BITMAPINFO with LUT values
    bilut.bih.biSize          = sizeof(BITMAPINFOHEADER);
    bilut.bih.biWidth         = m_wXResAct;
    bilut.bih.biHeight        = -m_wYResAct;
    bilut.bih.biPlanes        = 1;
    bilut.bih.biBitCount      = 8;
    bilut.bih.biCompression   = BI_RGB;
    bilut.bih.biSizeImage     = m_wXResAct*m_wYResAct;
    bilut.bih.biXPelsPerMeter = 0;
    bilut.bih.biYPelsPerMeter = 0;
    bilut.bih.biClrUsed       = 0;
    bilut.bih.biClrImportant  = 0;
    for (int q = 0; q < 256; q++)
      bilut.Colors[q] = (short)(q);
    
    m_hBmp = CreateDIBSection(m_myHdc, (BITMAPINFO *)&bilut.bih, // Create a memory Bitmap
      DIB_PAL_COLORS,
      (void **)&m_pic8,
      NULL, 0);
  }
  if (strDescription.wSensorTypeDESC & 1)// if bit0 is 1, then sensor is a color sensor
  {
    if (!m_bPlay)
      m_iColorPatternType = GetDemosaickingMode(strSensor.wRoiX0,
      strSensor.wRoiY0);
    else
      m_iColorPatternType = GetDemosaickingMode(strImage.strSegment[strStorage.wActSeg - 1].wRoiX0,
      strImage.strSegment[strStorage.wActSeg - 1].wRoiY0);
  }
  else
    m_iColorPatternType = 0;
  if (hLutDialog != NULL)
    PCO_SetDataToDialog(hLutDialog, m_wXResAct, m_wYResAct, m_pic12, m_pic8);
}

// start recording
int CSC2Class::StartRecord()
{
  if (m_bConnectionLost)
    return -1;

  int iRetCode = PCO_SetRecordingState(hCam, 0);// stop recording
  if (iRetCode & PCO_WARNING_FIRMWARE_FUNC_ALREADY_OFF)
    iRetCode = PCO_NOERROR;
  m_bPlay = FALSE;
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  
  iRetCode = PCO_GetStorageStruct(hCam, &strStorage);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  
  iRetCode = PCO_GetSensorStruct(hCam, &strSensor);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  iRetCode = PCO_GetSizes(hCam, &m_wXResAct, &m_wYResAct, &m_wXResMax, &m_wYResMax);
                                      // Get actual resolution of ROI
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }

  // PCO_CamLinkSetImageParameters is mandatory for Cameralink, GigE and USB
  iRetCode = PCO_CamLinkSetImageParameters(hCam, m_wXResAct, m_wYResAct);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }

  m_bPlay = FALSE;
  PrepareBuffers();                   // Prepare receiving buffers and memory bitmap
  iRetCode = PCO_ClearRamSegment(hCam);// Clear all previous recorded pictures in actual segment
  if((iRetCode & (PCO_ERROR_IS_ERROR | PCO_ERROR_LAYER_MASK | PCO_ERROR_CODE_MASK)) == PCO_ERROR_FIRMWARE_NOT_SUPPORTED)
    iRetCode = PCO_NOERROR;

  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }

#if defined DO_THE_EDGE
  if(strCamType.wCamType == CAMERATYPE_PCO_EDGE)
  {
    bool bnormal = TRUE;

    if((strSensor.dwPixelRate > strSensor.strDescription.dwPixelRateDESC[0]) &&
        (m_wXResAct > 1920))
      bnormal = FALSE;
    iRetCode = HandleEdgePixelRate(bnormal);     // Prepare Camera for recording

    if (iRetCode != PCO_NOERROR)
    {
      Disconnected(__LINE__);
      return -1;
    }
    m_bFirstArm = FALSE;
  }
#else
  if(strCamType.wCamType == CAMERATYPE_PCO_EDGE)
  {
    CString csh;

    csh = "You must handle the transfer parameter setting with the edge!!!\r\n";
    csh +="Please uncomment #define DO_THE_EDGE in sc2_class.h";
    AfxMessageBox((LPCTSTR)csh, MB_ICONERROR, 0);
  }
#endif

  if(m_bFirstArm == FALSE)
    iRetCode = PCO_ArmCamera(hCam);     // Prepare Camera for recording
  m_bFirstArm = TRUE;
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }

  DWORD dwWarn = 0, dwErr = 0, dwStatus = 0;
  iRetCode = PCO_GetCameraHealthStatus(hCam, &dwWarn, &dwErr, &dwStatus);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  if(dwErr != 0)
  {
    CString csh;

    csh.Format("Camera internal error 0x%X in line %d. Please switch off camera.", dwErr, __LINE__);
    AfxMessageBox((LPCTSTR)csh, MB_OK);
  }

  iRetCode = PCO_SetRecordingState(hCam, 1);// Start recording
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  
  if(m_hSC2Dlg != NULL)
  {
    iRetCode = PCO_EnableDialogCam(m_hSC2Dlg, FALSE);
    if (iRetCode != PCO_NOERROR)
      m_hSC2Dlg = NULL;
  }

  m_bThreadRun = TRUE;
  if (m_wthWorker == NULL)
    m_wthWorker = AfxBeginThread((AFX_THREADPROC) CamTimer, this,
    THREAD_PRIORITY_NORMAL, 0, 0);
  return 0;
}

// Stops recording
void CSC2Class::StopRecord()
{
  m_bPlay = FALSE;
  int iRetCode = PCO_CancelImages(hCam);   // If there's still a buffer in the driver queue, remove it
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return;
  }
  
   iRetCode = PCO_SetRecordingState(hCam, 0);// stop recording
  if (iRetCode & PCO_WARNING_FIRMWARE_FUNC_ALREADY_OFF)
    iRetCode = PCO_NOERROR;
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return;
  }
  if ((m_wthWorker != NULL) &&(m_bThreadRun == TRUE))
  {
    m_bThreadRun = FALSE;
    SetEvent(m_hEvent);
    try
    {
      WaitForSingleObject(m_wthWorker->m_hThread, 1000);// wait till thread is done
    }
    catch (...)
    {};
  }
  if(m_hSC2Dlg != NULL)
  {
    iRetCode = PCO_EnableDialogCam(m_hSC2Dlg, TRUE);
    if (iRetCode != PCO_NOERROR)
      Disconnected(__LINE__);
  }

  if ((m_wXResAct != 0) &&(m_wYResAct != 0))
  {
    PCO_GetImageEx(hCam, strStorage.wActSeg, 1, 1, 0, m_wXResAct, m_wYResAct, strSensor.strDescription.wDynResDESC);
    Convert();
  }
  m_wthWorker = NULL;
}

// start playback
int CSC2Class::StartPlay()
{
  int iRetCode;

  if (m_bConnectionLost)
    return -1;

  iRetCode = PCO_GetImageStruct(hCam, &strImage);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  
  iRetCode = PCO_GetStorageStruct(hCam, &strStorage);
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  
  iRetCode = PCO_GetActiveRamSegment(hCam, &m_wActSeg);// Get the active Segment
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }
  iRetCode = PCO_GetNumberOfImagesInSegment(hCam, m_wActSeg, &m_dwValidImageCnt, &m_dwMaxImageCnt);  
                                       // and get the number of images in it
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return -1;
  }

  m_wXResAct = strImage.strSegment[strStorage.wActSeg - 1].wXRes;// Get resolution that has been recorded with
  m_wYResAct = strImage.strSegment[strStorage.wActSeg - 1].wYRes;

  m_bPlay = TRUE;
  PrepareBuffers();                    // Prepare receiving buffers and memory bitmap

  m_bThreadRun = TRUE;
  if (m_wthWorker == NULL)
    m_wthWorker = AfxBeginThread((AFX_THREADPROC) CamTimer, this,
    THREAD_PRIORITY_NORMAL, 0, 0);
  return 0;
}

// stop playback
void CSC2Class::StopPlay()
{
  if ((m_wthWorker != NULL) &&(m_bThreadRun == TRUE))
  {
    m_bThreadRun = FALSE;
    SetEvent(m_hEvent);
    try
    {
      WaitForSingleObject(m_wthWorker->m_hThread, 1000);// wait till thread is done
    }
    catch (...)
    {};
  }
  m_wthWorker = NULL;
  int iRetCode = PCO_CancelImages(hCam);   // If there's still a buffer in the driver queue, remove it
  if (iRetCode != PCO_NOERROR)
  {
    Disconnected(__LINE__);
    return;
  }
  if ((m_wXResAct != 0) &&(m_wYResAct != 0))
  {
    PCO_GetImageEx(hCam, strStorage.wActSeg, 1, 1, 0, m_wXResAct, m_wYResAct, strSensor.strDescription.wDynResDESC);
    Convert();
  }
}

// Close camera
void CSC2Class::CloseCamera()
{
  StopRecord();
  CloseConvertDialog();
  CloseCamDialog();
  
  if(hColConvert)
    PCO_ConvertDelete(hColConvert);
  if(hBWConvert)
    PCO_ConvertDelete(hBWConvert);


  if (m_hBmp)
    DeleteObject(m_hBmp);              // Delete the memory Bitmap if exists
  m_hBmp = NULL;
  
  if (m_pic12)
    PCO_FreeBuffer(hCam, 0);           // Frees the memory that was allocated for the buffer
  
  if (hCam)
    PCO_CloseCamera(hCam);
}

// returns the actual horizontal resolution
int CSC2Class::GetXRes()
{
  return (int) m_wXResAct;
}

// returns the actual vertical resolution
int CSC2Class::GetYRes()
{
  return (int) m_wYResAct;
}

void CSC2Class::AutoGain()
{
  unsigned short minimum = 65535, maximum = 0;
  int err = PCO_NOERROR;
  PCO_Display strDisplay;
  
  strDisplay.wSize = sizeof(PCO_Display);

  GetDialog(TRUE);

  if (m_bConnectionLost)
    return;

  DWORD dwWaitResult = WaitForSingleObject( 
            m_hMutex,    // handle to mutex
            2000);  // no time-out interval
 
  if((dwWaitResult == WAIT_ABANDONED) || (dwWaitResult == STATUS_TIMEOUT))
    return;

  
  if (m_pic12)        // find minimal and maximal values in raw image data
  {
    for (int i = 20 * GetXRes(); i < GetXRes() * GetYRes(); i++)
    {
      minimum = min(minimum, m_pic12[i]);
      maximum = max(maximum, m_pic12[i]);
    }
  }
  
  unsigned short bitpix = GetBitPix();
  minimum = minimum >>(16 - bitpix);  // Correct value since raw data is left aligned
  maximum = maximum >>(16 - bitpix);

  if (m_iColorCam == 1)
  {
    err = PCO_ConvertGetDisplay(hColConvert, (PCO_Display*)&strDisplay.wSize);
    strDisplay.iScale_min = minimum;
    strDisplay.iScale_max = maximum;
    err = PCO_ConvertSetDisplay(hColConvert, (PCO_Display*)&strDisplay.wSize);
    
    if (hLutDialog != NULL)
      PCO_SetConvertDialog(hLutDialog, hColConvert);
  }
  else
  {
    err = PCO_ConvertGetDisplay(hBWConvert, (PCO_Display*)&strDisplay.wSize);
    strDisplay.iScale_min = minimum;
    strDisplay.iScale_max = maximum;
    err = PCO_ConvertSetDisplay(hBWConvert, (PCO_Display*)&strDisplay.wSize);

    if (hLutDialog != NULL)
      PCO_SetConvertDialog(hLutDialog, hBWConvert);
  }

  ReleaseMutex(m_hMutex);
  Convert();
}

void CSC2Class::WhiteBalance()
{
  int imode = 0;
  if (m_iColorCam == 1 && hColConvert)// It's a color camera
  {
    int imosaiker[4] = {BAYER_UPPER_LEFT_IS_RED, BAYER_UPPER_LEFT_IS_GREEN_RED,
                        BAYER_UPPER_LEFT_IS_GREEN_BLUE, BAYER_UPPER_LEFT_IS_BLUE};
    int imosaik;

    DWORD dwWaitResult = WaitForSingleObject( 
              m_hMutex,    // handle to mutex
              2000);  // no time-out interval
   
    if((dwWaitResult == WAIT_ABANDONED) || (dwWaitResult == STATUS_TIMEOUT))
      return;

    if ((m_iColorPatternType > 3) ||(m_iColorPatternType < 0))
      m_iColorPatternType = 0;
    imosaik = imosaiker[m_iColorPatternType];
    imode |= imosaik;

    int icoltemp, itint;
    int err = PCO_GetWhiteBalance(hColConvert, &icoltemp, &itint, imode, m_wXResAct, m_wYResAct, m_pic12, 0, 0, m_wXResAct, m_wYResAct);

    if(err == PCO_NOERROR)
    {
      PCO_Display strDisplay;

      strDisplay.wSize = sizeof(PCO_Display);
      err = PCO_ConvertGetDisplay(hColConvert, (PCO_Display*) &strDisplay.wSize);
      strDisplay.iColor_temp = icoltemp;
      strDisplay.iColor_tint = itint;
      PCO_ConvertSetDisplay(hColConvert, (PCO_Display*) &strDisplay.wSize);
      err = PCO_SetConvertDialog(hLutDialog, hColConvert);
    }
    ReleaseMutex(m_hMutex);
  }
  Convert();
}

#if defined DO_THE_EDGE
int CSC2Class::HandleEdgePixelRate(bool bNormal)
{
  WORD wId, wh = 0;
  int err;
  PCO_SC2_CL_TRANSFER_PARAM strCLTransferParameter;


  err = PCO_GetTransferParameter(hCam, &strCLTransferParameter, sizeof(strCLTransferParameter));
  if(!bNormal)
  {
    bool btestimage = FALSE;

    strCLTransferParameter.DataFormat = PCO_CL_DATAFORMAT_5x12L | SCCMOS_FORMAT_TOP_CENTER_BOTTOM_CENTER;
    wId = 0x1612;                // Switch LUT->sqrt
  }
  else
  {
    strCLTransferParameter.DataFormat = PCO_CL_DATAFORMAT_5x16 | SCCMOS_FORMAT_TOP_CENTER_BOTTOM_CENTER;
    wId = 0;                     // Switch LUT->off
  }
  err = PCO_SetTransferParameter(hCam, &strCLTransferParameter, sizeof(strCLTransferParameter));
  if(err != PCO_NOERROR)
  {
    if((err & PCO_ERROR_DRIVER_LUT_MISMATCH) == PCO_ERROR_DRIVER_LUT_MISMATCH)
    {
      static bool balreadyshown = FALSE;

      if(balreadyshown == FALSE)
        AfxMessageBox("Camera and sc2_cl_xxx.dll lut do not match.\r\nSwitching back to 12bit mode.\r\nPlease update your sdk dlls and/or Camware!", MB_ICONERROR | MB_OK, 0);
      balreadyshown = TRUE;
      strCLTransferParameter.DataFormat = PCO_CL_DATAFORMAT_5x12 | SCCMOS_FORMAT_TOP_CENTER_BOTTOM_CENTER;
      wId = 0;                // 
      err = PCO_SetTransferParameter(hCam, &strCLTransferParameter, sizeof(strCLTransferParameter));
      if(err != PCO_NOERROR)
      {
        return err;
      }
    }
  }

  err = PCO_SetActiveLookupTable(hCam, &wId, &wh);
  return err;
}
#endif
