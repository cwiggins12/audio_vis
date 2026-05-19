#pragma once

#if defined(__linux__) || defined(_WIN32)

#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/inotify.h>
    #include <sys/poll.h>
    #include <unistd.h>
    #include <unordered_map>
#endif

enum class WatchEventType {
    SHADER_CHANGED,     // frag.glsl or spec.cfg modified: full reloadPreset
    TEXTURE_CHANGED,    // texture file modified: rebuild that slot only
    FONT_CHANGED,       // font file modified: rebuild that slot only
    PRESET_ADDED,       // new subdirectory in shaders: runs addPreset
    PRESET_REMOVED,     // subdirectory deleted/moved out: remove from list
};

struct WatchEvent {
    WatchEventType type;
    std::string    path;   // absolute path of changed file or directory
};

// Constructed once. Call updateActivePreset() whenever the active preset
// changes. Call drain() once per frame on the main thread to get events.
class ShaderWatcher {
public:
    ShaderWatcher() = default;
    ~ShaderWatcher() { stop(); }

    ShaderWatcher(const ShaderWatcher&) = delete;
    ShaderWatcher& operator=(const ShaderWatcher&) = delete;

    // shadersDir: absolute path to the top-level shaders/ directory
    // presetDir: shaderDir of the currently active preset
    // textureFiles: filenames from the active spec.textures
    // fontFiles: filenames from the active spec.fonts
    bool init(const std::string& shadersDir,
              const std::string& presetDir,
              const std::vector<std::string>& textureFiles,
              const std::vector<std::string>& fontFiles) {
        shadersPath = shadersDir;
        activeDir = presetDir;
        activeTextures = textureFiles;
        activeFonts = fontFiles;

#ifdef _WIN32
        return initWindows();
#else
        return initLinux();
#endif
    }

    void updateActivePreset(const std::string& presetDir,
                            const std::vector<std::string>& textureFiles,
                            const std::vector<std::string>& fontFiles) {
        std::lock_guard<std::mutex> lock(activeMutex);
        activeDir      = presetDir;
        activeTextures = textureFiles;
        activeFonts    = fontFiles;

#ifdef __linux__
        // inotify needs explicit watch updates, Windows watches recursively
        std::lock_guard<std::mutex> wlock(watchMutex);
        removeActiveWatches();
        setActiveWatches(presetDir, textureFiles, fontFiles);
#endif
    }

    void drain(std::vector<WatchEvent>& out) {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!eventQueue.empty()) {
            out.push_back(eventQueue.front());
            eventQueue.pop();
        }
    }

    void stop() {
        if (!running) return;
        running = false;

#ifdef _WIN32
        // Signal the wakeEvent so WaitForMultipleObjects unblocks
        if (wakeEvent != INVALID_HANDLE_VALUE) SetEvent(wakeEvent);
        if (watchThread.joinable()) watchThread.join();
        if (dirHandle != INVALID_HANDLE_VALUE) {
            CancelIo(dirHandle);
            CloseHandle(dirHandle);
            dirHandle = INVALID_HANDLE_VALUE;
        }
        if (wakeEvent != INVALID_HANDLE_VALUE) {
            CloseHandle(wakeEvent);
            wakeEvent = INVALID_HANDLE_VALUE;
        }
        if (overlapped.hEvent != INVALID_HANDLE_VALUE &&
            overlapped.hEvent != nullptr) {
            CloseHandle(overlapped.hEvent);
            overlapped.hEvent = INVALID_HANDLE_VALUE;
        }
#else
        char c = 1;
        if (write(wakePipe[1], &c, 1) < 0) {}
        if (watchThread.joinable()) watchThread.join();
        if (ifd >= 0) { close(ifd); ifd = -1; }
        if (wakePipe[0] >= 0) { close(wakePipe[0]); wakePipe[0] = -1; }
        if (wakePipe[1] >= 0) { close(wakePipe[1]); wakePipe[1] = -1; }
#endif
    }

