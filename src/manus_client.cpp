#include "manus_client.h"

namespace manus_pybind {

// Skeleton implementation
std::vector<SkeletonNode> Skeleton::GetNodes() const {
    std::vector<SkeletonNode> nodes;
    size_t n = node_ids.size();
    nodes.reserve(n);
    for (size_t i = 0; i < n; i++) {
        SkeletonNode node;
        node.id = node_ids[i];
        node.transform.position = {node_positions[i * 3], node_positions[i * 3 + 1], node_positions[i * 3 + 2]};
        node.transform.rotation = {node_rotations[i * 4], node_rotations[i * 4 + 1], node_rotations[i * 4 + 2], node_rotations[i * 4 + 3]};
        node.transform.scale = {node_scales[i * 3], node_scales[i * 3 + 1], node_scales[i * 3 + 2]};
        nodes.push_back(node);
    }
    return nodes;
}

// Utility functions
inline std::vector<float> rotate_vector_by_quaternion(const std::vector<float>& v, const std::vector<float>& q) {
    float tx = 2.0f * (q[2] * v[2] - q[3] * v[1] + q[0] * v[0]);
    float ty = 2.0f * (q[3] * v[0] - q[1] * v[2] + q[0] * v[1]);
    float tz = 2.0f * (q[1] * v[1] - q[2] * v[0] + q[0] * v[2]);
    return std::vector<float>{
        v[0] + q[2] * tz - q[3] * ty,
        v[1] + q[3] * tx - q[1] * tz,
        v[2] + q[1] * ty - q[2] * tx
    };
}

inline std::vector<float> get_local_coords(const SkeletonNode& wrist_node, const SkeletonNode& target_node) {
    std::vector<float> rel_pos = {
        target_node.transform.position[0] - wrist_node.transform.position[0],
        target_node.transform.position[1] - wrist_node.transform.position[1],
        target_node.transform.position[2] - wrist_node.transform.position[2]
    };
    std::vector<float> wq_conj = {
        wrist_node.transform.rotation[0],
        -wrist_node.transform.rotation[1],
        -wrist_node.transform.rotation[2],
        -wrist_node.transform.rotation[3]
    };
    return rotate_vector_by_quaternion(rel_pos, wq_conj);
}

inline bool is_valid_ergo_data(const float* data) {
    bool all_zero = true;
    for (int i = 0; i < 20; i++) {
        float val = data[i];
        if (std::isnan(val) || std::isinf(val) || val < -360.0f || val > 360.0f) {
            return false;
        }
        if (val != 0.0f) {
            all_zero = false;
        }
    }
    return !all_zero;
}

// ManusClient implementation
ManusClient::ManusClient() {
    s_instance = this;
}

ManusClient::~ManusClient() {
    Disconnect();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool ManusClient::Initialize(int connectionType) {
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
    m_watcherThread = std::thread(&ManusClient::watcher_thread_func, this);

    m_initialized = true;
    return true;
}

std::vector<Host> ManusClient::LookForHosts(int seconds, bool localOnly) {
    if (!m_initialized) return {};

    SDKReturnCode res = CoreSdk_LookForHosts(seconds, localOnly);
    if (res != SDKReturnCode_Success) return {};

    uint32_t count = 0;
    CoreSdk_GetNumberOfAvailableHostsFound(&count);
    if (count == 0) return {};

    std::vector<ManusHost> hosts(count);
    CoreSdk_GetAvailableHostsFound(hosts.data(), count);

    std::vector<Host> pyHosts;
    for (const auto& h : hosts) {
        Host pyH;
        pyH.hostname = h.hostName;
        pyH.ip_address = h.ipAddress;
        pyH.version = std::to_string(h.manusCoreVersion.major) + "." +
                      std::to_string(h.manusCoreVersion.minor) + "." +
                      std::to_string(h.manusCoreVersion.patch);
        pyHosts.push_back(pyH);
    }
    return pyHosts;
}

bool ManusClient::ConnectToHost(const std::string& ipAddress, int connectionType) {
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

void ManusClient::Disconnect() {
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

bool ManusClient::IsConnected() const {
    return m_connected;
}

std::vector<Skeleton> ManusClient::GetSkeletons() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_skeletons;
}

std::vector<Skeleton> ManusClient::GetRAWSkeletons() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_rawSkeletons;
}

std::vector<Tracker> ManusClient::GetTrackerData() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_trackers;
}

std::vector<Ergonomics> ManusClient::GetErgonomicsData() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_ergonomics;
}

