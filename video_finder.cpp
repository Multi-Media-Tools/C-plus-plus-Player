#include "video_finder.h"
#include "app.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <comdef.h>

#import "C:\\Program Files\\Common Files\\System\\ado\\msado15.dll" no_namespace rename("EOF", "adoEOF")

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

static std::vector<std::thread>     s_workerThreads;
static std::mutex                   s_finderMutex;
static std::vector<VideoItem>       s_sharedVideos;
static std::unordered_set<std::string> s_seenPaths;
static std::atomic<bool>            s_isScanning{ false };
static std::atomic<bool>            s_stopScan{ false };
static std::atomic<uint64_t>        s_version{ 0 };
static std::atomic<int>             s_activeThreads{ 0 };

static std::string FormatBytes(uintmax_t bytes) {
    if (bytes == 0) return "0 B";
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    double size = (double)bytes;
    while (size >= 1024.0 && u < 4) {
        size /= 1024.0;
        ++u;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(u >= 2 ? 1 : 0) << size << " " << units[u];
    return ss.str();
}

static bool IsAllowedVideoExt(const std::string& ext) {
    // Explicitly exclude .ts per requirements
    if (ext == ".ts" || ext == ".d.ts")
        return false;

    static const std::unordered_set<std::string> allowed = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv",
        ".webm", ".m4v", ".m2ts", ".vob", ".3gp",
        ".mpg", ".mpeg", ".divx", ".asf", ".rmvb"
    };
    return allowed.find(ext) != allowed.end();
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], len, nullptr, nullptr);
    return str;
}

static std::string ToLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return (char)::tolower(c); });
    return str;
}


static std::string DetermineCategory(const std::string& path) {
    std::string lower = ToLower(path);
    if (lower.find("\\videos\\") != std::string::npos || lower.find("/videos/") != std::string::npos)
        return "Videos";
    if (lower.find("\\downloads\\") != std::string::npos || lower.find("/downloads/") != std::string::npos)
        return "Downloads";
    if (lower.find("\\documents\\") != std::string::npos || lower.find("/documents/") != std::string::npos)
        return "Documents";
    return "Indexed";
}

static void AddDiscoveredFile(const std::string& fullPath, uintmax_t sizeBytes, const std::string& defaultCat) {
    if (fullPath.empty())
        return;

    std::string lower = ToLower(fullPath);
    
    // Check extension
    size_t dotPos = lower.rfind('.');
    if (dotPos == std::string::npos)
        return;
    std::string ext = lower.substr(dotPos);
    if (!IsAllowedVideoExt(ext))
        return;

    std::string cat = defaultCat.empty() ? DetermineCategory(fullPath) : defaultCat;

    std::error_code ec;
    if (sizeBytes == 0) {
        sizeBytes = fs::file_size(fullPath, ec);
        if (ec) sizeBytes = 0;
    }

    // Extract filename
    size_t slashPos = fullPath.find_last_of("\\/");
    std::string fileName = (slashPos != std::string::npos) ? fullPath.substr(slashPos + 1) : fullPath;

    VideoItem item;
    item.fileName = fileName;
    item.fullPath = fullPath;
    item.category = cat;
    item.fileSizeBytes = sizeBytes;
    item.sizeFormatted = FormatBytes(sizeBytes);

    {
        std::lock_guard<std::mutex> lk(s_finderMutex);
        if (s_seenPaths.insert(lower).second) {
            s_sharedVideos.push_back(std::move(item));
            s_version.fetch_add(1, std::memory_order_release);
        }
    }
    RequestRepaint();
}

static void QueryWindowsIndexWorker(std::wstring whereClause) {
    CoInitialize(NULL);
    {
        try {
            _ConnectionPtr conn;
            HRESULT hr = conn.CreateInstance(__uuidof(Connection));
            if (SUCCEEDED(hr)) {
                conn->Open(L"Provider=Search.CollatorDSO;Extended Properties='Application=Windows';", L"", L"", adConnectUnspecified);

                _RecordsetPtr rs;
                rs.CreateInstance(__uuidof(Recordset));
                std::wstring sql = L"SELECT System.ItemPathDisplay, System.Size FROM SystemIndex WHERE " + whereClause;
                
                rs->Open(sql.c_str(), conn.GetInterfacePtr(), adOpenForwardOnly, adLockReadOnly, adCmdText);

                while (!rs->adoEOF && !s_stopScan.load(std::memory_order_relaxed)) {
                    _variant_t valPath = rs->Fields->Item[L"System.ItemPathDisplay"]->Value;
                    if (valPath.vt == VT_BSTR && valPath.bstrVal != nullptr) {
                        std::wstring wpath(valPath.bstrVal);
                        std::string path = WideToUtf8(wpath);

                        uintmax_t sizeBytes = 0;
                        _variant_t valSize = rs->Fields->Item[L"System.Size"]->Value;
                        if (valSize.vt != VT_NULL && valSize.vt != VT_EMPTY) {
                            sizeBytes = (uintmax_t)(double)valSize;
                        }

                        AddDiscoveredFile(path, sizeBytes, "");
                    }
                    rs->MoveNext();
                }
                rs->Close();
                conn->Close();
            }
        }
        catch (...) {
            // Windows Search index may be busy or query not supported
        }
    }
    CoUninitialize();

    if (s_activeThreads.fetch_sub(1) <= 1) {
        s_isScanning.store(false, std::memory_order_release);
        RequestRepaint();
    }
}