private:
    std::string              shadersPath;
    std::string              activeDir;
    std::vector<std::string> activeTextures;
    std::vector<std::string> activeFonts;
    std::mutex               activeMutex;

    std::queue<WatchEvent>   eventQueue;
    std::mutex               queueMutex;

    std::thread              watchThread;
    std::atomic<bool>        running{false};

    void pushEvent(WatchEventType type, const std::string& path) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!eventQueue.empty()) {
            auto& back = eventQueue.back();
            if (back.type == type && back.path == path) return;
        }
        eventQueue.push({ type, path });
    }

    // WINDOWS implementation
#ifdef _WIN32

    HANDLE    dirHandle  = INVALID_HANDLE_VALUE;
    HANDLE    wakeEvent  = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {};

    // ReadDirectoryChangesW result buffer
    static constexpr DWORD RDC_BUF_SIZE = 65536;
    alignas(DWORD) char rdcBuf[RDC_BUF_SIZE];

    bool initWindows() {
        // Open the shaders directory for async change notifications
        dirHandle = CreateFileA(
            shadersPath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (dirHandle == INVALID_HANDLE_VALUE) {
            std::cerr << "ShaderWatcher: CreateFile failed for "
                      << shadersPath << "\n";
            return false;
        }

        // Event for overlapped I/O completion
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            std::cerr << "ShaderWatcher: CreateEvent failed\n";
            return false;
        }

        // Event for waking the thread on stop()
        wakeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (wakeEvent == nullptr) {
            std::cerr << "ShaderWatcher: CreateEvent (wake) failed\n";
            return false;
        }

        running = true;
        watchThread = std::thread(&ShaderWatcher::threadFuncWindows, this);
        return true;
    }

    void threadFuncWindows() {
        while (running) {
            ResetEvent(overlapped.hEvent);

            BOOL ok = ReadDirectoryChangesW(
                dirHandle,
                rdcBuf, RDC_BUF_SIZE,
                TRUE,   // watch subtree recursively
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_FILE_NAME  |
                FILE_NOTIFY_CHANGE_DIR_NAME,
                nullptr,    // bytes returned. Not used in async mode
                &overlapped,
                nullptr);   // no completion routine

            if (!ok && GetLastError() != ERROR_IO_PENDING) {
                std::cerr << "ShaderWatcher: ReadDirectoryChangesW failed\n";
                break;
            }

            // Wait for either a change event or the stop signal
            HANDLE handles[2] = { overlapped.hEvent, wakeEvent };
            DWORD waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (!running || waited == WAIT_OBJECT_0 + 1) break; // wake signal

            if (waited != WAIT_OBJECT_0) break; // error

            DWORD bytesTransferred = 0;
            if (!GetOverlappedResult(dirHandle, &overlapped,
                                     &bytesTransferred, FALSE)) break;
            if (bytesTransferred == 0) continue;

            // Parse the results
            std::lock_guard<std::mutex> lock(activeMutex);

            DWORD offset = 0;
            for (;;) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    rdcBuf + offset);

                // Convert wide filename to narrow string
                int nameLen = WideCharToMultiByte(
                    CP_UTF8, 0,
                    info->FileName, info->FileNameLength / sizeof(WCHAR),
                    nullptr, 0, nullptr, nullptr);
                std::string relPath(nameLen, '\0');
                WideCharToMultiByte(
                    CP_UTF8, 0,
                    info->FileName, info->FileNameLength / sizeof(WCHAR),
                    relPath.data(), nameLen, nullptr, nullptr);

                // Replace Windows backslashes
                for (auto& c : relPath) if (c == '\\') c = '/';

                std::string fullPath =
                    (std::filesystem::path(shadersPath) / relPath).string();

                classifyAndPush(info->Action, relPath, fullPath);

                if (info->NextEntryOffset == 0) break;
                offset += info->NextEntryOffset;
            }
        }
    }

    void classifyAndPush(DWORD action, const std::string& relPath,
                         const std::string& fullPath) {
        namespace fs = std::filesystem;

        bool isAdd    = (action == FILE_ACTION_ADDED ||
                         action == FILE_ACTION_RENAMED_NEW_NAME);
        bool isRemove = (action == FILE_ACTION_REMOVED ||
                         action == FILE_ACTION_RENAMED_OLD_NAME);
        bool isModify = (action == FILE_ACTION_MODIFIED);

        std::string filename = fs::path(relPath).filename().string();
        std::string parentRel = fs::path(relPath).parent_path().string();

        // top-level subdirectory added/removed = preset add/remove
        // A top-level entry has no parent in the relative path (parentRel is "")
        if (parentRel.empty() || parentRel == ".") {
            if (isAdd) {
                // Could be a directory — check
                if (fs::exists(fullPath) && fs::is_directory(fullPath)) {
                    pushEvent(WatchEventType::PRESET_ADDED, fullPath);
                }
            } else if (isRemove) {
                // Can't check is_directory since it's gone — push anyway,
                // shader_system will validate
                pushEvent(WatchEventType::PRESET_REMOVED, fullPath);
            }
            return;
        }

        // only process events under the active preset's directory
        std::string activeRel = fs::relative(activeDir, shadersPath).string();
        for (auto& c : activeRel) if (c == '\\') c = '/';

        // Check if this file is under the active preset
        if (relPath.rfind(activeRel + "/", 0) != 0) return;

        if (!isModify && !isAdd) return;

        // classify by filename
        if (filename == "frag.glsl" || filename == "spec.cfg") {
            pushEvent(WatchEventType::SHADER_CHANGED, fullPath);
            return;
        }

        for (auto& tf : activeTextures) {
            if (filename == tf) {
                pushEvent(WatchEventType::TEXTURE_CHANGED, fullPath);
                return;
            }
        }

        for (auto& ff : activeFonts) {
            if (filename == ff) {
                pushEvent(WatchEventType::FONT_CHANGED, fullPath);
                return;
            }
        }
    }

