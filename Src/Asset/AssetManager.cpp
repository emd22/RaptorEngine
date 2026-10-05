#include "AssetManager.hpp"

#include "Core/Assert.hpp"
#include "Core/SizedArray.hpp"
#include "Loader/Image/LoaderKtx.hpp"
#include "Loader/Image/LoaderStb.hpp"
#include "Loader/Object/LoaderGltf.hpp"

#include <Core/Defines.hpp>
#include <Core/Thread/ThreadManager.hpp>
#include <Core/Types.hpp>
#include <Engine.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/Object.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Texture/TextureManager.hpp>
#include <Util/RustInterop.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>

namespace fx {

/////////////////////////////////////
// Scheduler backend
/////////////////////////////////////

static_assert(static_cast<int32>(loader::eLoaderStatus::None) == RX_LOAD_STATUS_NONE);
static_assert(static_cast<int32>(loader::eLoaderStatus::Success) == RX_LOAD_STATUS_SUCCESS);
static_assert(static_cast<int32>(loader::eLoaderStatus::Error) == RX_LOAD_STATUS_ERROR);

static loader::eLoaderStatus LoadObjectItem(AssetQueueItem& item, LockContext<AssetItemData>& asset_data)
{
	TSRef<loader::ObjectLoaderBase> object_loader(asset_data->pLoader);

	switch (item.AssetLoadOp) {
	case fx::eAssetLoadOp::ReadAndUpload:
		return object_loader->Load(asset_data->Ticket, String(item.Path));
	case fx::eAssetLoadOp::ProcessAndUpload: {
		const loader::eLoaderStatus status = object_loader->Load(asset_data->Ticket, item.pcRawData, item.DataSize);
		if (item.bOwnsRawData && item.pcRawData) {
			std::free(const_cast<uint8*>(item.pcRawData));
			item.pcRawData = nullptr;
			item.bOwnsRawData = false;
		}
		return status;
	}
	default:
		LogError(LC_ASSET, "Unknown asset source!");
		return loader::eLoaderStatus::None;
	}
}

static loader::eLoaderStatus LoadImageItem(AssetQueueItem& item, LockContext<AssetItemData>& asset_data)
{
	TSRef<loader::ImageLoaderBase> image_loader(asset_data->pLoader);

	switch (item.AssetLoadOp) {
	case fx::eAssetLoadOp::ReadAndUpload:
		return image_loader->Load(asset_data->Ticket, item.Path);
	case fx::eAssetLoadOp::ProcessAndUpload: {
		const loader::eLoaderStatus status = image_loader->Load(asset_data->Ticket, item.pcRawData, item.DataSize);
		if (item.bOwnsRawData && item.pcRawData) {
			std::free(const_cast<uint8*>(item.pcRawData));
			item.pcRawData = nullptr;
			item.bOwnsRawData = false;
		}
		return status;
	}
	default:
		LogError(LC_ASSET, "Unknown asset source!");
		return loader::eLoaderStatus::None;
	}
}

static void DoDirectUpload(AssetQueueItem& item, AssetItemData& asset_data)
{
	ImageInfo& img_info = item.ImgInfo;

	AssetTicket& ticket = asset_data.Ticket;
	Image* image = static_cast<Image*>(ticket.Get());

	image->Upload(renderer::GraphicsBackendFwd::GetUploadCmd(), img_info);

	// Upload() stages the pixels into a GPU buffer, so the CPU side copy is done with here
	img_info.FreeOwnedData();

	ticket.SignalUploadedToGpu();
}

static void ProcessLoadSuccess(LockContext<AssetItemData>& asset_data)
{
	AssetTicket& ticket = asset_data->Ticket;

	ticket.WaitUntilUploaded();

	// Objects and images tell whatever was waiting on them. Anything else is just marked as loaded.
	const bool run_callbacks = (asset_data->LoadType == eAssetType::Object || asset_data->LoadType == eAssetType::Image);

	ticket.Complete(run_callbacks ? ticket.Get() : nullptr, run_callbacks);

	if (asset_data->pLoader.IsValid()) {
		// Defer loader destruction until upload fence completes - staging buffers must stay alive. The upload fence is
		// waited before the next batch of uploads, so destroying here after the ticket is signalled is safe.
		asset_data->DestroyLoader();
	}
}

/// What the asset scheduler asks of the engine. `user` is the asset manager and a job is an `AssetQueueItem`.
struct SchedulerBackend
{
	static int32 Load(void*, void* job)
	{
		AssetQueueItem& item = *static_cast<AssetQueueItem*>(job);

		LockContext<AssetItemData> asset_data = item.GetDataContext();

		AssertMsg(item.AssetLoadOp != eAssetLoadOp::None, "No asset load op set!");

		loader::eLoaderStatus status = loader::eLoaderStatus::None;

		if (item.AssetLoadOp == eAssetLoadOp::DirectUpload || item.AssetLoadOp == eAssetLoadOp::CallUserFunction) {
			status = loader::eLoaderStatus::Success;
		}
		else if (item.IsObject()) {
			status = LoadObjectItem(item, asset_data);
		}
		else if (item.IsImage()) {
			status = LoadImageItem(item, asset_data);
		}

		return static_cast<int32>(status);
	}

