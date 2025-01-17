﻿/*************************************************************************************/
/*
* File name: PacketDelay.cpp
*
* Synopsis:  This program shows how to calculate inter-packet delays for your GigE
*            Vision® camera. The resulting table printed at the end of this example
*            can be used to program the appropriate delay in user applications using
*            MdigControl with M_GC_INTER_PACKET_DELAY.
*
*      Note: The inter-packet delay is initially set to zero. A reference frame rate
*            is then sampled and used for subsequent calculations. The algorithm then
*            proceeds and calculates a theoretical inter-packet delay for the current 
*            camera parameters (SizeX, SizeY, PacketSize, PixelFormat). This theoretical
*            delay is used as a starting point. At this point acquisition is re-started
*            and the obtained frame-rate is compared to the reference frame-rate.
*            Modifications to the theoretical inter-packet delay are then performed when
*            needed. This process repeats iteratively until the obtained frame rate
*            converges to the reference frame rate. If the reference frame rate initially
*            sampled is off, then the algorithm will not converge to the solution.
*
* Copyright © Matrox Electronic Systems Ltd., 1992-YYYY.
* All Rights Reserved
*/

#include <mil.h>
#include <vector>
#if M_MIL_USE_WINDOWS
#include <conio.h>
#include <windows.h>
#endif

using namespace std;

/* Number of images in the buffering grab queue.
Generally, increasing this number gives better real-time grab.
*/
#define BUFFERING_SIZE_MAX 20

/* Set this define to 1 to print additional details performed by this example. */
#define PRINT_DETAILS      0

/* User's processing function prototype. */
MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType,
                                  MIL_ID HookId,
                                  void* HookDataPtr);

/* Struct and variable definitions/declarations. */
MIL_ID MilGrabBufferList[BUFFERING_SIZE_MAX] = { 0 };
MIL_INT MilGrabBufferListSize;

struct PacketDelayInfo
   {
   PacketDelayInfo()
      {
      BaseFrameRate = 0;
      ProcessFrameRate = 0;
      DelayInSeconds = 0;
      TickFreq = 0;
      DelayTickVal = 0;
      ProcessFrameCount = 0;
      EqualityCounter = 0;
      Error = false;
      }
   MIL_DOUBLE BaseFrameRate;
   MIL_DOUBLE ProcessFrameRate;
   MIL_DOUBLE DelayInSeconds;
   MIL_UINT64 TickFreq;
   MIL_INT DelayTickVal;
   MIL_INT ProcessFrameCount;
   MIL_INT EqualityCounter;
   bool Error;
   };

struct PacketDelayResults
   {
   PacketDelayResults()
      {
      Selection = 0;
      }

   vector<MIL_STRING> PixelFormats;
   vector<MIL_INT> InterPacketDelayInTicks;
   vector<MIL_DOUBLE> InterPacketDelayInSec;
   vector<MIL_DOUBLE> ReferenceFrameRate;
   vector<MIL_DOUBLE> ObtainedFrameRate;
   unsigned long Selection;
   };

/* Utility functions. */
void EnumeratePixelFormats(MIL_ID MilDigitizer, MIL_INT BoardType, PacketDelayResults& Results);
void ApplyPixelFormat(MIL_ID MilDigitizer, PacketDelayResults& Results);
void AllocateAcquisitionBuffers(MIL_ID MilSystem, MIL_ID MilDigitizer, MIL_INT BoardType);
void AcquireReferenceFrameRate(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results);
void FindInterPacketDelay(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results);
void PrintResults(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results);
void GetMilBufferInfoFromPixelFormat(MIL_ID MilDigitizer, MIL_INT& SizeBand,
                                     MIL_INT& BufType, MIL_INT64& Attribute);
bool IsEqual(MIL_DOUBLE A, MIL_DOUBLE B)
   {
   if(((A+0.1) >= B) && ((A-0.1) <= B))
      return true;
   else
      return false;
   }

/* Main function. */
/* -------------- */

