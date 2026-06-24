#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
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
#include "ManusSDK.h"

namespace py = pybind11;

// Pybind-friendly versions of Manus SDK data structures
struct PyVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PyQuaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PyTransform {
    PyVec3 position;
    PyQuaternion rotation;
    PyVec3 scale;
};

struct PySkeletonNode {
    uint32_t id = 0;
    PyTransform transform;
};

struct PySkeleton {
    uint32_t id = 0;
    uint64_t publish_time_seconds = 0;
    uint32_t hand = 0; // 0 for Left, 1 for Right
    std::vector<PySkeletonNode> nodes;
};

struct PyTracker {
    std::string id;
    uint32_t user_id = 0;
    bool is_hmd = false;
    int type = 0; // Maps to TrackerType enum
    PyVec3 position;
    PyQuaternion rotation;
    int quality = 0; // Maps to TrackingQuality enum
};

struct PyErgonomics {
    uint32_t id = 0;
    bool is_user_id = false;
    uint32_t hand = 0; // 0 for Left, 1 for Right
    std::vector<float> data; // 20 elements representing the active hand's ergonomics data
    std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
};

struct PyGesture {
    std::string name;
    float probability = 0.0f;
};

struct PyGestureData {
    uint32_t id = 0;
    bool is_user_id = false;
    std::vector<PyGesture> gestures;
};

struct PyHost {
    std::string hostname;
    std::string ip_address;
    std::string version;
};

inline PyVec3 rotate_vector_by_quaternion(const PyVec3& v, const PyQuaternion& q) {
    float tx = 2.0f * (q.y * v.z - q.z * v.y + q.w * v.x);
    float ty = 2.0f * (q.z * v.x - q.x * v.z + q.w * v.y);
    float tz = 2.0f * (q.x * v.y - q.y * v.x + q.w * v.z);
    PyVec3 r;
    r.x = v.x + q.y * tz - q.z * ty;
    r.y = v.y + q.z * tx - q.x * tz;
    r.z = v.z + q.x * ty - q.y * tx;
    return r;
}

inline PyVec3 get_local_coords(const PySkeletonNode& wrist_node, const PySkeletonNode& target_node) {
    PyVec3 rel_pos = {
        target_node.transform.position.x - wrist_node.transform.position.x,
        target_node.transform.position.y - wrist_node.transform.position.y,
        target_node.transform.position.z - wrist_node.transform.position.z
    };
    PyQuaternion wq_conj = {
        wrist_node.transform.rotation.w,
        -wrist_node.transform.rotation.x,
        -wrist_node.transform.rotation.y,
        -wrist_node.transform.rotation.z
    };
    return rotate_vector_by_quaternion(rel_pos, wq_conj);
}

class ManusClientPython {
public:
    static ManusClientPython* s_instance;
    static LogSeverity s_logLevel;
    std::mutex m_dataMutex;
    bool m_initialized = false;
    bool m_connected = false;

    // Local caches for stream data
    std::vector<PySkeleton> m_skeletons;
    std::vector<PySkeleton> m_rawSkeletons;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_skeletonLastUpdateTime;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_rawSkeletonLastUpdateTime;
    std::unordered_map<uint32_t, uint32_t> m_handSideCache;
    std::vector<PyTracker> m_trackers;
    std::vector<PyErgonomics> m_ergonomics;
    std::vector<PyGestureData> m_gestures;
    std::unordered_map<uint32_t, std::string> m_gestureNames;
    std::chrono::steady_clock::time_point m_lastDataTime = std::chrono::steady_clock::now();
    std::atomic<bool> m_errorTriggered{false};

    ManusClientPython() {
        s_instance = this;
    }

