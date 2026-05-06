# C++ 简单内存池实现 (Frame Pool)

> **设计目标**：
> 1. 避免频繁分配/释放大块内存（如音视频 Frame）。
> 2. 线程安全（支持多线程获取和归还）。
> 3. 结合 `std::shared_ptr` 和自定义删除器，实现自动回收。

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>

// 模拟一个占用较大内存的音视频帧
class Frame {
public:
    int width;
    int height;
    std::vector<uint8_t> data; // 模拟像素数据

    Frame(int w, int h) : width(w), height(h) {
        // 模拟分配大块内存，例如 1920x1080 RGBA
        data.resize(w * h * 4); 
        std::cout << "Frame constructed. Address: " << this << std::endl;
    }

    ~Frame() {
        std::cout << "Frame destructed. Address: " << this << std::endl;
    }

    // 重置状态，供下次复用
    void reset() {
        // 实际业务中可能不需要清空数据，只需重置宽高、PTS等元数据即可
        // std::fill(data.begin(), data.end(), 0); 
    }
};

// 线程安全的 Frame 内存池
class FramePool : public std::enable_shared_from_this<FramePool> {
private:
    std::queue<Frame*> freeList;
    std::mutex mtx;
    std::condition_variable cv;
    
    int width_;
    int height_;
    size_t capacity_;
    size_t currentSize_;

public:
    FramePool(int w, int h, size_t capacity) 
        : width_(w), height_(h), capacity_(capacity), currentSize_(0) {}

    ~FramePool() {
        std::lock_guard<std::mutex> lock(mtx);
        while (!freeList.empty()) {
            delete freeList.front();
            freeList.pop();
        }
    }

    // 获取一个 Frame，返回 shared_ptr
    std::shared_ptr<Frame> acquire() {
        Frame* rawFrame = nullptr;

        {
            std::unique_lock<std::mutex> lock(mtx);
            
            // 如果池子空了，且还没达到最大容量，则新建
            if (freeList.empty() && currentSize_ < capacity_) {
                rawFrame = new Frame(width_, height_);
                currentSize_++;
            } 
            // 如果池子空了，且达到了最大容量，则阻塞等待别人归还
            else if (freeList.empty() && currentSize_ >= capacity_) {
                cv.wait(lock, [this] { return !freeList.empty(); });
                rawFrame = freeList.front();
                freeList.pop();
            } 
            // 池子不为空，直接取
            else {
                rawFrame = freeList.front();
                freeList.pop();
            }
        }

        // 关键：使用 shared_ptr 包装，并传入自定义删除器 (Deleter)
        // 注意：这里需要捕获 pool 的 shared_ptr，防止池子比 Frame 先析构
        std::shared_ptr<FramePool> poolPtr = shared_from_this();
        
        return std::shared_ptr<Frame>(rawFrame, [poolPtr](Frame* f) {
            poolPtr->recycle(f);
        });
    }

private:
    // 回收 Frame 到池中 (由 shared_ptr 的 Deleter 自动调用)
    void recycle(Frame* f) {
        f->reset(); // 重置状态
        
        std::lock_guard<std::mutex> lock(mtx);
        freeList.push(f);
        cv.notify_one(); // 唤醒可能在等待 acquire 的线程
        
        std::cout << "Frame recycled to pool. Address: " << f << std::endl;
    }
};

// ================= 测试代码 =================

void testFramePool() {
    // 必须用 shared_ptr 管理 Pool，因为 enable_shared_from_this 需要
    auto pool = std::make_shared<FramePool>(1920, 1080, 2); 

    std::cout << "--- Acquiring Frame 1 ---" << std::endl;
    auto frame1 = pool->acquire();
    
    std::cout << "--- Acquiring Frame 2 ---" << std::endl;
    auto frame2 = pool->acquire();
    
    std::cout << "--- Frame 1 leaves scope ---" << std::endl;
    {
        // 模拟 frame1 传递给其他函数/线程，引用计数增加
        auto frame1_copy = frame1; 
        std::cout << "frame1 use_count: " << frame1.use_count() << std::endl;
    } // frame1_copy 离开作用域，引用计数减1，但还不为0，不会回收

    frame1.reset(); // 手动释放 frame1，引用计数归零，触发 recycle

    std::cout << "--- Acquiring Frame 3 ---" << std::endl;
    // 此时池子容量已达上限(2)，但 frame1 刚被回收，所以 frame3 会复用 frame1 的内存
    auto frame3 = pool->acquire(); 
    
    std::cout << "--- End of test ---" << std::endl;
}

int main() {
    testFramePool();
    return 0;
}
```