int MosMain(void)
   {
   MIL_ID MilApplication;
   MIL_ID MilSystem     ;
   MIL_ID MilDigitizer  ;
   MIL_INT BoardType = 0;
   MIL_UINT NbIterations = 0, i = 0;
   PacketDelayInfo PktInfo;
   PacketDelayResults Results;

   /* Allocate defaults. */
   MappAllocDefault(M_DEFAULT, &MilApplication, &MilSystem, M_NULL,
      &MilDigitizer, M_NULL);
   
   /* Inquire board type. */
   MsysInquire(MilSystem, M_BOARD_TYPE, &BoardType);

   /* This example only runs on gige vision systems. */
   if((BoardType != M_GIGE_VISION))
      {
      MosPrintf(MIL_TEXT("This example only runs on GigE Vision systems.\n"));   
      MappFreeDefault(MilApplication, MilSystem, M_NULL, MilDigitizer, M_NULL);
      return 0;
      }

   /* Inquire the camera's clock tick frequency. */
   MdigInquire(MilDigitizer, M_GC_COUNTER_TICK_FREQUENCY, &PktInfo.TickFreq);
   if(PktInfo.TickFreq == 0)
      {
      MosPrintf(MIL_TEXT("Error, camera does not support inter-packet delay.\n"));
      
      /* Release defaults. */
      MappFreeDefault(MilApplication, MilSystem, M_NULL, MilDigitizer, M_NULL);
      return 0;
      }

   /* Print a message. */
   MosPrintf(MIL_TEXT("\nThis example shows how to calculate inter-packet\n"));
   MosPrintf(MIL_TEXT("delay for your GigE Vision camera.\n\n"));
   MosPrintf(MIL_TEXT("Inter-packet delay is used to spread packet transmission\n"));
   MosPrintf(MIL_TEXT("over the length of a frame. This is done to minimize the chance\n"));
   MosPrintf(MIL_TEXT("of FIFO overruns inside your Gigabit Ethernet controller.\n"));
   MosPrintf(MIL_TEXT("Press <Enter> to continue.\n\n\n"));
   MosGetch();

   /* Print the camera's pixel formats and wait for user selections. */
   EnumeratePixelFormats(MilDigitizer, BoardType, Results);
   if(Results.Selection == Results.PixelFormats.size())
      {
      NbIterations = Results.PixelFormats.size();
      Results.Selection = 0;
      }
   else
      NbIterations = 1;
   
   /* Iterate through the user's selected pixel formats. */
   while(NbIterations--)
      {
      memset(&PktInfo, 0, sizeof(PacketDelayInfo));
      
      /* Inquire the camera's clock frequency so we can convert clock ticks to seconds. */
      MdigInquire(MilDigitizer, M_GC_COUNTER_TICK_FREQUENCY, &PktInfo.TickFreq);

      /* Apply the next pixel format for calculation. */
      ApplyPixelFormat(MilDigitizer, Results);
      
      /* Allocate grab buffers matching the camera's pixel format. */
      AllocateAcquisitionBuffers(MilSystem, MilDigitizer, BoardType);

      /* Print a message. */
      MosPrintf(MIL_TEXT("\n\nCalculating inter-packet delay for %s.\n\n"), Results.PixelFormats[Results.Selection].c_str());
      
      /* Get the reference frame rate. */
      AcquireReferenceFrameRate(MilDigitizer, PktInfo, Results);

      /* With the reference frame rate found, find the optimal inter-packet delay. */
      FindInterPacketDelay(MilDigitizer, PktInfo, Results);

      /* Free the grab buffers. */
      while(MilGrabBufferListSize > 0)
         MbufFree(MilGrabBufferList[--MilGrabBufferListSize]);
      
      Results.Selection++;
      }

   /* Print results. */
   PrintResults(MilDigitizer, PktInfo, Results);
   MosPrintf(MIL_TEXT("Press <Enter> to quit.\n\n\n"));
   MosGetch();
   
   /* Reset inter-packet delay to zero. */
   MdigControl(MilDigitizer, M_GC_INTER_PACKET_DELAY, 0);

   /* Release defaults. */
   MappFreeDefault(MilApplication, MilSystem, M_NULL, MilDigitizer, M_NULL);

   return 0;
   }

