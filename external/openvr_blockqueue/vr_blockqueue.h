// Converted to C++ from the OpenVR C API.

#pragma once

#ifndef _OPENVR_BLOCKQUEUE
#define _OPENVR_BLOCKQUEUE

#include "openvr.h"

namespace vr
{
	typedef uint64_t PathHandle_t;

	struct PathWrite_t
	{
		PathHandle_t ulPath;
		enum EPropertyWriteType writeType;
		enum ETrackedPropertyError eSetError;
		void* pvBuffer;
		uint32_t unBufferSize;
		PropertyTypeTag_t unTag;
		enum ETrackedPropertyError eError;
		const char* pszPath;
	};

	struct PathRead_t
	{
		PathHandle_t ulPath;
		void* pvBuffer;
		uint32_t unBufferSize;
		PropertyTypeTag_t unTag;
		uint32_t unRequiredBufferSize;
		enum ETrackedPropertyError eError;
		const char* pszPath;
	};

	typedef enum EBlockQueueError
	{
		EBlockQueueError_BlockQueueError_None = 0,
		EBlockQueueError_BlockQueueError_QueueAlreadyExists = 1,
		EBlockQueueError_BlockQueueError_QueueNotFound = 2,
		EBlockQueueError_BlockQueueError_BlockNotAvailable = 3,
		EBlockQueueError_BlockQueueError_InvalidHandle = 4,
		EBlockQueueError_BlockQueueError_InvalidParam = 5,
		EBlockQueueError_BlockQueueError_ParamMismatch = 6,
		EBlockQueueError_BlockQueueError_InternalError = 7,
		EBlockQueueError_BlockQueueError_AlreadyInitialized = 8,
		EBlockQueueError_BlockQueueError_OperationIsServerOnly = 9,
		EBlockQueueError_BlockQueueError_TooManyConnections = 10,
	} EBlockQueueError;

	typedef enum EBlockQueueReadType
	{
		EBlockQueueReadType_BlockQueueRead_Latest = 0,
		EBlockQueueReadType_BlockQueueRead_New = 1,
		EBlockQueueReadType_BlockQueueRead_Next = 2,
	} EBlockQueueReadType;

	typedef enum EBlockQueueCreationFlag
	{
		EBlockQueueCreationFlag_BlockQueueFlag_OwnerIsReader = 1,
	} EBlockQueueCreationFlag;

	class IVRPaths
	{
	public:
		virtual ETrackedPropertyError ReadPathBatch(PropertyContainerHandle_t ulRootHandle, PathRead_t* pBatch, uint32_t unBatchEntryCount) = 0;
		virtual ETrackedPropertyError WritePathBatch(PropertyContainerHandle_t ulRootHandle, PathWrite_t* pBatch, uint32_t unBatchEntryCount) = 0;
		virtual ETrackedPropertyError StringToHandle(PathHandle_t* pHandle, const char* pchPath) = 0;
		virtual ETrackedPropertyError HandleToString(PathHandle_t pHandle, const char* pchBuffer, uint32_t unBufferSize, uint32_t* punBufferSizeUsed) = 0;
	};

	class IVRBlockQueue
	{
	public:
		virtual EBlockQueueError Create(PropertyContainerHandle_t* pulQueueHandle, const char* pchPath, uint32_t unBlockDataSize, uint32_t unBlockHeaderSize, uint32_t unBlockCount, uint32_t unFlags) = 0;

		virtual EBlockQueueError Connect(PropertyContainerHandle_t* pulQueueHandle, const char* pchPath) = 0;

		virtual EBlockQueueError Destroy(PropertyContainerHandle_t ulQueueHandle) = 0;

		virtual EBlockQueueError AcquireWriteOnlyBlock(PropertyContainerHandle_t ulQueueHandle, PropertyContainerHandle_t* pulBlockHandle, void** ppvBuffer) = 0;

		virtual EBlockQueueError ReleaseWriteOnlyBlock(PropertyContainerHandle_t ulQueueHandle, PropertyContainerHandle_t ulBlockHandle) = 0;

		virtual EBlockQueueError WaitAndAcquireReadOnlyBlock(PropertyContainerHandle_t ulQueueHandle, 
PropertyContainerHandle_t* pulBlockHandle, void** ppvBuffer, EBlockQueueReadType eReadType, uint32_t unTimeoutMs) = 0;

		virtual EBlockQueueError AcquireReadOnlyBlock(PropertyContainerHandle_t ulQueueHandle, PropertyContainerHandle_t* pulBlockHandle, void** ppvBuffer, EBlockQueueReadType eReadType) = 0;

		virtual EBlockQueueError ReleaseReadOnlyBlock(PropertyContainerHandle_t ulQueueHandle, PropertyContainerHandle_t ulBlockHandle) = 0;

		virtual EBlockQueueError QueueHasReader(PropertyContainerHandle_t ulQueueHandle, bool* pbHasReaders) = 0;
	};


	static const char* IVRPaths_Version = "IVRPaths_001";
	static const char* IVRBlockQueue_Version = "IVRBlockQueue_005";
}

#endif