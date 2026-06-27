#pragma once

#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include "ManusSDK.h"

namespace manus_pybind {

// Transform containing position, rotation (w,x,y,z), and scale directly stored as float vectors
struct Transform {
    std::vector<float> position{0.0f, 0.0f, 0.0f};
    std::vector<float> rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> scale{1.0f, 1.0f, 1.0f};
};

struct SkeletonNode {
    uint32_t id = 0;
    Transform transform;
};

struct Skeleton {
    uint32_t id = 0;
    uint64_t publish_time_seconds = 0;
    uint32_t hand = 0; // 0 for Left, 1 for Right
    std::vector<uint32_t> node_ids;
    std::vector<float> node_positions; // size N * 3
    std::vector<float> node_rotations; // size N * 4 (w, x, y, z)
    std::vector<float> node_scales;    // size N * 3

    // For backwards compatibility:
    std::vector<SkeletonNode> GetNodes() const;
};

struct Tracker {
    std::string id;
    uint32_t user_id = 0;
    bool is_hmd = false;
    int type = 0; // Maps to TrackerType enum
    std::vector<float> position{0.0f, 0.0f, 0.0f};
    std::vector<float> rotation{1.0f, 0.0f, 0.0f, 0.0f};
    int quality = 0; // Maps to TrackingQuality enum
};

struct Ergonomics {
    uint32_t id = 0;
    bool is_user_id = false;
    uint32_t hand = 0; // 0 for Left, 1 for Right
    std::vector<float> data; // 20 elements representing the active hand's ergonomics data
    std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
};

struct Gesture {
    std::string name;
    float probability = 0.0f;
};

struct GestureData {
    uint32_t id = 0;
    bool is_user_id = false;
    std::vector<Gesture> gestures;
};

struct Host {
    std::string hostname;
    std::string ip_address;
    std::string version;
};

inline std::vector<float> rotate_vector_by_quaternion(const std::vector<float>& v, const std::vector<float>& q);
inline std::vector<float> get_local_coords(const SkeletonNode& wrist_node, const SkeletonNode& target_node);
inline bool is_valid_ergo_data(const float* data);

class ManusClient {
public:
    inline static ManusClient* s_instance = nullptr;
    inline static LogSeverity s_logLevel = LogSeverity_Warn;

    std::mutex m_dataMutex;
    bool m_initialized = false;
    bool m_connected = false;

    // Local caches for stream data
    std::vector<Skeleton> m_skeletons;
    std::vector<Skeleton> m_rawSkeletons;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_skeletonLastUpdateTime;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_rawSkeletonLastUpdateTime;
    std::unordered_map<uint32_t, uint32_t> m_handSideCache;
    std::vector<Tracker> m_trackers;
    std::vector<Ergonomics> m_ergonomics;
    std::vector<GestureData> m_gestures;
    std::unordered_map<uint32_t, std::string> m_gestureNames;
    std::chrono::steady_clock::time_point m_lastDataTime = std::chrono::steady_clock::now();
    std::atomic<bool> m_errorTriggered{false};

    ManusClient();
    ~ManusClient();

    // Initialize Manus SDK and set up callbacks/coordinates
    bool Initialize(int connectionType);

    // Scan local interface or LAN network for active Manus Core hosts
    std::vector<Host> LookForHosts(int seconds, bool localOnly);

    // Connect to a designated Manus Core host IP or fallback to auto-detection (unmodified connection logic)
    bool ConnectToHost(const std::string& ipAddress, int connectionType);

    // Shutdown client connection and terminate background threads
    void Disconnect();

    // Check connection status with Manus Core SDK
    bool IsConnected() const;

    // Retrieve active retargeted full-body/polygon skeleton nodes stream
    std::vector<Skeleton> GetSkeletons();

    // Retrieve active RAW skeleton joint transforms
    std::vector<Skeleton> GetRAWSkeletons();

    // Retrieve active trackers stream data (HMD, steamvr trackers, etc)
    std::vector<Tracker> GetTrackerData();

    // Retrieve real-time ergonomics stream data (joint stretch angles)
    std::vector<Ergonomics> GetErgonomicsData();

    // Retrieve real-time gesture matching predictions data
    std::vector<GestureData> GetGestureData();

    // Set console logging severity
    void SetLogLevel(int level);

private:
    // Core SDK callback router endpoints
    static void OnConnected(const ManusHost* const p_Host);
    static void OnDisconnected(const ManusHost* const p_Host);
    static void OnSkeletonStream(const SkeletonStreamInfo* const p_SkeletonStreamInfo);
    static void OnRawSkeletonStream(const SkeletonStreamInfo* const p_RawSkeletonStreamInfo);
    static void OnTrackerStream(const TrackerStreamInfo* const p_TrackerStreamInfo);
    static void OnErgonomicsCallback(const ErgonomicsStream* const p_Ergo);
    static void OnLandscapeCallback(const Landscape* const p_Landscape);
    static void OnGestureStream(const GestureStreamInfo* const p_GestureStream);
    static void OnLogCallback(LogSeverity p_Severity, const char* const p_Log, uint32_t p_Length);

    std::thread m_watcherThread;
    std::atomic<bool> m_watcherRunning{false};

    void watcher_thread_func();
};

} // namespace manus_pybind