/* Enumerate the camera's pixel formats. Only MIL compatible formats are printed. */
/* ------------------------------------------------------------------------------ */
void EnumeratePixelFormats(MIL_ID MilDigitizer, MIL_INT BoardType, PacketDelayResults& Results)
   {
   long SpreadFactor = 0;
   MIL_INT64 PixFmt = 0;
   Results.Selection = -1;
   MIL_INT Count = 0;
   
   /* Inquire the number of pixel formats supported by the camera. */
   MdigInquireFeature(MilDigitizer, M_FEATURE_ENUM_ENTRY_COUNT, MIL_TEXT("PixelFormat"), M_TYPE_MIL_INT, &Count);
   if(Count)
      {
      bool Done = false;
      Results.InterPacketDelayInTicks.assign(Count, 0);
      Results.InterPacketDelayInSec.assign(Count, 0.0);
      Results.ReferenceFrameRate.assign(Count, 0.0);
      Results.ObtainedFrameRate.assign(Count, 0.0);

      MosPrintf(MIL_TEXT("Your camera supports the following pixel formats:\n"));
      for (MIL_INT i = 0; i < Count; i++)
         {
         MIL_INT64 AccessMode = 0;
         /* Get the nth pixel format's string length. . */
         /* Get the nth pixel format's name and numerical value. */
         MIL_STRING PixelFormat;
         MdigInquireFeature(MilDigitizer, M_FEATURE_ENUM_ENTRY_NAME + i, MIL_TEXT("PixelFormat"), M_TYPE_STRING, PixelFormat);
         MdigInquireFeature(MilDigitizer, M_FEATURE_ENUM_ENTRY_VALUE + i, MIL_TEXT("PixelFormat"), M_TYPE_INT64, &PixFmt);
         MdigInquireFeature(MilDigitizer, M_FEATURE_ENUM_ENTRY_ACCESS_MODE + i, MIL_TEXT("PixelFormat"), M_TYPE_INT64, &AccessMode);

         /* Validate that the pixel format is compatible with MIL. */
         if (M_FEATURE_IS_AVAILABLE(AccessMode) && (PixFmt & PFNC_CUSTOM) != PFNC_CUSTOM)
            {
            MIL_INT SizeBand = 0, BufType = 0;
            MIL_INT64 Attribute = 0;
            
            MappControl(M_ERROR, M_PRINT_DISABLE);
            MdigControlFeature(MilDigitizer, M_FEATURE_VALUE, MIL_TEXT("PixelFormat"), M_TYPE_STRING, PixelFormat);
            GetMilBufferInfoFromPixelFormat(MilDigitizer, SizeBand, BufType, Attribute);

            if(SizeBand && BufType && Attribute)
               {
               Results.PixelFormats.push_back(PixelFormat);
               MosPrintf(MIL_TEXT("%d %s\n"), (int)(Results.PixelFormats.size() - 1), PixelFormat.c_str());
               }
            MappControl(M_ERROR, M_PRINT_ENABLE);
            }
         }

      /* Add an entry so the user can perform calculations on All pixel formats. */
      if(Results.PixelFormats.size() > 1)
         MosPrintf(MIL_TEXT("%d All\n"), (int)Results.PixelFormats.size());
      
      /* Wait for user selection. */
      if(Results.PixelFormats.size() > 1)
         {
         do
            {
            MosPrintf(MIL_TEXT("\nPlease select the pixel format that you want use for inter-packet delay\n"));
            MosPrintf(MIL_TEXT("calculation (0-%d): "), (int)Results.PixelFormats.size());
#if M_MIL_USE_WINDOWS
            scanf_s("%d-", &(Results.Selection));
#else
            scanf("%ld-", &(Results.Selection));
#endif
            if(Results.Selection >= 0 && Results.Selection <= Results.PixelFormats.size())
               Done = true;
            else
               MosPrintf(MIL_TEXT("Invalid selection, please try again\n."));
            }
         while(!Done);

         MosPrintf(MIL_TEXT("\n%s selected\n\n"),
            Results.Selection < Results.PixelFormats.size() ? Results.PixelFormats[Results.Selection].c_str() : MIL_TEXT("All"));
         }
      else
         Results.Selection = 0;
      }
   }