	static void BeginUpload(void*)
	{
		renderer::gGraphics->UploadContext.UploadFence.WaitFor();
		renderer::gGraphics->UploadContext.UploadFence.Reset();

		renderer::gGraphics->UploadContext.CmdBuffer.Record();
	}

	static uint8 Upload(void*, void* job)
	{
		AssetQueueItem& item = *static_cast<AssetQueueItem*>(job);

		LockContext<AssetItemData> asset_data = item.GetDataContext();

		if (item.AssetLoadOp == eAssetLoadOp::DirectUpload) {
			DoDirectUpload(item, asset_data.Get());
			return 1;
		}

		if (item.AssetLoadOp == eAssetLoadOp::CallUserFunction) {
			AssetCustomFunctionType* fn = reinterpret_cast<AssetCustomFunctionType*>(asset_data->Ticket.Get());

			if (fn == nullptr) {
				LogError(LC_ASSET, "Cannot call custom user function as it is null");
			}
			else {
				(*fn)(renderer::gGraphics->UploadContext.CmdBuffer);
				delete fn;
			}

			item.Data.Ticket.SignalUploadedToGpu();

			return 1;
		}

		if (asset_data->pLoader.IsValid()) {
			asset_data->CreateGpuResource();
			return 1;
		}

		return 0;
	}

	static void EndUpload(void*)
	{
		using namespace renderer;

		CommandBuffer& cmd = gGraphics->UploadContext.CmdBuffer;
		cmd.End();

		const uint64_t tl_value = gGraphics->TransferCount.load() + 1;

		void* commands[] = { cmd.Cmd };

		const RxSubmitSignal signal = { .semaphore = RxRaw(gGraphics->TransferSync.InternalSemaphore),
										.value = tl_value };

		rx_gpu_queue_submit(gGraphics->GetDevice()->GetRustDevice(), RX_QUEUE_TRANSFER, nullptr, 0, commands, 1, &signal,
							1, RxRaw(gGraphics->UploadContext.UploadFence.Get()));

		gGraphics->TransferCount.store(tl_value);

		// Wait for transfer to complete before destroying staging buffers held by loaders
		gGraphics->UploadContext.UploadFence.WaitFor();
	}

	static void Finish(void*, void* job, int32 status)
	{
		AssetQueueItem* item = static_cast<AssetQueueItem*>(job);

		{
			LockContext<AssetItemData> asset_data = item->GetDataContext();

			if (status == RX_LOAD_STATUS_SUCCESS) {
				ProcessLoadSuccess(asset_data);
			}
			else if (status == RX_LOAD_STATUS_ERROR) {
				// There was an error, call the OnError callback if it was registered
				asset_data->Ticket.Fail(asset_data->LoadType == eAssetType::Object);
			}
			else {
				asset_data->Ticket.SignalFinished();
				Panic("AssetManager", "Worker status is none!");
			}
		}

		delete item;
	}