    ~ManusClientPython() {
        disconnect();
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    // Initialize Manus SDK and set up callbacks/coordinates
    bool initialize(int connectionType) {
        if (m_initialized) return true;

        SDKReturnCode result;
        if (connectionType == 1) { // ConnectionType_Integrated
            result = CoreSdk_InitializeIntegrated();
        } else {
            result = CoreSdk_InitializeCore();
        }

        if (result != SDKReturnCode_Success) {
            return false;
        }

        // Register all callbacks required to pull data from streams
        CoreSdk_RegisterCallbackForOnLog(&OnLogCallback);
        CoreSdk_RegisterCallbackForOnConnect(&OnConnected);
        CoreSdk_RegisterCallbackForOnDisconnect(&OnDisconnected);
        CoreSdk_RegisterCallbackForSkeletonStream(&OnSkeletonStream);
        CoreSdk_RegisterCallbackForRawSkeletonStream(&OnRawSkeletonStream);
        CoreSdk_RegisterCallbackForTrackerStream(&OnTrackerStream);
        CoreSdk_RegisterCallbackForGestureStream(&OnGestureStream);
        CoreSdk_RegisterCallbackForErgonomicsStream(&OnErgonomicsCallback);
        CoreSdk_RegisterCallbackForLandscapeStream(&OnLandscapeCallback);

        // Set the default coordinate system: Z-up, X-forward (positive), right-handed, in meter scale
        CoordinateSystemVUH vuh;
        CoordinateSystemVUH_Init(&vuh);
        vuh.handedness = Side_Right;
        vuh.up = AxisPolarity_PositiveZ;
        vuh.view = AxisView_XFromViewer;
        vuh.unitScale = 1.0f; // 1.0 = meters
        CoreSdk_InitializeCoordinateSystemWithVUH(vuh, true);

        m_watcherRunning = true;
        m_watcherThread = std::thread(&ManusClientPython::watcher_thread_func, this);

        m_initialized = true;
        return true;
    }

    // Scan local interface or LAN network for active Manus Core hosts
    std::vector<PyHost> lookForHosts(int seconds, bool localOnly) {
        if (!m_initialized) return {};

        SDKReturnCode res = CoreSdk_LookForHosts(seconds, localOnly);
        if (res != SDKReturnCode_Success) return {};

        uint32_t count = 0;
        CoreSdk_GetNumberOfAvailableHostsFound(&count);
        if (count == 0) return {};

        std::vector<ManusHost> hosts(count);
        CoreSdk_GetAvailableHostsFound(hosts.data(), count);

        std::vector<PyHost> pyHosts;
        for (const auto& h : hosts) {
            PyHost pyH;
            pyH.hostname = h.hostName;
            pyH.ip_address = h.ipAddress;
            pyH.version = std::to_string(h.manusCoreVersion.major) + "." +
                          std::to_string(h.manusCoreVersion.minor) + "." +
                          std::to_string(h.manusCoreVersion.patch);
            pyHosts.push_back(pyH);
        }
        return pyHosts;
    }

    // Connect to a designated Manus Core host IP or fallback to auto-detection
    bool connectToHost(const std::string& ipAddress, int connectionType) {
        if (!m_initialized) return false;

        ManusHost targetHost;
        ManusHost_Init(&targetHost);

        if (connectionType != 1) { // If not integrated, find host in host list
            uint32_t count = 0;
            CoreSdk_GetNumberOfAvailableHostsFound(&count);
            if (count > 0) {
                std::vector<ManusHost> hosts(count);
                CoreSdk_GetAvailableHostsFound(hosts.data(), count);
                bool found = false;
                if (!ipAddress.empty()) {
                    for (const auto& h : hosts) {
                        if (std::string(h.ipAddress) == ipAddress) {
                            targetHost = h;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    targetHost = hosts[0]; // fallback to first detected host
                }
            }
        }

        SDKReturnCode res = CoreSdk_ConnectToHost(targetHost);
        if (res == SDKReturnCode_Success) {
            m_connected = true;
            CoreSdk_SetRawSkeletonHandMotion(HandMotion_Auto);
            return true;
        }
        return false;
    }

    // Shutdown client connection and terminate background threads
    void disconnect() {
        if (m_initialized) {
            if (m_watcherRunning) {
                m_watcherRunning = false;
                if (m_watcherThread.joinable()) {
                    m_watcherThread.join();
                }
            }
            CoreSdk_ShutDown();
            m_initialized = false;
            m_connected = false;
        }
    }

    // Check connection status with Manus Core SDK
    bool isConnected() const {
        return m_connected;
    }

    // Retrieve active retargeted full-body/polygon skeleton nodes stream
    std::vector<PySkeleton> getSkeletons() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_skeletons;
    }

    // Retrieve active RAW skeleton joint transforms
    std::vector<PySkeleton> getRAWSkeletons() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_rawSkeletons;
    }

    // Retrieve active trackers stream data (HMD, steamvr trackers, etc)
    std::vector<PyTracker> getTrackerData() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_trackers;
    }

