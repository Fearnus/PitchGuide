#pragma once

#include <mutex>

// GPU execution provider types for ONNX Runtime inference
enum class GPUProvider
{
    CPU = 0,
    CUDA,     // NVIDIA GPU
    DirectML, // Windows DirectX 12 (AMD/Intel/NVIDIA)
    CoreML    // Apple Neural Engine / GPU (macOS/iOS)
};

/**
 * ONNX Runtime DirectML 1.18/1.19 can crash inside the GPU driver when several
 * sessions for one device are created at the same time. Everything in this
 * binary that creates a DirectML session holds this lock while it does.
 */
inline std::mutex &directMLSessionCreationMutex()
{
    static std::mutex mutex;
    return mutex;
}
