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
    std::vector<PyTracker> m_trackers;
    std::vector<PyErgonomics> m_ergonomics;
    std::vector<PyGestureData> m_gestures;
    std::unordered_map<uint32_t, std::string> m_gestureNames;

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

        m_initialized = true;
        return true;
    }

    // Look for hosts on the network (Local or Remote)
    std::vector<PyHost> look_for_hosts(int seconds, bool local_only) {
        if (!m_initialized) return {};

        SDKReturnCode res = CoreSdk_LookForHosts(seconds, local_only);
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

    // Connect to a selected host IP or fallback to local
    bool connect_to_host(const std::string& ip_address, int connectionType) {
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
                if (!ip_address.empty()) {
                    for (const auto& h : hosts) {
                        if (std::string(h.ipAddress) == ip_address) {
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
            return true;
        }
        return false;
    }

    // Shutdown and release resources
    void disconnect() {
        if (m_initialized) {
            CoreSdk_ShutDown();
            m_initialized = false;
            m_connected = false;
        }
    }

    bool is_connected() const {
        return m_connected;
    }

    // Thread-safe data fetchers for python script to poll data
    std::vector<PySkeleton> get_skeletons() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_skeletons;
    }

    std::vector<PySkeleton> get_raw_skeletons() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_rawSkeletons;
    }

    std::vector<PyTracker> get_tracker_data() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_trackers;
    }

    std::vector<PyErgonomics> get_ergonomics_data() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_ergonomics;
    }

    std::vector<PyGestureData> get_gesture_data() {
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
        std::vector<PySkeleton> skeletons(p_SkeletonStreamInfo->skeletonsCount);
        for (uint32_t i = 0; i < p_SkeletonStreamInfo->skeletonsCount; i++) {
            SkeletonInfo info;
            CoreSdk_GetSkeletonInfo(i, &info);
            skeletons[i].id = info.id;
            skeletons[i].publish_time_seconds = p_SkeletonStreamInfo->publishTime.time;

            std::vector<SkeletonNode> nodes(info.nodesCount);
            CoreSdk_GetSkeletonData(i, nodes.data(), info.nodesCount);

            skeletons[i].nodes.reserve(info.nodesCount);
            for (const auto& node : nodes) {
                PySkeletonNode pyNode;
                pyNode.id = node.id;
                pyNode.transform.position = {node.transform.position.x, node.transform.position.y, node.transform.position.z};
                pyNode.transform.rotation = {node.transform.rotation.w, node.transform.rotation.x, node.transform.rotation.y, node.transform.rotation.z};
                pyNode.transform.scale = {node.transform.scale.x, node.transform.scale.y, node.transform.scale.z};
                skeletons[i].nodes.push_back(pyNode);
            }
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_skeletons = std::move(skeletons);
    }

    static void OnRawSkeletonStream(const SkeletonStreamInfo* const p_RawSkeletonStreamInfo) {
        if (!s_instance) return;
        std::vector<PySkeleton> rawSkeletons(p_RawSkeletonStreamInfo->skeletonsCount);
        for (uint32_t i = 0; i < p_RawSkeletonStreamInfo->skeletonsCount; i++) {
            RawSkeletonInfo info;
            CoreSdk_GetRawSkeletonInfo(i, &info);
            rawSkeletons[i].id = info.gloveId;
            rawSkeletons[i].publish_time_seconds = p_RawSkeletonStreamInfo->publishTime.time;

            std::vector<SkeletonNode> nodes(info.nodesCount);
            CoreSdk_GetRawSkeletonData(i, nodes.data(), info.nodesCount);

            rawSkeletons[i].nodes.reserve(info.nodesCount);
            for (const auto& node : nodes) {
                PySkeletonNode pyNode;
                pyNode.id = node.id;
                pyNode.transform.position = {node.transform.position.x, node.transform.position.y, node.transform.position.z};
                pyNode.transform.rotation = {node.transform.rotation.w, node.transform.rotation.x, node.transform.rotation.y, node.transform.rotation.z};
                pyNode.transform.scale = {node.transform.scale.x, node.transform.scale.y, node.transform.scale.z};
                rawSkeletons[i].nodes.push_back(pyNode);
            }
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_rawSkeletons = std::move(rawSkeletons);
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
    }

    static void OnErgonomicsCallback(const ErgonomicsStream* const p_Ergo) {
        if (!s_instance) return;
        std::vector<PyErgonomics> ergoList;
        ergoList.reserve(p_Ergo->dataCount * 2);
        for (uint32_t i = 0; i < p_Ergo->dataCount; i++) {
            // Check if there is any non-zero value in the first 20 elements (Left Hand)
            bool has_left = false;
            for (int j = 0; j < 20; j++) {
                if (p_Ergo->data[i].data[j] != 0.0f) {
                    has_left = true;
                    break;
                }
            }

            // Check if there is any non-zero value in the last 20 elements (Right Hand)
            bool has_right = false;
            for (int j = 20; j < 40; j++) {
                if (p_Ergo->data[i].data[j] != 0.0f) {
                    has_right = true;
                    break;
                }
            }

            if (has_left) {
                PyErgonomics leftErgo;
                leftErgo.id = p_Ergo->data[i].id;
                leftErgo.is_user_id = p_Ergo->data[i].isUserID;
                leftErgo.hand = 0; // Left Hand
                leftErgo.data.assign(p_Ergo->data[i].data, p_Ergo->data[i].data + 20);
                ergoList.push_back(std::move(leftErgo));
            }

            if (has_right) {
                PyErgonomics rightErgo;
                rightErgo.id = p_Ergo->data[i].id;
                rightErgo.is_user_id = p_Ergo->data[i].isUserID;
                rightErgo.hand = 1; // Right Hand
                rightErgo.data.assign(p_Ergo->data[i].data + 20, p_Ergo->data[i].data + 40);
                ergoList.push_back(std::move(rightErgo));
            }
        }

        std::lock_guard<std::mutex> lock(s_instance->m_dataMutex);
        s_instance->m_ergonomics = std::move(ergoList);
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
        .def("look_for_hosts", &ManusClientPython::look_for_hosts, py::arg("seconds") = 2, py::arg("local_only") = true,
             "Scans the network for running Manus Core servers.")
        .def("connect_to_host", &ManusClientPython::connect_to_host, py::arg("ip_address") = "", py::arg("connection_type") = 2,
             "Connects to the specified Core Host IP, or falls back to first detected host.")
        .def("disconnect", &ManusClientPython::disconnect, 
             "Shuts down the Core SDK client.")
        .def("is_connected", &ManusClientPython::is_connected, 
             "Returns True if successfully connected to Manus Core.")
        .def("get_skeletons", &ManusClientPython::get_skeletons, 
             "Retrieves the active retargeted skeleton nodes data.")
        .def("get_raw_skeletons", &ManusClientPython::get_raw_skeletons, 
             "Retrieves the raw (non-retargeted) skeleton joint transforms.")
        .def("get_tracker_data", &ManusClientPython::get_tracker_data, 
             "Retrieves trackers position and orientation updates.")
        .def("get_ergonomics_data", &ManusClientPython::get_ergonomics_data, 
             "Retrieves finger ergonomics angle measurements.")
        .def("get_gesture_data", &ManusClientPython::get_gesture_data, 
             "Retrieves real-time gesture matching probabilities.")
        .def("set_log_level", [](ManusClientPython& self, int level) {
             ManusClientPython::s_logLevel = static_cast<LogSeverity>(level);
        }, "Sets the console log level (0=Debug, 1=Info, 2=Warn, 3=Error)");
}