    // Retrieve real-time ergonomics stream data (joint stretch angles)
    std::vector<PyErgonomics> getErgonomicsData() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_ergonomics;
    }

    // Retrieve real-time gesture matching predictions data
    std::vector<PyGestureData> getGestureData() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_gestures;
    }

private:
    // Core SDK callback router endpoints
    static void OnConnected(const ManusHost* const p_Host) {
        if (s_instance) s_instance->m_connected = true;
    }

    static void OnDisconnected(const ManusHost* const p_Host) {
        if (s_instance) s_instance->m_connected = false;
    }

    static void OnSkeletonStream(const SkeletonStreamInfo* const p_SkeletonStreamInfo) {
        if (!s_instance) return;
        
        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        
        if (p_SkeletonStreamInfo->skeletonsCount > 0) {
            s_instance->m_lastDataTime = std::chrono::steady_clock::now();
            s_instance->m_errorTriggered = false;
        }

        auto now = std::chrono::steady_clock::now();

        for (uint32_t i = 0; i < p_SkeletonStreamInfo->skeletonsCount; i++) {
            SkeletonInfo info;
            CoreSdk_GetSkeletonInfo(i, &info);
            uint32_t skeletonId = info.id;

            std::vector<SkeletonNode> nodes(info.nodesCount);
            CoreSdk_GetSkeletonData(i, nodes.data(), info.nodesCount);

            std::vector<PySkeletonNode> pyNodes;
            pyNodes.reserve(info.nodesCount);
            for (const auto& node : nodes) {
                PySkeletonNode pyNode;
                pyNode.id = node.id;
                pyNode.transform.position = {node.transform.position.x, node.transform.position.y, node.transform.position.z};
                pyNode.transform.rotation = {node.transform.rotation.w, node.transform.rotation.x, node.transform.rotation.y, node.transform.rotation.z};
                pyNode.transform.scale = {node.transform.scale.x, node.transform.scale.y, node.transform.scale.z};
                pyNodes.push_back(pyNode);
            }

            // Determine Left/Right hand side orientation
            uint32_t hand = 0;
            auto cache_it = s_instance->m_handSideCache.find(skeletonId);
            if (cache_it != s_instance->m_handSideCache.end()) {
                hand = cache_it->second;
            } else {
                uint32_t pinky_idx = (pyNodes.size() >= 25) ? 21 : 17;
                uint32_t index_idx = (pyNodes.size() >= 25) ? 6 : 5;
                const PySkeletonNode* wrist = nullptr;
                const PySkeletonNode* pinky_mcp = nullptr;
                const PySkeletonNode* index_mcp = nullptr;
                for (const auto& node : pyNodes) {
                    if (node.id == 0) wrist = &node;
                    else if (node.id == pinky_idx) pinky_mcp = &node;
                    else if (node.id == index_idx) index_mcp = &node;
                }
                if (wrist && pinky_mcp && index_mcp) {
                    PyVec3 local_pinky = get_local_coords(*wrist, *pinky_mcp);
                    PyVec3 local_index = get_local_coords(*wrist, *index_mcp);
                    hand = (local_pinky.y - local_index.y > 0.0f) ? 0 : 1; // 0=Left, 1=Right
                    s_instance->m_handSideCache[skeletonId] = hand;
                }
            }

            // Merge in-place
            bool found = false;
            for (auto& sk : s_instance->m_skeletons) {
                if (sk.id == skeletonId) {
                    sk.publish_time_seconds = p_SkeletonStreamInfo->publishTime.time;
                    sk.nodes = std::move(pyNodes);
                    sk.hand = hand;
                    found = true;
                    break;
                }
            }
            if (!found) {
                PySkeleton sk;
                sk.id = skeletonId;
                sk.publish_time_seconds = p_SkeletonStreamInfo->publishTime.time;
                sk.hand = hand;
                sk.nodes = std::move(pyNodes);
                s_instance->m_skeletons.push_back(std::move(sk));
            }
            s_instance->m_skeletonLastUpdateTime[skeletonId] = now;
        }
    }

    static void OnRawSkeletonStream(const SkeletonStreamInfo* const p_RawSkeletonStreamInfo) {
        if (!s_instance) return;
        
        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        
        if (p_RawSkeletonStreamInfo->skeletonsCount > 0) {
            s_instance->m_lastDataTime = std::chrono::steady_clock::now();
            s_instance->m_errorTriggered = false;
        }

        auto now = std::chrono::steady_clock::now();

        for (uint32_t i = 0; i < p_RawSkeletonStreamInfo->skeletonsCount; i++) {
            RawSkeletonInfo info;
            CoreSdk_GetRawSkeletonInfo(i, &info);
            uint32_t gloveId = info.gloveId;

            std::vector<SkeletonNode> nodes(info.nodesCount);
            CoreSdk_GetRawSkeletonData(i, nodes.data(), info.nodesCount);

            std::vector<PySkeletonNode> pyNodes;
            pyNodes.reserve(info.nodesCount);
            for (const auto& node : nodes) {
                PySkeletonNode pyNode;
                pyNode.id = node.id;
                pyNode.transform.position = {node.transform.position.x, node.transform.position.y, node.transform.position.z};
                pyNode.transform.rotation = {node.transform.rotation.w, node.transform.rotation.x, node.transform.rotation.y, node.transform.rotation.z};
                pyNode.transform.scale = {node.transform.scale.x, node.transform.scale.y, node.transform.scale.z};
                pyNodes.push_back(pyNode);
            }

            // Determine Left/Right hand side orientation
            uint32_t hand = 0;
            auto cache_it = s_instance->m_handSideCache.find(gloveId);
            if (cache_it != s_instance->m_handSideCache.end()) {
                hand = cache_it->second;
            } else {
                GloveLandscapeData gloveData;
                GloveLandscapeData_Init(&gloveData);
                if (CoreSdk_GetDataForGlove_UsingGloveId(gloveId, &gloveData) == SDKReturnCode_Success &&
                    gloveData.side != Side_Invalid) {
                    hand = (gloveData.side == Side_Left) ? 0 : 1; // 0=Left, 1=Right
                    s_instance->m_handSideCache[gloveId] = hand;
                } else {
                    // Fallback to coordinate math if SDK data is unavailable
                    uint32_t pinky_idx = (pyNodes.size() >= 25) ? 21 : 17;
                    uint32_t index_idx = (pyNodes.size() >= 25) ? 6 : 5;
                    const PySkeletonNode* wrist = nullptr;
                    const PySkeletonNode* pinky_mcp = nullptr;
                    const PySkeletonNode* index_mcp = nullptr;
                    for (const auto& node : pyNodes) {
                        if (node.id == 0) wrist = &node;
                        else if (node.id == pinky_idx) pinky_mcp = &node;
                        else if (node.id == index_idx) index_mcp = &node;
                    }
                    if (wrist && pinky_mcp && index_mcp) {
                        PyVec3 local_pinky = get_local_coords(*wrist, *pinky_mcp);
                        PyVec3 local_index = get_local_coords(*wrist, *index_mcp);
                        hand = (local_pinky.y - local_index.y > 0.0f) ? 0 : 1; // 0=Left, 1=Right
                        s_instance->m_handSideCache[gloveId] = hand;
                    }
                }
            }

            // Merge in-place
            bool found = false;
            for (auto& sk : s_instance->m_rawSkeletons) {
                if (sk.id == gloveId) {
                    sk.publish_time_seconds = p_RawSkeletonStreamInfo->publishTime.time;
                    sk.nodes = std::move(pyNodes);
                    sk.hand = hand;
                    found = true;
                    break;
                }
            }
            if (!found) {
                PySkeleton sk;
                sk.id = gloveId;
                sk.publish_time_seconds = p_RawSkeletonStreamInfo->publishTime.time;
                sk.hand = hand;
                sk.nodes = std::move(pyNodes);
                s_instance->m_rawSkeletons.push_back(std::move(sk));
            }
            s_instance->m_rawSkeletonLastUpdateTime[gloveId] = now;
        }
    }

    static void OnTrackerStream(const TrackerStreamInfo* const p_TrackerStreamInfo) {
        if (!s_instance) return;
        std::vector<PyTracker> trackers(p_TrackerStreamInfo->trackerCount);
        for (uint32_t i = 0; i < p_TrackerStreamInfo->trackerCount; i++) {
            TrackerData data;
            CoreSdk_GetTrackerData(i, &data);

            trackers[i].id = std::string(data.trackerId.id);
            trackers[i].user_id = data.userId;
            trackers[i].is_hmd = data.isHmd;
            trackers[i].type = static_cast<int>(data.trackerType);
            trackers[i].position = {data.position.x, data.position.y, data.position.z};
            trackers[i].rotation = {data.rotation.w, data.rotation.x, data.rotation.y, data.rotation.z};
            trackers[i].quality = static_cast<int>(data.quality);
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_trackers = std::move(trackers);
        if (p_TrackerStreamInfo->trackerCount > 0) {
            s_instance->m_lastDataTime = std::chrono::steady_clock::now();
            s_instance->m_errorTriggered = false;
        }
    }

    static void OnErgonomicsCallback(const ErgonomicsStream* const p_Ergo) {
        if (!s_instance) return;
        
        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        
        if (p_Ergo->dataCount > 0) {
            s_instance->m_lastDataTime = std::chrono::steady_clock::now();
            s_instance->m_errorTriggered = false;
        }

        for (uint32_t i = 0; i < p_Ergo->dataCount; i++) {
            // Simultaneously check Left Hand (0-19) and Right Hand (20-39) values in a single loop
            bool has_left = false;
            bool has_right = false;
            for (int j = 0; j < 20; j++) {
                if (p_Ergo->data[i].data[j] != 0.0f) has_left = true;
                if (p_Ergo->data[i].data[j + 20] != 0.0f) has_right = true;
                if (has_left && has_right) break; // Early exit if both hands are detected
            }

            if (has_left) {
                bool found = false;
                for (auto& ergo : s_instance->m_ergonomics) {
                    if (ergo.id == p_Ergo->data[i].id && ergo.hand == 0) {
                        ergo.data.assign(p_Ergo->data[i].data, p_Ergo->data[i].data + 20);
                        ergo.last_update_time = std::chrono::steady_clock::now();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    PyErgonomics leftErgo;
                    leftErgo.id = p_Ergo->data[i].id;
                    leftErgo.is_user_id = p_Ergo->data[i].isUserID;
                    leftErgo.hand = 0; // Left Hand
                    leftErgo.data.assign(p_Ergo->data[i].data, p_Ergo->data[i].data + 20);
                    leftErgo.last_update_time = std::chrono::steady_clock::now();
                    s_instance->m_ergonomics.push_back(std::move(leftErgo));
                }
            }

            if (has_right) {
                bool found = false;
                for (auto& ergo : s_instance->m_ergonomics) {
                    if (ergo.id == p_Ergo->data[i].id && ergo.hand == 1) {
                        ergo.data.assign(p_Ergo->data[i].data + 20, p_Ergo->data[i].data + 40);
                        ergo.last_update_time = std::chrono::steady_clock::now();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    PyErgonomics rightErgo;
                    rightErgo.id = p_Ergo->data[i].id;
                    rightErgo.is_user_id = p_Ergo->data[i].isUserID;
                    rightErgo.hand = 1; // Right Hand
                    rightErgo.data.assign(p_Ergo->data[i].data + 20, p_Ergo->data[i].data + 40);
                    rightErgo.last_update_time = std::chrono::steady_clock::now();
                    s_instance->m_ergonomics.push_back(std::move(rightErgo));
                }
            }
        }
    }

    static void OnLandscapeCallback(const Landscape* const p_Landscape) {
        if (!s_instance) return;
        std::vector<GestureLandscapeData> gestureData(p_Landscape->gestureCount);
        if (p_Landscape->gestureCount > 0) {
            CoreSdk_GetGestureLandscapeData(gestureData.data(), (uint32_t)gestureData.size());
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_gestureNames.clear();
        for (const auto& g : gestureData) {
            s_instance->m_gestureNames[g.id] = std::string(g.name);
        }
    }

    static void OnGestureStream(const GestureStreamInfo* const p_GestureStream) {
        if (!s_instance) return;
        std::vector<PyGestureData> gestureList;
        for (uint32_t i = 0; i < p_GestureStream->gestureProbabilitiesCount; i++) {
            GestureProbabilities t_Probs;
            CoreSdk_GetGestureStreamData(i, 0, &t_Probs);

            PyGestureData pyGData;
            pyGData.id = t_Probs.id;
            pyGData.is_user_id = t_Probs.isUserID;

            std::vector<GestureProbability> rawProbs;
            rawProbs.reserve(t_Probs.totalGestureCount);

            uint32_t t_BatchCount = (t_Probs.totalGestureCount / MAX_GESTURE_DATA_CHUNK_SIZE) + 1;
            uint32_t t_ProbabilityIdx = 0;
            for (uint32_t b = 0; b < t_BatchCount; b++) {
                for (uint32_t j = 0; j < t_Probs.gestureCount; j++) {
                    rawProbs.push_back(t_Probs.gestureData[j]);
                }
                t_ProbabilityIdx += t_Probs.gestureCount;
                CoreSdk_GetGestureStreamData(i, t_ProbabilityIdx, &t_Probs);
            }

            for (const auto& gp : rawProbs) {
                PyGesture pyG;
                pyG.probability = gp.percent;
                std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
                auto it = s_instance->m_gestureNames.find(gp.id);
                if (it != s_instance->m_gestureNames.end()) {
                    pyG.name = it->second;
                } else {
                    pyG.name = "Gesture_" + std::to_string(gp.id);
                }
                pyGData.gestures.push_back(pyG);
            }
            gestureList.push_back(pyGData);
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_gestures = std::move(gestureList);
        if (p_GestureStream->gestureProbabilitiesCount > 0) {
            s_instance->m_lastDataTime = std::chrono::steady_clock::now();
            s_instance->m_errorTriggered = false;
        }
    }

    static void OnLogCallback(LogSeverity p_Severity, const char* const p_Log, uint32_t p_Length) {
        if (p_Severity >= s_logLevel) {
            std::string color_code = "";
            std::string level_text = "";
            switch (p_Severity) {
                case LogSeverity_Debug:
                    color_code = "\033[36m"; // Cyan
                    level_text = "[debug]";
                    break;
                case LogSeverity_Info:
                    color_code = "\033[32m"; // Green
                    level_text = "[info]";
                    break;
                case LogSeverity_Warn:
                    color_code = "\033[33m"; // Yellow
                    level_text = "[warning]";
                    break;
                case LogSeverity_Error:
                    color_code = "\033[31m"; // Red
                    level_text = "[error]";
                    break;
                default:
                    level_text = "[unknown]";
                    break;
            }
            
            // Generate current local timestamp with millisecond precision
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;
            
            std::tm tm_now;
#ifdef _WIN32
            localtime_s(&tm_now, &time_t_now);
#else
            localtime_r(&time_t_now, &tm_now);
#endif
            
            std::ostringstream ss;
            ss << "[" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") 
               << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
            std::string timestamp = ss.str();

            // Format message content and strip existing line endings
            std::string logStr(p_Log, p_Length);
            while (!logStr.empty() && (logStr.back() == '\n' || logStr.back() == '\r')) {
                logStr.pop_back();
            }
            
            // Format log: Gray timestamp, colored level brackets, and default message text
            std::cout << "\033[90m" << timestamp << "\033[0m "
                      << color_code << level_text << "\033[0m "
                      << logStr << "\n" << std::flush;
        }
    }

    std::thread m_watcherThread;
    std::atomic<bool> m_watcherRunning{false};

    void watcher_thread_func() {
        while (m_watcherRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!m_watcherRunning) break;

            bool timeout_occurred = false;
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDataTime).count();
                if (elapsed > 1000) {
                    m_skeletons.clear();
                    m_rawSkeletons.clear();
                    m_skeletonLastUpdateTime.clear();
                    m_rawSkeletonLastUpdateTime.clear();
                    m_handSideCache.clear();
                    m_trackers.clear();
                    m_gestures.clear();
                    m_ergonomics.clear();
                    if (!m_errorTriggered) {
                        m_errorTriggered = true;
                        timeout_occurred = true;
                    }
                } else {
                    // Global stream is still active, but individual gloves/skeletons might have disconnected.
                    // Prune timed-out individual skeletons.
                    if (!m_skeletons.empty()) {
                        auto it = std::remove_if(m_skeletons.begin(), m_skeletons.end(), [this, now](const PySkeleton& sk) {
                            auto update_it = m_skeletonLastUpdateTime.find(sk.id);
                            if (update_it != m_skeletonLastUpdateTime.end()) {
                                bool is_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(now - update_it->second).count() > 1000;
                                if (is_timeout) {
                                    m_handSideCache.erase(sk.id);
                                }
                                return is_timeout;
                            }
                            return true;
                        });
                        m_skeletons.erase(it, m_skeletons.end());
                    }
                    if (!m_rawSkeletons.empty()) {
                        auto it = std::remove_if(m_rawSkeletons.begin(), m_rawSkeletons.end(), [this, now](const PySkeleton& sk) {
                            auto update_it = m_rawSkeletonLastUpdateTime.find(sk.id);
                            if (update_it != m_rawSkeletonLastUpdateTime.end()) {
                                bool is_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(now - update_it->second).count() > 1000;
                                if (is_timeout) {
                                    m_handSideCache.erase(sk.id);
                                }
                                return is_timeout;
                            }
                            return true;
                        });
                        m_rawSkeletons.erase(it, m_rawSkeletons.end());
                    }
                    // Prune timed-out individual glove ergonomics cache entries.
                    if (!m_ergonomics.empty()) {
                        auto it = std::remove_if(m_ergonomics.begin(), m_ergonomics.end(), [now](const PyErgonomics& ergo) {
                            return std::chrono::duration_cast<std::chrono::milliseconds>(now - ergo.last_update_time).count() > 1000;
                        });
                        m_ergonomics.erase(it, m_ergonomics.end());
                    }
                }
            }

            if (timeout_occurred) {
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()
                ) % 1000;
                
                std::tm tm_now;
#ifdef _WIN32
                localtime_s(&tm_now, &time_t_now);
#else
                localtime_r(&time_t_now, &tm_now);
#endif
                
                std::ostringstream ss;
                ss << "[" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") 
                   << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
                
                std::cout << "\033[90m" << ss.str() << "\033[0m "
                          << "\033[31m[error]\033[0m "
                          << "Manus SDK data stream timed out. No active glove or tracker data received for 1 second. Clearing cache.\n" 
                          << std::flush;
            }
        }
    }
};

