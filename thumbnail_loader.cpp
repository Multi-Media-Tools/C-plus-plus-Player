#include "thumbnail_loader.h"
#include "ffmpeg_wrapper.h"
#include "app.h"

#include <d3d11.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>

struct DecodedImage {
    std::string path;
    int width = 0;
    int height = 0;
    float aspectRatio = 1.0f;
    std::vector<uint8_t> rgba;
    bool success = false;
};

struct CacheEntry {
    ID3D11ShaderResourceView* srv = nullptr;
    float aspectRatio = 1.0f;
};

static std::thread                      s_thumbThread;
static std::mutex                       s_queueMutex;
static std::condition_variable          s_queueCv;
static std::deque<std::string>          s_pendingQueue;
static std::unordered_set<std::string>  s_queuedOrProcessed;
static std::atomic<bool>                s_thumbRunning{ false };

static std::mutex                       s_finishedMutex;
static std::vector<DecodedImage>        s_finishedImages;

static std::unordered_map<std::string, CacheEntry> s_cache;
static std::unordered_set<std::string>  s_failedPaths;

static bool ExtractVideoThumbnail(const std::string& path, DecodedImage& outImg) {
    outImg.path = path;
    outImg.success = false;
    outImg.aspectRatio = 1.0f;

    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    int videoStreamIdx = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = (int)i;
            break;
        }
    }

    if (videoStreamIdx == -1) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVCodecParameters* codecPar = fmtCtx->streams[videoStreamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx, codecPar) < 0 ||
        avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Seek ~10% into video to avoid black intro frames
    if (fmtCtx->duration > 0) {
        int64_t targetTimestamp = fmtCtx->duration / 10;
        av_seek_frame(fmtCtx, -1, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool gotFrame = false;

    // Read frames until a video frame is decoded
    int attempts = 0;
    while (av_read_frame(fmtCtx, packet) >= 0 && attempts < 120) {
        if (packet->stream_index == videoStreamIdx) {
            if (avcodec_send_packet(codecCtx, packet) >= 0) {
                if (avcodec_receive_frame(codecCtx, frame) >= 0) {
                    gotFrame = true;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
        attempts++;
    }

    if (!gotFrame) {
        // Flush decoder
        avcodec_send_packet(codecCtx, nullptr);
        if (avcodec_receive_frame(codecCtx, frame) >= 0) {
            gotFrame = true;
        }
    }

    if (gotFrame && frame->width > 0 && frame->height > 0) {
        outImg.aspectRatio = (float)frame->width / (float)frame->height;
        const int maxDim = 320;
        int targetW = maxDim;
        int targetH = maxDim;

        if (outImg.aspectRatio >= 1.0f) {
            targetH = std::max(1, (int)(maxDim / outImg.aspectRatio));
        } else {
            targetW = std::max(1, (int)(maxDim * outImg.aspectRatio));
        }

        if (targetW % 2 != 0) targetW++;
        if (targetH % 2 != 0) targetH++;

        SwsContext* swsCtx = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            targetW, targetH, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (swsCtx) {
            outImg.width = targetW;
            outImg.height = targetH;
            outImg.rgba.resize(targetW * targetH * 4);

            uint8_t* dstData[4] = { outImg.rgba.data(), nullptr, nullptr, nullptr };
            int dstLinesize[4] = { targetW * 4, 0, 0, 0 };

            sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
            sws_freeContext(swsCtx);
            outImg.success = true;
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    return outImg.success;
}

static void ThumbnailWorkerThread() {
    while (s_thumbRunning.load(std::memory_order_relaxed)) {
        std::string currentPath;
        {
            std::unique_lock<std::mutex> lk(s_queueMutex);
            s_queueCv.wait(lk, []() {
                return !s_pendingQueue.empty() || !s_thumbRunning.load(std::memory_order_relaxed);
            });

            if (!s_thumbRunning.load(std::memory_order_relaxed))
                break;

            currentPath = std::move(s_pendingQueue.front());
            s_pendingQueue.pop_front();
        }

        DecodedImage img;
        bool ok = ExtractVideoThumbnail(currentPath, img);
        img.success = ok;

        {
            std::lock_guard<std::mutex> lk(s_finishedMutex);
            s_finishedImages.push_back(std::move(img));
        }

        RequestRepaint();
    }
}

void ThumbnailLoader_Init() {
    if (s_thumbRunning.load())
        return;

    s_thumbRunning.store(true);
    s_thumbThread = std::thread(ThumbnailWorkerThread);
}

void ThumbnailLoader_Shutdown() {
    s_thumbRunning.store(false);
    s_queueCv.notify_all();

    if (s_thumbThread.joinable())
        s_thumbThread.join();

    {
        std::lock_guard<std::mutex> lk(s_queueMutex);
        s_pendingQueue.clear();
        s_queuedOrProcessed.clear();
    }

    {
        std::lock_guard<std::mutex> lk(s_finishedMutex);
        s_finishedImages.clear();
    }

    for (auto& pair : s_cache) {
        if (pair.second.srv) {
            pair.second.srv->Release();
        }
    }
    s_cache.clear();
    s_failedPaths.clear();
}

void ThumbnailLoader_Update(ID3D11Device* device) {
    if (!device) return;

    std::vector<DecodedImage> localFinished;
    {
        std::lock_guard<std::mutex> lk(s_finishedMutex);
        if (s_finishedImages.empty())
            return;
        localFinished = std::move(s_finishedImages);
        s_finishedImages.clear();
    }

    for (const auto& item : localFinished) {
        if (!item.success || item.rgba.empty()) {
            s_failedPaths.insert(item.path);
            continue;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = item.width;
        desc.Height = item.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = item.rgba.data();
        initData.SysMemPitch = item.width * 4;

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &initData, &tex);
        if (SUCCEEDED(hr) && tex) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* srv = nullptr;
            hr = device->CreateShaderResourceView(tex, &srvDesc, &srv);
            tex->Release();

            if (SUCCEEDED(hr) && srv) {
                CacheEntry entry;
                entry.srv = srv;
                entry.aspectRatio = item.aspectRatio;
                s_cache[item.path] = entry;
            } else {
                s_failedPaths.insert(item.path);
            }
        } else {
            s_failedPaths.insert(item.path);
        }
    }
}

void ThumbnailLoader_Request(const std::string& fullPath) {
    if (fullPath.empty()) return;
    if (s_cache.find(fullPath) != s_cache.end()) return;
    if (s_failedPaths.find(fullPath) != s_failedPaths.end()) return;

    {
        std::lock_guard<std::mutex> lk(s_queueMutex);
        if (s_queuedOrProcessed.insert(fullPath).second) {
            s_pendingQueue.push_back(fullPath);
            s_queueCv.notify_one();
        }
    }
}

ID3D11ShaderResourceView* ThumbnailLoader_Get(const std::string& fullPath, ID3D11Device* device, float* outAspectRatio) {
    ThumbnailLoader_Update(device);

    auto it = s_cache.find(fullPath);
    if (it != s_cache.end()) {
        if (outAspectRatio) *outAspectRatio = it->second.aspectRatio;
        return it->second.srv;
    }

    if (s_failedPaths.find(fullPath) == s_failedPaths.end()) {
        ThumbnailLoader_Request(fullPath);
    }
    if (outAspectRatio) *outAspectRatio = 1.0f;
    return nullptr;
}