#endif // _WIN32

    // LINUX implementation
#ifdef __linux__

    int ifd = -1;
    int wakePipe[2] = {-1, -1};
    std::mutex watchMutex;

    struct WatchEntry {
        std::string    path;
        WatchEventType type;
        bool           isActivePreset = false;
    };
    std::unordered_map<int, WatchEntry> watches;

    bool initLinux() {
        ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (ifd < 0) {
            std::cerr << "ShaderWatcher: inotify_init1 failed\n";
            return false;
        }

        addWatch(shadersPath,
                 IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO,
                 WatchEventType::PRESET_ADDED);

        setActiveWatches(activeDir, activeTextures, activeFonts);

        running = true;
        watchThread = std::thread(&ShaderWatcher::threadFuncLinux, this);
        return true;
    }

    int addWatch(const std::string& path, uint32_t mask,
                 WatchEventType type, bool isActive = false) {
        int wd = inotify_add_watch(ifd, path.c_str(), mask);
        if (wd < 0) return -1;
        watches[wd] = { path, type, isActive };
        return wd;
    }

    void setActiveWatches(const std::string& presetDir,
                          const std::vector<std::string>& textureFiles,
                          const std::vector<std::string>& fontFiles) {
        auto fragPath = (std::filesystem::path(presetDir) / "frag.glsl").string();
        auto specPath = (std::filesystem::path(presetDir) / "spec.cfg").string();

        addWatch(fragPath,
                 IN_CLOSE_WRITE | IN_MOVED_TO,
                 WatchEventType::SHADER_CHANGED, true);
        addWatch(specPath,
                 IN_CLOSE_WRITE | IN_MOVED_TO,
                 WatchEventType::SHADER_CHANGED, true);
        // Preset dir watch catches vim/neovim rename-replace
        addWatch(presetDir,
                 IN_MOVED_TO | IN_CREATE,
                 WatchEventType::SHADER_CHANGED, true);

        for (auto& f : textureFiles) {
            auto p = (std::filesystem::path(presetDir) / f).string();
            addWatch(p, IN_CLOSE_WRITE | IN_MOVED_TO,
                     WatchEventType::TEXTURE_CHANGED, true);
        }
        for (auto& f : fontFiles) {
            auto p = (std::filesystem::path(presetDir) / f).string();
            addWatch(p, IN_CLOSE_WRITE | IN_MOVED_TO,
                     WatchEventType::FONT_CHANGED, true);
        }
    }

    void removeActiveWatches() {
        for (auto it = watches.begin(); it != watches.end(); ) {
            if (it->second.isActivePreset) {
                inotify_rm_watch(ifd, it->first);
                it = watches.erase(it);
            } else {
                ++it;
            }
        }
    }

    void threadFuncLinux() {
        if (pipe(wakePipe) < 0) {
            std::cerr << "ShaderWatcher: pipe failed\n";
            return;
        }

        constexpr size_t BUF_SIZE = sizeof(inotify_event) * 64 + NAME_MAX + 1;
        char buf[BUF_SIZE];

        while (running) {
            pollfd fds[2];
            fds[0].fd = ifd;       fds[0].events = POLLIN;
            fds[1].fd = wakePipe[0]; fds[1].events = POLLIN;

            int ret = poll(fds, 2, -1);
            if (ret <= 0 || !running) break;
            if (!(fds[0].revents & POLLIN)) continue;

            ssize_t len = read(ifd, buf, BUF_SIZE);
            if (len <= 0) continue;

            std::lock_guard<std::mutex> wlock(watchMutex);
            std::lock_guard<std::mutex> alock(activeMutex);

            ssize_t offset = 0;
            while (offset < len) {
                auto* ev = reinterpret_cast<inotify_event*>(buf + offset);
                offset += sizeof(inotify_event) + ev->len;

                auto it = watches.find(ev->wd);
                if (it == watches.end()) continue;

                const WatchEntry& entry = it->second;
                std::string eventPath = entry.path;
                if (ev->len > 0 && ev->name[0] != '\0')
                    eventPath = (std::filesystem::path(entry.path)
                                 / ev->name).string();

                // Top-level shaders dir: preset structural changes
                if (entry.path == shadersPath) {
                    if (!(ev->mask & IN_ISDIR)) continue;
                    if (ev->mask & (IN_CREATE | IN_MOVED_TO))
                        pushEvent(WatchEventType::PRESET_ADDED, eventPath);
                    else if (ev->mask & (IN_DELETE | IN_MOVED_FROM))
                        pushEvent(WatchEventType::PRESET_REMOVED, eventPath);
                    continue;
                }

                // Preset dir watch: catches rename-replace of any watched file
                if (entry.isActivePreset &&
                    std::filesystem::is_directory(entry.path)) {
                    if (!(ev->mask & IN_ISDIR)) {
                        std::string name = (ev->len > 0) ? ev->name : "";
                        if (name == "frag.glsl" || name == "spec.cfg") {
                            pushEvent(WatchEventType::SHADER_CHANGED, eventPath);
                            continue;
                        }
                        for (auto& tf : activeTextures) {
                            if (name == tf) {
                                pushEvent(WatchEventType::TEXTURE_CHANGED, eventPath);
                                break;
                            }
                        }
                        for (auto& ff : activeFonts) {
                            if (name == ff) {
                                pushEvent(WatchEventType::FONT_CHANGED, eventPath);
                                break;
                            }
                        }
                    }
                    continue;
                }

                // Per-file watches
                if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
                    pushEvent(entry.type, eventPath);
            }
        }
    }

#endif // __linux__
};

#endif // defined(__linux__) || defined(_WIN32)