/* Set the camera's pixel format to the next value. */
/* ------------------------------------------------ */
void ApplyPixelFormat(MIL_ID MilDigitizer, PacketDelayResults& Results)
   {
   MIL_INT64 AccessMode = 0;

   /* Wait for PixelFormat to become writable before writing. */
   MdigInquireFeature(MilDigitizer, M_FEATURE_ACCESS_MODE, MIL_TEXT("PixelFormat"), M_TYPE_INT64, &AccessMode);
   while (M_FEATURE_IS_WRITABLE(AccessMode) == M_FALSE)
      {
      MdigInquireFeature(MilDigitizer, M_FEATURE_ACCESS_MODE, MIL_TEXT("PixelFormat"), M_TYPE_INT64, &AccessMode);
      MosSleep(250);
      }

   MdigControlFeature(MilDigitizer, M_FEATURE_VALUE, MIL_TEXT("PixelFormat"), M_TYPE_STRING, Results.PixelFormats[Results.Selection]);
   }

/* Allocate acquisition buffers compatible with the camera's pixel format. */
/* ----------------------------------------------------------------------- */
void AllocateAcquisitionBuffers(MIL_ID MilSystem, MIL_ID MilDigitizer, MIL_INT BoardType)
   {
   MIL_INT SizeBand = 1;
   MIL_INT BufType = 8+M_UNSIGNED;
   MIL_INT64 AdditionalAttributes = 0;

   /* On the M_GIGE_VISION system, turn off the pixel-format switching feature. */
   /* Also turn off the automatic Bayer conversion feature. */
   /* We must also allocate grab buffers that are of the same format as the camera. */
   if(BoardType == M_GIGE_VISION)
      {
      MdigControl(MilDigitizer, M_GC_PIXEL_FORMAT_SWITCHING, M_DISABLE);
      MdigControl(MilDigitizer, M_BAYER_CONVERSION, M_DISABLE);
      GetMilBufferInfoFromPixelFormat(MilDigitizer, SizeBand, BufType, AdditionalAttributes);
      }

   /* Allocate the grab buffers and clear them. */
   MappControl(M_DEFAULT, M_ERROR, M_PRINT_DISABLE);
   for(MilGrabBufferListSize = 0; 
      MilGrabBufferListSize<BUFFERING_SIZE_MAX; MilGrabBufferListSize++)
      {
      MbufAllocColor(MilSystem,
         SizeBand,
         MdigInquire(MilDigitizer, M_SIZE_X, M_NULL),
         MdigInquire(MilDigitizer, M_SIZE_Y, M_NULL),
         BufType,
         M_IMAGE+M_GRAB+M_PROC+AdditionalAttributes,
         &MilGrabBufferList[MilGrabBufferListSize]);

      if (MilGrabBufferList[MilGrabBufferListSize])
         {
         MbufClear(MilGrabBufferList[MilGrabBufferListSize], 0xFF);
         }
      else
         break;
      }
   MappControl(M_DEFAULT, M_ERROR, M_PRINT_ENABLE);
   }

/* Use MdigProcess to acquire a reference frame rate with the inter-packet delay to zero. */
/* -------------------------------------------------------------------------------------- */
void AcquireReferenceFrameRate(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results)
   {
   /* Set initial inter-packet delay to zero; this is to measure the base frame rate of the
      camera. */
   MdigControl(MilDigitizer, M_GC_INTER_PACKET_DELAY, 0);

   /* Start the MdigProcess; here we want to record a base frame rate that
      will be used for our calculations later. */
   MdigProcess(MilDigitizer, MilGrabBufferList, MilGrabBufferListSize,
      M_SEQUENCE+M_COUNT(BUFFERING_SIZE_MAX), M_DEFAULT, ProcessingFunction, M_NULL);

   MdigProcess(MilDigitizer, MilGrabBufferList, MilGrabBufferListSize,
      M_STOP, M_DEFAULT, ProcessingFunction, M_NULL);

   /* Inquire the reference frame rate. */
   MdigInquire(MilDigitizer, M_PROCESS_FRAME_RATE, &Info.BaseFrameRate);
   Results.ReferenceFrameRate[Results.Selection] = Info.BaseFrameRate;

   /* With the frame-rate estimated, inquire the theoretical inter-packet delay to use. */
   MdigInquire(MilDigitizer, M_GC_THEORETICAL_INTER_PACKET_DELAY, &Info.DelayInSeconds);

   /* Convert the delay from seconds to camera ticks. */
   Info.DelayTickVal = (MIL_UINT32)(Info.DelayInSeconds * Info.TickFreq);
   }