std::vector<GestureData> ManusClient::GetGestureData() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_gestures;
}

void ManusClient::SetLogLevel(int level) {
    s_logLevel = static_cast<LogSeverity>(level);
}

// Callback implementations
void ManusClient::OnConnected(const ManusHost* const p_Host) {
    if (s_instance) s_instance->m_connected = true;
}

void ManusClient::OnDisconnected(const ManusHost* const p_Host) {
    if (s_instance) s_instance->m_connected = false;
}

void ManusClient::OnSkeletonStream(const SkeletonStreamInfo* const p_SkeletonStreamInfo) {
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

        std::vector<::SkeletonNode> nodes(info.nodesCount);
        CoreSdk_GetSkeletonData(i, nodes.data(), info.nodesCount);

        std::vector<uint32_t> pyNodeIds;
        std::vector<float> pyPositions;
        std::vector<float> pyRotations;
        std::vector<float> pyScales;

        pyNodeIds.reserve(info.nodesCount);
        pyPositions.reserve(info.nodesCount * 3);
        pyRotations.reserve(info.nodesCount * 4);
        pyScales.reserve(info.nodesCount * 3);

        for (const auto& node : nodes) {
            pyNodeIds.push_back(node.id);
            pyPositions.push_back(node.transform.position.x);
            pyPositions.push_back(node.transform.position.y);
            pyPositions.push_back(node.transform.position.z);
            pyRotations.push_back(node.transform.rotation.w);
            pyRotations.push_back(node.transform.rotation.x);
            pyRotations.push_back(node.transform.rotation.y);
            pyRotations.push_back(node.transform.rotation.z);
            pyScales.push_back(node.transform.scale.x);
            pyScales.push_back(node.transform.scale.y);
            pyScales.push_back(node.transform.scale.z);
        }

        // Determine Left/Right hand side orientation
        uint32_t hand = 0;
        auto cache_it = s_instance->m_handSideCache.find(skeletonId);
        if (cache_it != s_instance->m_handSideCache.end()) {
            hand = cache_it->second;
        } else {
            uint32_t pinky_idx = (pyNodeIds.size() >= 25) ? 21 : 17;
            uint32_t index_idx = (pyNodeIds.size() >= 25) ? 6 : 5;
            
            int wrist_idx = -1, pinky_idx_pos = -1, index_idx_pos = -1;
            for (size_t k = 0; k < pyNodeIds.size(); k++) {
                if (pyNodeIds[k] == 0) wrist_idx = (int)k;
                else if (pyNodeIds[k] == pinky_idx) pinky_idx_pos = (int)k;
                else if (pyNodeIds[k] == index_idx) index_idx_pos = (int)k;
            }
            
            if (wrist_idx != -1 && pinky_idx_pos != -1 && index_idx_pos != -1) {
                std::vector<float> wrist_pos = {pyPositions[wrist_idx * 3], pyPositions[wrist_idx * 3 + 1], pyPositions[wrist_idx * 3 + 2]};
                std::vector<float> wrist_rot = {pyRotations[wrist_idx * 4], pyRotations[wrist_idx * 4 + 1], pyRotations[wrist_idx * 4 + 2], pyRotations[wrist_idx * 4 + 3]};
                std::vector<float> pinky_pos = {pyPositions[pinky_idx_pos * 3], pyPositions[pinky_idx_pos * 3 + 1], pyPositions[pinky_idx_pos * 3 + 2]};
                std::vector<float> index_pos = {pyPositions[index_idx_pos * 3], pyPositions[index_idx_pos * 3 + 1], pyPositions[index_idx_pos * 3 + 2]};
                
                SkeletonNode wrist_node;
                wrist_node.id = 0;
                wrist_node.transform.position = wrist_pos;
                wrist_node.transform.rotation = wrist_rot;
                
                SkeletonNode pinky_node;
                pinky_node.id = pinky_idx;
                pinky_node.transform.position = pinky_pos;
                
                SkeletonNode index_node;
                index_node.id = index_idx;
                index_node.transform.position = index_pos;
                
                std::vector<float> local_pinky = get_local_coords(wrist_node, pinky_node);
                std::vector<float> local_index = get_local_coords(wrist_node, index_node);
                hand = (local_pinky[1] - local_index[1] > 0.0f) ? 0 : 1; // 0=Left, 1=Right
                s_instance->m_handSideCache[skeletonId] = hand;
            }
        }

        // Merge in-place
        bool found = false;
        for (auto& sk : s_instance->m_skeletons) {
            if (sk.id == skeletonId) {
                sk.publish_time_seconds = p_SkeletonStreamInfo->publishTime.time;
                sk.node_ids = std::move(pyNodeIds);
                sk.node_positions = std::move(pyPositions);
                sk.node_rotations = std::move(pyRotations);
                sk.node_scales = std::move(pyScales);
                sk.hand = hand;
                found = true;
                break;
            }
        }
        if (!found) {
            Skeleton sk;
            sk.id = skeletonId;
            sk.publish_time_seconds = p_SkeletonStreamInfo->publishTime.time;
            sk.hand = hand;
            sk.node_ids = std::move(pyNodeIds);
            sk.node_positions = std::move(pyPositions);
            sk.node_rotations = std::move(pyRotations);
            sk.node_scales = std::move(pyScales);
            s_instance->m_skeletons.push_back(std::move(sk));
        }
        s_instance->m_skeletonLastUpdateTime[skeletonId] = now;
    }
}