	static uint32 Frame(void*) { return renderer::gGraphics->GetElapsedFrameCount(); }

	static void WaitForUploads(void*) { renderer::gGraphics->UploadContext.UploadFence.WaitFor(); }

	static void Destroy(void*, void* resource)
	{
		// Wait for the graphics queue to become idle so that no submitted RenderCmd command buffer is still
		// referencing the buffer. Because the deletion is deferred by `scBufferDeletionFrameSpacing` rendered frames,
		// this returns almost immediately
		rx_gpu_queue_wait_idle(renderer::gGraphics->GetDevice()->GetRustDevice(), RX_QUEUE_GRAPHICS);

		rx_buffer_resource_destroy(static_cast<RxBufferResource*>(resource), renderer::gGraphics->GpuAllocator);
	}
};


/////////////////////////////////////
// Asset Manager
/////////////////////////////////////


void AssetManager::Start(int32 min_threads)
{
	AssertMsg(mbActive.test() == false, "Asset manager is already created!");

	mMinThreads = min_threads;
	mbActive.test_and_set();

	const RxAssetBackend backend = {
		.user = this,
		.load = &SchedulerBackend::Load,
		.begin_upload = &SchedulerBackend::BeginUpload,
		.upload = &SchedulerBackend::Upload,
		.end_upload = &SchedulerBackend::EndUpload,
		.finish = &SchedulerBackend::Finish,
		.frame = &SchedulerBackend::Frame,
		.wait_for_uploads = &SchedulerBackend::WaitForUploads,
		.destroy = &SchedulerBackend::Destroy,
		.log = &RustInterop::Log,
	};

	std::lock_guard<std::mutex> lock(mPendingMutex);

	mpScheduler = rx_asset_scheduler_new(&backend, static_cast<uint32>(mMinThreads));

	for (RxBufferResource* resource : mPendingDeletions) {
		rx_asset_scheduler_delete_resource(mpScheduler, resource, scBufferDeletionFrameSpacing);
	}

	for (AssetQueueItem* job : mPendingJobs) {
		rx_asset_scheduler_submit(mpScheduler, job);
	}

	mPendingDeletions.clear();
	mPendingJobs.clear();
}

void AssetManager::Submit(AssetQueueItem&& item)
{
	AssetQueueItem* job = new AssetQueueItem(std::move(item));

	{
		std::lock_guard<std::mutex> lock(mPendingMutex);

		if (mpScheduler == nullptr) {
			mPendingJobs.push_back(job);
			return;
		}
	}

	rx_asset_scheduler_submit(mpScheduler, job);
}

void AssetManager::DeleteBuffer(RxBufferResource* buffer_resource)
{
	{
		std::lock_guard<std::mutex> lock(mPendingMutex);

		if (mpScheduler == nullptr) {
			mPendingDeletions.push_back(buffer_resource);
			return;
		}
	}

	rx_asset_scheduler_delete_resource(mpScheduler, buffer_resource, scBufferDeletionFrameSpacing);
}

void AssetManager::SignalUpdate()
{
	if (mpScheduler != nullptr) {
		rx_asset_scheduler_signal(mpScheduler);
	}
}

void AssetManager::StopWorkers()
{
	if (mbWorkersStopped || !mbActive.test()) {
		return;
	}

	mbWorkersStopped = true;

	mbActive.clear();

	if (mpScheduler != nullptr) {
		rx_asset_scheduler_stop(mpScheduler);
	}
}

void AssetManager::Shutdown()
{
	LogInfo(LC_ASSET, "Shutting down asset manager...");

	if (!mbActive.test() && !mbWorkersStopped) {
		return;
	}

	StopWorkers();

	if (mbShutdownDone) {
		return;
	}

	mbShutdownDone = true;

	{
		std::lock_guard<std::mutex> lock(mNullImageMutex);
		mNullImageList.Clear();
		mpFlatNormalImage = nullptr;
	}

	// Flush pending GPU deletions before destroying allocator/device
	ShutdownDeletionQueue();
}

void AssetManager::RequestHigherDetail()
{
	const ObjectIDSpan nearby_objects = gWorldGrid->GetNearbyObjects();

	for (ObjectID object_id : nearby_objects) {
		Object* object = gObjectManager->GetObject(object_id);
		Material* material = gMaterialManager->GetMaterial(object->mMaterialID);
	}
}

void AssetManager::ShutdownDeletionQueue()
{
	if (mpScheduler != nullptr) {
		rx_asset_scheduler_flush_deletions(mpScheduler);
	}
}


/// Both KTX1 and KTX2 files start with the identifier "\xABKTX 1" / "\xABKTX 2" followed by "0\xBB\r\n\x1A\n".
inline bool IsMemoryKtx(const uint8* data, uint32 data_size)
{
	static constexpr uint8 scKtxMagic[] = { 0xAB, 'K', 'T', 'X', ' ' };

	if (data == nullptr || data_size < 12) {
		return false;
	}

	return memcmp(data, scKtxMagic, sizeof(scKtxMagic)) == 0 && (data[5] == '1' || data[5] == '2');
}

inline bool IsFileKtx(const std::string& path)
{
	FILE* fp = fopen(path.c_str(), "rb");

	if (fp == nullptr) {
		return false;
	}

	uint8 magic_buffer[12];
	const bool read_ok = fread(magic_buffer, 1, sizeof(magic_buffer), fp) == sizeof(magic_buffer);

	fclose(fp);

	return read_ok && IsMemoryKtx(magic_buffer, sizeof(magic_buffer));
}


/////////////////////////////////////
// Object loading functions
/////////////////////////////////////

AssetTicket AssetManager::LoadObject(const std::string& name, const std::string& path)
{
	Object* object = gObjectManager->NewObject(name, MaterialID::scNull);
	AssetTicket ticket { object };

	LoadFromPath<loader::LoaderGltf>(ticket, eAssetType::Object, path);

	return ticket;
}

AssetTicket AssetManager::LoadObjectFromMemory(const std::string& name, const uint8* data, uint32 data_size)
{
	Object* object = gObjectManager->NewObject(name, MaterialID::scNull);
	AssetTicket ticket { object };

	LoadFromMemory<loader::LoaderGltf>(ticket, eAssetType::Object, Slice<const uint8>(data, data_size));

	return ticket;
}


/////////////////////////////////////
// Image loading functions
/////////////////////////////////////

AssetTicket AssetManager::LoadImage(eImageType image_type, eImageFormat format, const std::string& path,
									eImageCreateFlags flags)
{
	AssetTicket ticket { gTextureManager->NewTexture() };

	if (IsFileKtx(path)) {
		TSRef<loader::LoaderKtx> loader = TSRef<loader::LoaderKtx>::New();

		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromPath<loader::LoaderKtx>(ticket, loader, eAssetType::Image, path);
	}
	// Everything else is decoded as a plain image
	else {
		TSRef<loader::LoaderStb> loader = TSRef<loader::LoaderStb>::New();

		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromPath<loader::LoaderStb>(ticket, loader, eAssetType::Image, path);
	}

	return ticket;
}


AssetTicket AssetManager::LoadImageFromMemory(eImageType image_type, eImageFormat format,
											  const Slice<const uint8>& data, eImageCreateFlags flags)
{
	AssetTicket ticket { gTextureManager->NewTexture() };

	if (IsMemoryKtx(data.pData, data.Size)) {
		TSRef<loader::LoaderKtx> loader = TSRef<loader::LoaderKtx>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromData<loader::LoaderKtx>(ticket, loader, eAssetType::Image, data);
	}
	else {
		// Decode it as a plain image
		TSRef<loader::LoaderStb> loader = TSRef<loader::LoaderStb>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromData<loader::LoaderStb>(ticket, loader, eAssetType::Image, data);
	}

	return ticket;
}

AssetTicket AssetManager::UploadImage(ImageInfo& img_info)
{
	AssetTicket ticket = NewTextureTicket();

	AssertMsg(img_info.ImageData.pData != nullptr, "Image data cannot be null");

	Submit(AssetQueueItem::DirectUploadImage(ticket, img_info));

	// The queued item owns the pixels from here. The caller keeps its pointer (MaterialComponent still tests it to
	// decide whether a component has anything to upload) but is no longer the one that frees it.
	img_info.bOwnsData = false;

	return ticket;
}

AssetTicket AssetManager::SubmitCustom(const AssetCustomFunctionType& fn)
{
	AssetTicket ticket { new AssetCustomFunctionType(fn) };

	AssertMsg(fn != nullptr, "Custom function cannot be null");

	Submit(AssetQueueItem::UserFunction(ticket));

	return ticket;
}

AssetTicket AssetManager::NewTextureTicket()
{
	AssetTicket ticket { gTextureManager->NewTexture() };
	return ticket;
}


/**
 * @brief Creates a 1x1 image of `format` filled with `pixel`, which must hold one pixel's worth of bytes.
 */
static fx::Image* CreateSolidImage(eImageFormat format, const uint8* pixel)
{
	const uint32 pixel_stride = ImageFormatUtil::GetPixelStride(format);

	SizedArray<uint8> pixel_data;
	pixel_data.InitSize(pixel_stride);
	memcpy(pixel_data.pData, pixel, pixel_stride);

	fx::Image* image = gTextureManager->NewTexture();

	// Upload the texture directly
	Slice<const uint8> pixel_slice(pixel_data.pData, pixel_data.Size);
	renderer::GraphicsBackendFwd::SubmitImmediateUploadCmd(
		[image, format, pixel_slice](renderer::CommandBuffer& cmd)
		{
			ImageInfo image_info {
				Vec2u(1, 1), format, 0, 1, pixel_slice,
			};
			image->Upload(cmd, image_info);
		});

	return image;
}

fx::Image* AssetManager::GetNullImage(eImageFormat format)
{
	{
		std::lock_guard<std::mutex> lock(mNullImageMutex);
		fx::Image** existing = mNullImageList.Find(format);
		if (existing != nullptr && (*existing) != nullptr) {
			return *existing;
		}
	}

	// 0xFF so shader multiplies remain neutral (1.0). 0x01 would give near-black.
	uint8 pixel[16];
	memset(pixel, 0xFF, sizeof(pixel));
	Assert(ImageFormatUtil::GetPixelStride(format) <= sizeof(pixel));

	fx::Image* image = CreateSolidImage(format, pixel);

	{
		std::lock_guard<std::mutex> lock(mNullImageMutex);
		// Double-check after creation to avoid race
		fx::Image** existing = mNullImageList.Find(format);
		if (existing != nullptr && (*existing) != nullptr) {
			// Another thread won race, use existing. The newly created image will leak if not freed;
			// but NewTexture is cheap vs complexity. In practice GetNullImage is called rarely.
			return *existing;
		}
		mNullImageList.Insert(format, image);
	}

	return image;
}

fx::Image* AssetManager::GetFlatNormalImage()
{
	{
		std::lock_guard<std::mutex> lock(mNullImageMutex);
		if (mpFlatNormalImage != nullptr) {
			return mpFlatNormalImage;
		}
	}

	// (0.5, 0.5, 1.0) decodes to a tangent space normal of (0, 0, 1), which leaves the vertex normal unchanged
	const uint8 pixel[4] = { 0x80, 0x80, 0xFF, 0xFF };
	fx::Image* image = CreateSolidImage(eImageFormat::RGBA8_UNorm, pixel);

	{
		std::lock_guard<std::mutex> lock(mNullImageMutex);
		// Same race handling as GetNullImage
		if (mpFlatNormalImage == nullptr) {
			mpFlatNormalImage = image;
		}
	}

	return mpFlatNormalImage;
}

AssetTicket AssetManager::GetNullImageTicket(eImageFormat format)
{
	fx::Image* image = GetNullImage(format);

	AssetTicket ticket { image };
	ticket.MarkAndSignalLoaded();

	return ticket;
}


AssetManager* AssetManager::GetInstance() { return gAssetManager; }

} // namespace fx