/* Iteratively find a solution that maximizes the inter-packet delay without      */
/* disturbing the frame-rate of the camera.                                       */
/* ------------------------------------------------------------------------------ */
void FindInterPacketDelay(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results)
   {
   bool Done = false;

#if PRINT_DETAILS
   MosPrintf(MIL_TEXT("Reference frame-rate used: %.2f\n\n"), Info.BaseFrameRate);
#endif

   while(!Done)
      {
      /* Set the delay in the camera. Initially this delay corresponds to the value
         returned by MdigInquire with M_GC_THEORETICAL_INTER_PACKET_DELAY. */
      MdigControl(MilDigitizer, M_GC_INTER_PACKET_DELAY, Info.DelayTickVal);

      /* Start acquisition. */
      MdigProcess(MilDigitizer, MilGrabBufferList, MilGrabBufferListSize,
         M_SEQUENCE+M_COUNT(BUFFERING_SIZE_MAX), M_DEFAULT, ProcessingFunction, M_NULL);

      /* Inquire the obtained frame rate with the current inter-packet delay. */
      MdigInquire(MilDigitizer, M_PROCESS_FRAME_RATE,  &Info.ProcessFrameRate);

      /* Stop acquisition. */
      MdigProcess(MilDigitizer, MilGrabBufferList, MilGrabBufferListSize,
         M_STOP, M_DEFAULT, ProcessingFunction, M_NULL);

#if PRINT_DETAILS
      MosPrintf(MIL_TEXT("%Programming delay of %d ticks; frame-rate obtained: %.2f\r"),
         (int)Info.DelayTickVal, Info.ProcessFrameRate);
#else
      MosPrintf(MIL_TEXT("."));
#endif

      /* Validate if obtained frame rate matches reference frame rate. If it does not,
         reduce the inter packet delay and try another iteration. */
      if(IsEqual(Info.BaseFrameRate, Info.ProcessFrameRate))
         {
         /* Frame rate inquired is equal to the base frame rate; we are converging on
            the solution. */
         Info.EqualityCounter++;

         if(Info.DelayTickVal == 0)
            {
            Info.DelayInSeconds = 0.0;
            Info.Error = true;
            Done = true;
            }
         else if(Info.EqualityCounter == 3)
            {
            Done = true;
            /* Found optimal solution, remove an additional 15%. */
            Info.DelayInSeconds -= (Info.DelayInSeconds * 15.0 / 100.0);
            if(Info.DelayInSeconds <= 0.0)
               Info.DelayInSeconds = 0.0;

            Info.DelayTickVal = (MIL_UINT32)(Info.DelayInSeconds * Info.TickFreq);
            MdigControl(MilDigitizer, M_GC_INTER_PACKET_DELAY, Info.DelayTickVal);
            }
         else
            {
            /* Reduce delay for next iteration. */
            Info.DelayInSeconds -= (Info.DelayInSeconds / 50.0);
            Info.DelayTickVal = (MIL_UINT32)(Info.DelayInSeconds * Info.TickFreq);
            }
         }
      else
         {
         /* We are still far from the reference frame rate, reduce delay for next
            iteration. */
         Info.EqualityCounter = 0;
         Info.DelayInSeconds -= (Info.DelayInSeconds / 10.0);
         Info.DelayTickVal = (MIL_UINT32)(Info.DelayInSeconds * Info.TickFreq);
         if(Info.DelayTickVal == 0)
            {
            Info.DelayInSeconds = 0.0;
            Info.Error = true;
            Done = true;
            }
         else if(Info.DelayInSeconds <= 0.0)
            {
            Info.DelayInSeconds = 0.0;
            Done = true;
            }
         }
      
      MosSleep(500);
      }

   /* Store solution in Results struct. This will get printed at the end of the example. */
   if(Info.Error == false)
      {
      Results.InterPacketDelayInTicks[Results.Selection] = Info.DelayTickVal;
      Results.InterPacketDelayInSec[Results.Selection] = Info.DelayInSeconds;
      Results.ObtainedFrameRate[Results.Selection] = Info.ProcessFrameRate;
      }
   }