void ManusClient::OnRawSkeletonStream(const SkeletonStreamInfo* const p_RawSkeletonStreamInfo) {
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

        std::vector<::SkeletonNode> nodes(info.nodesCount);
        CoreSdk_GetRawSkeletonData(i, nodes.data(), info.nodesCount);

        std::vector<uint32_t> pyNodeIds;
        std::vector<float> pyPositions;
        std::vector<float> pyRotations;
        std::vector<float> pyScales;

        pyNodeIds.reserve(info.nodesCount);
        pyPositions.reserve(info.nodesCount * 3);
        pyRotations.reserve(info.nodesCount * 4);
        pyScales.reserve(info.nodesCount * 3);

        for (const auto& node : nodes) {
            pyNodeIds.push_back(node.id);
            pyPositions.push_back(node.transform.position.x);
            pyPositions.push_back(node.transform.position.y);
            pyPositions.push_back(node.transform.position.z);
            pyRotations.push_back(node.transform.rotation.w);
            pyRotations.push_back(node.transform.rotation.x);
            pyRotations.push_back(node.transform.rotation.y);
            pyRotations.push_back(node.transform.rotation.z);
            pyScales.push_back(node.transform.scale.x);
            pyScales.push_back(node.transform.scale.y);
            pyScales.push_back(node.transform.scale.z);
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
                uint32_t pinky_idx = (pyNodeIds.size() >= 25) ? 21 : 17;
                uint32_t index_idx = (pyNodeIds.size() >= 25) ? 6 : 5;
                
                int wrist_idx = -1, pinky_idx_pos = -1, index_idx_pos = -1;
                for (size_t k = 0; k < pyNodeIds.size(); k++) {
                    if (pyNodeIds[k] == 0) wrist_idx = (int)k;
                    else if (pyNodeIds[k] == pinky_idx) pinky_idx_pos = (int)k;
                    else if (pyNodeIds[k] == index_idx) index_idx_pos = (int)k;
                }
                
                if (wrist_idx != -1 && pinky_idx_pos != -1 && index_idx_pos != -1) {
                    std::vector<float> wrist_pos = {pyPositions[wrist_idx * 3], pyPositions[wrist_idx * 3 + 1], pyPositions[wrist_idx * 3 + 2]};
                    std::vector<float> wrist_rot = {pyRotations[wrist_idx * 4], pyRotations[wrist_idx * 4 + 1], pyRotations[wrist_idx * 4 + 2], pyRotations[wrist_idx * 4 + 3]};
                    std::vector<float> pinky_pos = {pyPositions[pinky_idx_pos * 3], pyPositions[pinky_idx_pos * 3 + 1], pyPositions[pinky_idx_pos * 3 + 2]};
                    std::vector<float> index_pos = {pyPositions[index_idx_pos * 3], pyPositions[index_idx_pos * 3 + 1], pyPositions[index_idx_pos * 3 + 2]};
                    
                    SkeletonNode wrist_node;
                    wrist_node.id = 0;
                    wrist_node.transform.position = wrist_pos;
                    wrist_node.transform.rotation = wrist_rot;
                    
                    SkeletonNode pinky_node;
                    pinky_node.id = pinky_idx;
                    pinky_node.transform.position = pinky_pos;
                    
                    SkeletonNode index_node;
                    index_node.id = index_idx;
                    index_node.transform.position = index_pos;
                    
                    std::vector<float> local_pinky = get_local_coords(wrist_node, pinky_node);
                    std::vector<float> local_index = get_local_coords(wrist_node, index_node);
                    hand = (local_pinky[1] - local_index[1] > 0.0f) ? 0 : 1; // 0=Left, 1=Right
                    s_instance->m_handSideCache[gloveId] = hand;
                }
            }
        }

        // Merge in-place
        bool found = false;
        for (auto& sk : s_instance->m_rawSkeletons) {
            if (sk.id == gloveId) {
                sk.publish_time_seconds = p_RawSkeletonStreamInfo->publishTime.time;
                sk.node_ids = std::move(pyNodeIds);
                sk.node_positions = std::move(pyPositions);
                sk.node_rotations = std::move(pyRotations);
                sk.node_scales = std::move(pyScales);
                sk.hand = hand;
                found = true;
                break;
            }
        }
        if (!found) {
            Skeleton sk;
            sk.id = gloveId;
            sk.publish_time_seconds = p_RawSkeletonStreamInfo->publishTime.time;
            sk.hand = hand;
            sk.node_ids = std::move(pyNodeIds);
            sk.node_positions = std::move(pyPositions);
            sk.node_rotations = std::move(pyRotations);
            sk.node_scales = std::move(pyScales);
            s_instance->m_rawSkeletons.push_back(std::move(sk));
        }
        s_instance->m_rawSkeletonLastUpdateTime[gloveId] = now;
    }
}

