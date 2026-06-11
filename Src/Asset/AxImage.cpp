#include "AxImage.hpp"

#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>

namespace fx {


static PagedArray<TSRef<AxImage>> mEmptyImagesPerFormat;

PagedArray<TSRef<AxImage>>& AxImage::GetEmptyImagesArray() { return mEmptyImagesPerFormat; }

AxImage::AxImage(const AxImage& other) { (*this) = other; }

void AxImage::MarkAndSignalLoaded()
{
    IsFinishedNotifier.SignalDataWritten();

    bIsUploadedToGpu = true;
    bIsUploadedToGpu.notify_all();

    mIsLoaded.store(true);
}

AxImage& AxImage::operator=(const AxImage& other)
{
    Image = other.Image;
    ImageType = other.ImageType;
    Image.Size = other.Image.Size;

    return *this;
}


void AxImage::Destroy()
{
    if (!bIsUploadedToGpu.load()) {
        return;
    }

    Image.DecRef();

    bIsUploadedToGpu = false;
}

} // namespace fx