static void ScanDirectoryWorker(std::wstring rootPath, std::string category) {
    std::error_code ec;
    if (fs::exists(rootPath, ec) && fs::is_directory(rootPath, ec)) {
        auto it = fs::recursive_directory_iterator(
            rootPath,
            fs::directory_options::skip_permission_denied,
            ec
        );
        auto end = fs::recursive_directory_iterator();

        for (; it != end && !s_stopScan.load(std::memory_order_relaxed); it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
                continue;

            auto path = it->path();
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return (char)::tolower(c); });

            if (IsAllowedVideoExt(ext)) {
                AddDiscoveredFile(path.string(), it->file_size(ec), category);
            }
        }
    }

    if (s_activeThreads.fetch_sub(1) <= 1) {
        s_isScanning.store(false, std::memory_order_release);
        RequestRepaint();
    }
}

static std::wstring GetFolder(REFKNOWNFOLDERID rfid) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, NULL, &path))) {
        std::wstring res(path);
        CoTaskMemFree(path);
        return res;
    }
    return L"";
}

void VideoFinder_Init() {
    VideoFinder_StartScan();
}

void VideoFinder_StartScan() {
    if (s_isScanning.load(std::memory_order_relaxed))
        return;

    VideoFinder_Shutdown();

    {
        std::lock_guard<std::mutex> lk(s_finderMutex);
        s_sharedVideos.clear();
        s_seenPaths.clear();
        s_version.fetch_add(1, std::memory_order_release);
    }

    s_stopScan.store(false, std::memory_order_release);
    s_isScanning.store(true, std::memory_order_release);

    // Multithreaded queries: Windows Search Indexer queries + Directory crawlers
    std::vector<std::wstring> indexQueries = {
        L"System.FileExtension = '.mkv'",
        L"System.FileExtension = '.mp4'",
        L"System.FileExtension = '.avi' OR System.FileExtension = '.mov'",
        L"System.FileExtension = '.wmv' OR System.FileExtension = '.flv' OR System.FileExtension = '.webm'",
        L"System.FileExtension = '.m4v' OR System.FileExtension = '.m2ts'",
        L"System.FileExtension = '.vob' OR System.FileExtension = '.3gp' OR System.FileExtension = '.mpg' OR System.FileExtension = '.mpeg'"
    };

    int totalJobs = (int)indexQueries.size() + 3; // + Videos, Downloads, Documents
    s_activeThreads.store(totalJobs);

    for (const auto& q : indexQueries) {
        s_workerThreads.emplace_back(QueryWindowsIndexWorker, q);
    }

    // Parallel directory fallback crawlers (ensure non-indexed files in user folders are also captured)
    std::wstring vidFolder = GetFolder(FOLDERID_Videos);
    if (!vidFolder.empty())
        s_workerThreads.emplace_back(ScanDirectoryWorker, vidFolder, "Videos");
    else
        s_activeThreads.fetch_sub(1);

    std::wstring downFolder = GetFolder(FOLDERID_Downloads);
    if (!downFolder.empty())
        s_workerThreads.emplace_back(ScanDirectoryWorker, downFolder, "Downloads");
    else
        s_activeThreads.fetch_sub(1);

    std::wstring docFolder = GetFolder(FOLDERID_Documents);
    if (!docFolder.empty())
        s_workerThreads.emplace_back(ScanDirectoryWorker, docFolder, "Documents");
    else
        s_activeThreads.fetch_sub(1);

    RequestRepaint();
}

void VideoFinder_StopScan() {
    s_stopScan.store(true, std::memory_order_release);
}

void VideoFinder_Shutdown() {
    s_stopScan.store(true, std::memory_order_release);
    for (auto& t : s_workerThreads) {
        if (t.joinable())
            t.join();
    }
    s_workerThreads.clear();
    s_isScanning.store(false, std::memory_order_release);
}

bool VideoFinder_IsScanning() {
    return s_isScanning.load(std::memory_order_relaxed);
}

bool VideoFinder_GetSnapshot(uint64_t& inOutVersion, std::vector<VideoItem>& outSnapshot) {
    uint64_t curVer = s_version.load(std::memory_order_acquire);
    if (curVer != inOutVersion) {
        std::lock_guard<std::mutex> lk(s_finderMutex);
        outSnapshot = s_sharedVideos;
        inOutVersion = curVer;
        return true;
    }
    return false;
}