void ManusClient::OnTrackerStream(const TrackerStreamInfo* const p_TrackerStreamInfo) {
    if (!s_instance) return;
    std::vector<Tracker> trackers(p_TrackerStreamInfo->trackerCount);
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

void ManusClient::OnErgonomicsCallback(const ErgonomicsStream* const p_Ergo) {
    if (!s_instance) return;
    
    std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
    
    if (p_Ergo->dataCount > 0) {
        s_instance->m_lastDataTime = std::chrono::steady_clock::now();
        s_instance->m_errorTriggered = false;
    }

    for (uint32_t i = 0; i < p_Ergo->dataCount; i++) {
        uint32_t hand = 2; // 0=Left, 1=Right, 2=Unknown
        if (!p_Ergo->data[i].isUserID) {
            uint32_t gloveId = p_Ergo->data[i].id;
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
                }
            }
        }

        bool has_left = (hand == 0);
        bool has_right = (hand == 1);
        if (hand == 2) {
            has_left = is_valid_ergo_data(p_Ergo->data[i].data);
            has_right = is_valid_ergo_data(p_Ergo->data[i].data + 20);
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
                Ergonomics leftErgo;
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
                Ergonomics rightErgo;
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

void ManusClient::OnLandscapeCallback(const Landscape* const p_Landscape) {
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

void ManusClient::OnGestureStream(const GestureStreamInfo* const p_GestureStream) {
    if (!s_instance) return;
    std::vector<GestureData> gestureList;
    for (uint32_t i = 0; i < p_GestureStream->gestureProbabilitiesCount; i++) {
        GestureProbabilities t_Probs;
        CoreSdk_GetGestureStreamData(i, 0, &t_Probs);

        GestureData pyGData;
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
            Gesture pyG;
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

void ManusClient::OnLogCallback(LogSeverity p_Severity, const char* const p_Log, uint32_t p_Length) {
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

void ManusClient::watcher_thread_func() {
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
                    auto it = std::remove_if(m_skeletons.begin(), m_skeletons.end(), [this, now](const Skeleton& sk) {
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
                    auto it = std::remove_if(m_rawSkeletons.begin(), m_rawSkeletons.end(), [this, now](const Skeleton& sk) {
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
                    auto it = std::remove_if(m_ergonomics.begin(), m_ergonomics.end(), [now](const Ergonomics& ergo) {
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

} // namespace manus_pybind