// Define static members
LogSeverity ManusClientPython::s_logLevel = LogSeverity_Warn;
ManusClientPython* ManusClientPython::s_instance = nullptr;

PYBIND11_MODULE(_manus_pybind, m) {
    m.doc() = "Python bindings for the Manus SDK Client data-retrieval API";

    py::class_<PyVec3>(m, "Vec3")
        .def(py::init<>())
        .def_readwrite("x", &PyVec3::x)
        .def_readwrite("y", &PyVec3::y)
        .def_readwrite("z", &PyVec3::z)
        .def("__repr__", [](const PyVec3 &v) {
            return "<manus_pybind.Vec3 x=" + std::to_string(v.x) + " y=" + std::to_string(v.y) + " z=" + std::to_string(v.z) + ">";
        });

    py::class_<PyQuaternion>(m, "Quaternion")
        .def(py::init<>())
        .def_readwrite("w", &PyQuaternion::w)
        .def_readwrite("x", &PyQuaternion::x)
        .def_readwrite("y", &PyQuaternion::y)
        .def_readwrite("z", &PyQuaternion::z)
        .def("__repr__", [](const PyQuaternion &q) {
            return "<manus_pybind.Quaternion w=" + std::to_string(q.w) + " x=" + std::to_string(q.x) + " y=" + std::to_string(q.y) + " z=" + std::to_string(q.z) + ">";
        });

    py::class_<PyTransform>(m, "Transform")
        .def(py::init<>())
        .def_readwrite("position", &PyTransform::position)
        .def_readwrite("rotation", &PyTransform::rotation)
        .def_readwrite("scale", &PyTransform::scale);

    py::class_<PySkeletonNode>(m, "SkeletonNode")
        .def(py::init<>())
        .def_readwrite("id", &PySkeletonNode::id)
        .def_readwrite("transform", &PySkeletonNode::transform);

    py::class_<PySkeleton>(m, "Skeleton")
        .def(py::init<>())
        .def_readwrite("id", &PySkeleton::id)
        .def_readwrite("publish_time_seconds", &PySkeleton::publish_time_seconds)
        .def_readwrite("hand", &PySkeleton::hand)
        .def_readwrite("nodes", &PySkeleton::nodes);

    py::class_<PyTracker>(m, "Tracker")
        .def(py::init<>())
        .def_readwrite("id", &PyTracker::id)
        .def_readwrite("user_id", &PyTracker::user_id)
        .def_readwrite("is_hmd", &PyTracker::is_hmd)
        .def_readwrite("type", &PyTracker::type)
        .def_readwrite("position", &PyTracker::position)
        .def_readwrite("rotation", &PyTracker::rotation)
        .def_readwrite("quality", &PyTracker::quality);

    py::class_<PyErgonomics>(m, "Ergonomics")
        .def(py::init<>())
        .def_readwrite("id", &PyErgonomics::id)
        .def_readwrite("is_user_id", &PyErgonomics::is_user_id)
        .def_readwrite("hand", &PyErgonomics::hand)
        .def_readwrite("data", &PyErgonomics::data);

    py::class_<PyGesture>(m, "Gesture")
        .def(py::init<>())
        .def_readwrite("name", &PyGesture::name)
        .def_readwrite("probability", &PyGesture::probability);

    py::class_<PyGestureData>(m, "GestureData")
        .def(py::init<>())
        .def_readwrite("id", &PyGestureData::id)
        .def_readwrite("is_user_id", &PyGestureData::is_user_id)
        .def_readwrite("gestures", &PyGestureData::gestures);

    py::class_<PyHost>(m, "Host")
        .def(py::init<>())
        .def_readwrite("hostname", &PyHost::hostname)
        .def_readwrite("ip_address", &PyHost::ip_address)
        .def_readwrite("version", &PyHost::version)
        .def("__repr__", [](const PyHost &h) {
            return "<manus_pybind.Host " + h.hostname + " (" + h.ip_address + ") version=" + h.version + ">";
        });

    py::class_<ManusClientPython>(m, "ManusClient")
        .def(py::init<>())
        .def("initialize", &ManusClientPython::initialize, py::arg("connection_type") = 2, 
             "Initializes Manus Core SDK wrapper. ConnectionType: 1=Integrated, 2=Local, 3=Remote")
        .def("look_for_hosts", &ManusClientPython::lookForHosts, py::arg("seconds") = 2, py::arg("local_only") = true,
             "Scans the network for running Manus Core servers.")
        .def("connect_to_host", &ManusClientPython::connectToHost, py::arg("ip_address") = "", py::arg("connection_type") = 2,
             "Connects to the specified Core Host IP, or falls back to first detected host.")
        .def("disconnect", &ManusClientPython::disconnect, 
             "Shuts down the Core SDK client.")
        .def("is_connected", &ManusClientPython::isConnected, 
             "Returns True if successfully connected to Manus Core.")
        .def("get_skeletons", &ManusClientPython::getSkeletons, 
             "Retrieves the active retargeted skeleton nodes data.")
        .def("get_raw_skeletons", &ManusClientPython::getRAWSkeletons, 
             "Retrieves the raw (non-retargeted) skeleton joint transforms.")
        .def("get_tracker_data", &ManusClientPython::getTrackerData, 
             "Retrieves trackers position and orientation updates.")
        .def("get_ergonomics_data", &ManusClientPython::getErgonomicsData, 
             "Retrieves finger ergonomics angle measurements.")
        .def("get_gesture_data", &ManusClientPython::getGestureData, 
             "Retrieves real-time gesture matching probabilities.")
        .def("set_log_level", [](ManusClientPython& self, int level) {
             ManusClientPython::s_logLevel = static_cast<LogSeverity>(level);
        }, "Sets the console log level (0=Debug, 1=Info, 2=Warn, 3=Error)");
}