/* Print the results for each pixel format. */
/* ---------------------------------------- */
void PrintResults(MIL_ID MilDigitizer, PacketDelayInfo& Info, PacketDelayResults& Results)
   {
   MIL_STRING Model, Vendor;
   MIL_INT PacketSize = 0;

   /* Found optimal solution, print results. */
   MdigInquire(MilDigitizer, M_CAMERA_VENDOR, Vendor);
   MdigInquire(MilDigitizer, M_CAMERA_MODEL, Model);
   MdigInquire(MilDigitizer, M_GC_PACKET_SIZE, &PacketSize);

#if M_MIL_USE_WINDOWS
   system("cls");
#endif
   MosPrintf(MIL_TEXT("Inter-packet delay report summary for %s %s:\n\n"), Vendor.c_str(), Model.c_str());
   MosPrintf(MIL_TEXT("Camera parameters:\n"));
   MosPrintf(MIL_TEXT("Camera SizeX:         %lld\n"), (long long)(MdigInquire(MilDigitizer, M_SIZE_X, M_NULL)));
   MosPrintf(MIL_TEXT("Camera SizeY:         %lld\n"), (long long)(MdigInquire(MilDigitizer, M_SIZE_Y, M_NULL)));
   MosPrintf(MIL_TEXT("Camera Packet size:   %d\n\n"), (int)PacketSize);

   for (size_t i = 0; i < Results.PixelFormats.size(); i++)
      {
      MosPrintf(MIL_TEXT("Camera Pixel format:  %s\n"), Results.PixelFormats[i].c_str());
      MosPrintf(MIL_TEXT("Inter-packet delay of %d ticks (%.3f usec) calculated.\n"),
         (int)Results.InterPacketDelayInTicks[i], Results.InterPacketDelayInSec[i]*1e6);
      MosPrintf(MIL_TEXT("Reference frame rate: %.1f\n"), Results.ReferenceFrameRate[i]);
      MosPrintf(MIL_TEXT("Obtained frame rate:  %.1f\n"), Results.ObtainedFrameRate[i]);
      MosPrintf(MIL_TEXT("----------------------------------------------------------\n"));
      }

   MosPrintf(MIL_TEXT("\nPrinted inter-packet delay results are valid only for ")
      MIL_TEXT("the above parameters\n"));
   }

/* User's processing function called every time a grab buffer is modified. */
/* ----------------------------------------------------------------------- */
MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType,
                                  MIL_ID HookId,
                                  void* HookDataPtr)
   {
   MIL_ID ModifiedBufferId;

   /* Retrieve the MIL_ID of the grabbed buffer. */
   MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID, &ModifiedBufferId);

   return 0;
   }
 
/* Get the MIL buffer attributes that matches the camera's pixel format. */
/* --------------------------------------------------------------------- */
void GetMilBufferInfoFromPixelFormat(MIL_ID MilDigitizer, MIL_INT& SizeBand,
                                     MIL_INT& BufType, MIL_INT64& Attribute)
   {
   MdigInquire(MilDigitizer, M_SIZE_BAND, &SizeBand);
   MdigInquire(MilDigitizer, M_TYPE, &BufType);
   MdigInquire(MilDigitizer, M_SOURCE_DATA_FORMAT, &Attribute);
   }
 
