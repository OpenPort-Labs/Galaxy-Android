#include "gx_record.h"

namespace gpu {

std::unique_ptr<Frame> FrameQueue::publish(std::unique_ptr<Frame> f) {
    Frame* raw = f.release();
    std::shared_ptr<const Frame> shared(raw, [this](const Frame* p) {
        std::lock_guard<std::mutex> lock(mLock);
        mFree.emplace_back(const_cast<Frame*>(p));
    });
    std::unique_ptr<Frame> next;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mLatestNumber.store(raw->number);
        mLatest.swap(shared);
        if (!mFree.empty()) {
            next = std::move(mFree.back());
            mFree.pop_back();
        }
    }
    shared.reset();
    if (void (*listener)() = mListener.load()) {
        listener();
    }
    if (!next) {
        next.reset(new Frame());
    }
    next->clear();
    return next;
}

std::shared_ptr<const Frame> FrameQueue::latest() {
    std::lock_guard<std::mutex> lock(mLock);
    return mLatest;
}

FrameQueue& frameQueue() {
    static FrameQueue q;
    return q;
}

}  // namespace gpu
