#include "AxManager.hpp"

#include "AxImage.hpp"
#include "Core/Assert.hpp"
#include "Core/SizedArray.hpp"
#include "Loader/Image/LoaderJpeg.hpp"
#include "Loader/Image/LoaderStb.hpp"
#include "Loader/Object/LoaderGltf.hpp"

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <Object/Object.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace fx {

static constexpr uint32 scMaxWorkerThreads = 10;

static constexpr std::chrono::seconds scTimeUntilSleep = std::chrono::seconds(3);


/////////////////////////////////////
// Asset Deletion Ticket
/////////////////////////////////////

void AssetDeletionTicket::DeleteImmediate() const
{
	switch (Type) {
	case eType::None:
		break;

	case eType::Buffer: {
		const BufferTicket& ticket = Value.Ticket;
		vmaDestroyBuffer(renderer::gRenderer->GpuAllocator, ticket.Buffer, ticket.Allocation);
	} break;
	}
}


bool AssetDeletionTicket::TryDelete(uint32 current_tick) const
{
	const bool missed_or_overflow = ((MinDeletionTick - current_tick) > (UINT32_MAX - 10000));

	if (current_tick >= MinDeletionTick || missed_or_overflow) {
		DeleteImmediate();
		return true;
	}

	return false;
}


////////////////////////////////////
// Asset Worker
////////////////////////////////////

void AxWorker::Create()
{
	Thread = std::thread([this]() { this->Update(); });
}

void AxWorker::LoadObject(LockContext<AssetItemData>& asset_data)
{
	TSRef<loader::ObjectLoaderBase> object_loader(asset_data->pLoader);

	switch (Item.AssetLoadOp) {
	case fx::eAssetLoadOp::ReadAndUpload:
		LoadStatus = object_loader->Load(asset_data->ObjectTicket, String(Item.Path));
		break;
	case fx::eAssetLoadOp::ProcessAndUpload:
		LoadStatus = object_loader->Load(asset_data->ObjectTicket, Item.pcRawData, Item.DataSize);
		std::free(static_cast<void*>(const_cast<uint8*>(Item.pcRawData)));
		break;
	default:
		LogError(LC_ASSET, "Unknown asset source!");
	}
}

void AxWorker::LoadImage(const LockContext<AssetItemData>& asset_data)
{
	TSRef<loader::ImageLoaderBase> image_loader(asset_data->pLoader);

	switch (Item.AssetLoadOp) {
	case fx::eAssetLoadOp::ReadAndUpload:
		LoadStatus = image_loader->Load(asset_data->pAsset, Item.Path);
		break;
	case fx::eAssetLoadOp::ProcessAndUpload:
		LoadStatus = image_loader->Load(asset_data->pAsset, Item.pcRawData, Item.DataSize);
		std::free(static_cast<void*>(const_cast<uint8*>(Item.pcRawData)));
		break;
	default:
		LogError(LC_ASSET, "Unknown asset source!");
	}
}


void AxWorker::Update()
{
	while (bRunning.load()) {
		ItemReady.Wait();
		ItemReady.Reset();

		// In order to stop the worker, we need to kill the ItemReady notifier. Here we need to check if the worker
		// actually recieved data. This is also cheaper than calling ItemReady.IsKilled, but that shouldn't be a
		// bottleneck anyway.
		if (!bRunning.load()) {
			break;
		}

		// Retrieve the loader and asset from the item
		LockContext<AssetItemData> asset_data = Item.GetDataContext();

		AssertMsg(Item.AssetLoadOp != eAssetLoadOp::None, "No asset load op set!");

		// Directly upload to GPU
		if (Item.AssetLoadOp == eAssetLoadOp::DirectUpload) {
			LoadStatus = loader::eLoaderStatus::Success;
		}
		else {
			if (Item.IsObject()) {
				LoadObject(asset_data);
			}
			else if (Item.IsImage()) {
				LoadImage(asset_data);
			}
		}

		// Mark that we are waiting for the data to be uploaded to the GPU
		bDataPendingUpload.test_and_set();
		gAssetManager->SignalUpdate();
	}
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void AxManager::Start(int32 min_threads)
{
	AssertMsg(mbActive.test() == false, "Asset manager is already created!");

	mMinThreads = min_threads;
	mbActive.test_and_set();


	// Allocate the workers
	mWorkerThreads.InitCapacity(scMaxWorkerThreads);

	for (int32 i = 0; i < mMinThreads; i++) {
		// 'Insert' a new worker and get its pointer
		AxWorker* worker = mWorkerThreads.Insert();

		// Create the worker from the newly inserted pointer
		worker->Create();
	}

	WorkersWaitingToUpload.InitSize(scMaxWorkerThreads);

	mpAssetManagerThread = new std::thread([this]() { AxManager::AssetManagerUpdate(); });
}

void AxManager::DebugPrintWorkers() const
{
	for (const AxWorker& worker : mWorkerThreads) {
		worker.DebugPrint();
	}
}

void AxManager::Shutdown()
{
	LogInfo(LC_ASSET, "Shutting down asset manager...");

	if (!mbActive.test()) {
		return;
	}

	// Cleanup all permutations of empty images that were created.
	PagedArray<AxImage>& empty_images_list = AxImage::GetEmptyImagesArray();

	// if (empty_images_list.IsInited()) {
	//     for (TSRef<AxImage>& image_ref : empty_images_list) {
	//         image_ref.DestroyRef();
	//     }
	// }


	mbActive.clear();
	ManagerUpdateNotifier.Kill();

	for (auto& worker : mWorkerThreads) {
		worker.Kill();
		worker.Thread.join();
	}

	mpAssetManagerThread->join();
	delete mpAssetManagerThread;

	mWorkerThreads.Free();
}

void AxManager::ShutdownDeletionQueue()
{
	SpinLockContext<Queue<AssetDeletionTicket>> queue = mDeletionTickets.GetQueue();

	while (!queue->IsEmpty()) {
		queue->First().DeleteImmediate();
		queue->Pop();
	}
}


inline bool IsMemoryJpeg(const uint8* data, uint32 data_size)
{
	if (data_size < 120) {
		return false;
	}

	const bool header_correct = (data[0] == 0xFF) && (data[1] == 0xD8);
	const bool footer_correct = (data[data_size - 2] == 0xFF) && (data[data_size - 1] == 0xD9);

	return header_correct && footer_correct;
}


inline bool IsFileJpeg(const std::string& path)
{
	const char* path_cstr = path.c_str();

	FILE* fp = fopen(path_cstr, "rb");

	if (fp == nullptr) {
		return false;
	}

	constexpr size_t magic_size = sizeof(uint16);
	uint8 magic_buffer[2];

	if (fread(magic_buffer, 1, magic_size, fp) != magic_size) {
		fclose(fp);
		return false;
	}

	fclose(fp);

	if (magic_buffer[0] == 0xFF && magic_buffer[1] == 0xD8) {
		return true;
	}

	return false;
}

void AxManager::LoadObject(const AssetTicket<Object>& ticket, const std::string& path, LoadObjectOptions options)
{
	TSRef<loader::LoaderGltf> loader = TSRef<loader::LoaderGltf>::New();
	loader->bKeepInMemory = options.bKeepInMemory || options.bGeneratePhysicsMesh;

	SubmitLoadObject<loader::LoaderGltf>(ticket, loader, path);
}


void AxManager::LoadObjectFromMemory(const AssetTicket<Object>& ticket, const uint8* data, uint32 data_size)
{
	TSRef<loader::LoaderGltf> loader = TSRef<loader::LoaderGltf>::New();

	SubmitLoadObject<loader::LoaderGltf>(ticket, loader, Slice<const uint8>(data, data_size));
}


void AxManager::LoadImage(eImageType image_type, eImageFormat format, TSRef<AxImage>& asset, const std::string& path,
						  eImageCreateFlags flags)
{
	bool is_jpeg = IsFileJpeg(path);

	// Use TurboJPEG if this is a JPEG file.
	if (is_jpeg) {
		TSRef<loader::LoaderJpeg> loader = TSRef<loader::LoaderJpeg>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromPath<AxImage, loader::LoaderJpeg, eAssetLoadType::Image>(asset, loader, path);
	}
	// Use STB image otherwise.
	else {
		TSRef<loader::LoaderStb> loader = TSRef<loader::LoaderStb>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;
		loader->CreationFlags = flags;

		SubmitLoadAssetFromPath<AxImage, loader::LoaderStb, eAssetLoadType::Image>(asset, loader, path);
	}
}


void AxManager::LoadImageFromMemory(eImageType image_type, eImageFormat format, TSRef<AxImage>& asset,
									const uint8* data, uint32 data_size)
{
	if (IsMemoryJpeg(data, data_size)) {
		// Load the image using turbojpeg
		TSRef<loader::LoaderJpeg> loader = TSRef<loader::LoaderJpeg>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;

		SubmitLoadAssetFromData<AxImage, loader::LoaderJpeg, eAssetLoadType::Image>(
			asset, loader, Slice<const uint8>(data, data_size));
	}
	else {
		// Load the image using stb_image
		TSRef<loader::LoaderStb> loader = TSRef<loader::LoaderStb>::New();
		loader->ImageType = image_type;
		loader->ImageFormat = format;

		SubmitLoadAssetFromData<AxImage, loader::LoaderStb, eAssetLoadType::Image>(asset, loader,
																				   Slice<const uint8>(data, data_size));
	}
}

void AxManager::LoadImageFromPixels(TSRef<AxImage>& asset, const ImageInfo& img_info)
{
	SubmitImageToUpload<AxImage, eAssetLoadType::Image>(asset, img_info);
}

static void DoDirectUpload(AxQueueItem& item, AssetItemData& asset_data)
{
	ImageInfo img_info = item.ImgInfo;

	TSRef<AxImage> image(asset_data.pAsset);

	if (image->Image.IsInited()) {
		// The image already exists, upload to a new mip in the same image.
		image->Image.UploadMip(renderer::RenderBackendFwd::GetUploadCmd(), img_info.MipLevel, img_info.Size,
							   img_info.ImageData);
	}
	else {
		// The image does not exist yet, upload directly as the base mip.
		image->Image.CreateFromData(renderer::RenderBackendFwd::GetUploadCmd(), img_info, eImageCreateFlags::None);
	}

	image->bIsUploadedToGpu = true;
	image->bIsUploadedToGpu.notify_all();
}

int32 AxManager::CheckForUploadableData()
{
	using namespace renderer;

	int32 num_uploads = 0;

	WorkersWaitingToUpload.Clear();

	// Check with the workers to see if there is anything to load onto the GPU
	for (AxWorker& worker : mWorkerThreads) {
		if (!worker.bDataPendingUpload.test()) {
			continue;
		}

		WorkersWaitingToUpload.Insert(&worker);
	}

	const bool should_upload = WorkersWaitingToUpload.Size > 0;

	if (!should_upload) {
		return false;
	}

	// Begin GPU upload
	if (should_upload) {
		gRenderer->UploadContext.UploadFence.WaitFor();
		gRenderer->UploadContext.UploadFence.Reset();

		gRenderer->UploadContext.CmdBuffer.Record();
	}

	for (AxWorker* worker : WorkersWaitingToUpload) {
		LockContext<AssetItemData> asset_data = worker->Item.GetDataContext();

		// The asset was successfully loaded, upload to GPU
		if (worker->LoadStatus == loader::eLoaderStatus::Success) {
			if (worker->Item.AssetLoadOp == eAssetLoadOp::DirectUpload) {
				DoDirectUpload(worker->Item, asset_data.Get());
				++num_uploads;
			}
			else if (asset_data->pLoader.IsValid()) {
				asset_data->CreateGpuResource();
				++num_uploads;
			}
		}
	}

	SpinLockContext<VkQueue> transfer_queue = gRenderer->GetDevice()->GetTransferQueue();

	if (should_upload) {
		CommandBuffer& cmd = gRenderer->UploadContext.CmdBuffer;
		cmd.End();

		const VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.commandBufferCount = 1,
			.pCommandBuffers = &cmd.Cmd,
		};

		VkQueue vk_xfer_queue = transfer_queue.Get();

		AssertMsg(vk_xfer_queue != nullptr, "Queue has not been initialized");
		vkQueueSubmit(vk_xfer_queue, 1, &submit_info, gRenderer->UploadContext.UploadFence.Get());
	}

	transfer_queue.Unlock();

	// Finally, notify the asset that it is loaded and tell the workers they are free

	for (AxWorker* worker : WorkersWaitingToUpload) {
		LockContext<AssetItemData> asset_data = worker->Item.GetDataContext();

		if (worker->LoadStatus == loader::eLoaderStatus::Success) {
			if (asset_data->LoadType == eAssetLoadType::Object) {
				AssetTicketData* ticket_data = asset_data->ObjectTicket.pTicketData;

				fx::Object* object_data = asset_data->ObjectTicket.Get();

				while (!ticket_data->bIsUploadedToGpu) {
					ticket_data->bIsUploadedToGpu.wait(false);
				}

				{
					std::lock_guard guard(ticket_data->mCallbackMutex);

					// Call OnLoaded callbacks if they are attached
					if (!ticket_data->mOnLoadedCallbacks.empty()) {
						for (auto& callback : ticket_data->mOnLoadedCallbacks) {
							callback(reinterpret_cast<void*>(object_data));
						}
					}

					ticket_data->mOnLoadedCallbacks.clear();
					ticket_data->IsFinishedNotifier.Signal();

					// Notify the asset thread that loading is finished
					ticket_data->bIsLoaded.store(true);
				}
			}
			else {
				while (!asset_data->pAsset->bIsUploadedToGpu) {
					asset_data->pAsset->bIsUploadedToGpu.wait(false);
				}

				// Notify the asset thread that loading is finished

				asset_data->pAsset->IsFinishedNotifier.Signal();
				asset_data->pAsset->mIsLoaded.store(true);
			}

			if (asset_data->pLoader.IsValid()) {
				// Destroy the loader(clearing the loading buffers)
				asset_data->DestroyLoader();
			}
		}
		else if (worker->LoadStatus == loader::eLoaderStatus::Error) {
			asset_data->pAsset->IsFinishedNotifier.Signal();

			if (asset_data->LoadType == eAssetLoadType::Object && asset_data->ObjectTicket.pTicketData) {
				AssetTicketData* ticket_data = asset_data->ObjectTicket.pTicketData;

				// There was an error, call the OnError callback if it was registered
				if (ticket_data->mOnErrorCallback) {
					ticket_data->mOnErrorCallback();
				}
			}
		}
		else if (worker->LoadStatus == loader::eLoaderStatus::None) {
			asset_data->pAsset->IsFinishedNotifier.Signal();
			Panic("AssetManager", "Worker status is none!");
		}

		worker->bDataPendingUpload.clear();
		worker->LoadStatus = loader::eLoaderStatus::None;
		worker->bIsBusy.clear();
	}

	return num_uploads;
}

bool AxManager::CheckWorkersBusy()
{
	for (auto& worker : mWorkerThreads) {
		if (worker.bIsBusy.test()) {
			return true;
		}
	}
	return false;
}

void AxManager::AddWorkerThread()
{
	// At the maximum number of workers, break
	if (mWorkerThreads.Size >= mWorkerThreads.Capacity) {
		LogError(LC_ASSET, "Reached maximum number of worker threads");
		return;
	}

	AxWorker* worker = mWorkerThreads.Insert();
	worker->Create();
}

int32 AxManager::CheckForItemsToLoad()
{
	uint32 num_loads = mLoadQueue.Size();

	if (num_loads < 1) {
		return num_loads;
	}

	AxWorker* worker = FindWorkerThread();

	// No workers available currently, defer the item loading.
	// Even though there is no worker available, we still return true as there is an item in the queue.
	if (worker == nullptr) {
		return num_loads;
	}

	AxQueueItem item;
	if (!mLoadQueue.PopIfAvailable(&item)) {
		// The load queue is currently in use(uploaded to), skip for now.
		return num_loads;
	}

	if (worker) {
		if (worker->bIsBusy.test()) {
			// Return to sender
			mLoadQueue.Push(std::move(item));
			return true;
		}

		// Set busy
		while (worker->bIsBusy.test_and_set()) {
			worker->bIsBusy.wait(true);
		}

		// Submit the item we want to load
		worker->SubmitItemToLoad(std::move(item));
	}

	return num_loads;
}

int32 AxManager::CheckForItemsToDelete()
{
	int32 num_deletes = 0;

	SpinLockContext<Queue<fx::AssetDeletionTicket>> queue = mDeletionTickets.GetQueue();

	if (queue->IsEmpty()) {
		return 0;
	}

	// Wait for all uploads to finish. We cannot be actively loading the item we are deleting!
	renderer::gRenderer->UploadContext.UploadFence.WaitFor();

	if (queue->First().TryDelete(mTickCounter)) {
		++num_deletes;
		queue->Pop();
	}

	return num_deletes;
}

void AxManager::AssetManagerUpdate()
{
	while (mbActive.test()) {
		// bool is_busy = CheckWorkersBusy();

		// If any of the workers are still marked as busy, there is data
		// either to be uploaded or is currently being loaded.
		// if (is_busy) {
		//     // Loop while we are busy to check for when we are pending upload, as well
		//     // as check for new arrivals.
		//     while (CheckWorkersBusy() && mbActive.test()) {
		//         // Check if there is data to be uploaded to the GPU
		//         CheckForUploadableData();

		//         // Check if there are any items that can be loaded by a worker
		//         CheckForItemsToLoad();

		//         std::this_thread::sleep_for(std::chrono::milliseconds(80));
		//     }
		// }

		// There are no busy workers remaining, wait for the next item to be enqueued.

		if (mbShouldSleep) {
			ManagerUpdateNotifier.Wait();
			mbShouldSleep = false;
		}

		// std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if (!mbActive.test()) {
			break;
		}

		const int32 num_uploads = CheckForUploadableData();
		const int32 num_deletes = CheckForItemsToDelete();
		const int32 num_loads = CheckForItemsToLoad();


		const int32 num_operations = num_uploads + num_deletes + num_loads;
		ManagerUpdateNotifier.Discard(num_operations);

		if (num_operations > 0) {
			mbIsTimeSet = false;
			continue;
		}

		uint32 tick = mTickCounter.fetch_add(1);

		{
			// If this is the first time that there has been no activity, set the current timestamp.
			if (!mbIsTimeSet) {
				mbIsTimeSet = true;
				mLastActiveTime = std::chrono::system_clock::now();
			}
		}

		// If the time since last activity is greater than scTimeUntilSleep, then sleep the asset manager.
		// To wake it back up, ManagerUpdateNotifier will need to be signalled.
		std::chrono::system_clock::duration time_since_active = std::chrono::system_clock::now() - mLastActiveTime;

		if (time_since_active >= scTimeUntilSleep) {
			mbShouldSleep = true;
			LogInfo("Sleeping asset manager...");
			continue;
		}

		// Throttle back if there is nothing to do
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

AxWorker* AxManager::FindWorkerThread()
{
	uint32 worker_id = 0;
	for (AxWorker& worker : mWorkerThreads) {
		if (!worker.bIsBusy.test()) {
			LogInfo(LC_ASSET, "Found worker (id={})", worker_id);
			return &worker;
		}
		++worker_id;
	}

	// Did not find any open worker, return null to be handled by the caller.
	return nullptr;
}

AxManager* AxManager::GetInstance() { return gAssetManager; }

} // namespace fx
