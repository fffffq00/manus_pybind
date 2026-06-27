#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "manus_client.h"

namespace py = pybind11;

PYBIND11_MODULE(_manus_pybind, m) {
    m.doc() = "Python bindings for the Manus SDK Client data-retrieval API";

    py::class_<manus_pybind::Transform>(m, "Transform")
        .def(py::init<>())
        .def_property_readonly("position", [](const manus_pybind::Transform &t) {
            return t.position;
        }, "List of 3 floats representing position in [x, y, z] order.")
        .def_property_readonly("rotation", [](const manus_pybind::Transform &t) {
            return t.rotation;
        }, "List of 4 floats representing rotation in [w, x, y, z] order.")
        .def_property_readonly("scale", [](const manus_pybind::Transform &t) {
            return t.scale;
        }, "List of 3 floats representing scale in [x, y, z] order.")
        .def_property_readonly_static("position_order", [](py::object) { return "xyz"; }, "The layout order of positions returned by this class (always 'xyz').")
        .def_property_readonly_static("rotation_order", [](py::object) { return "wxyz"; }, "The layout order of quaternions returned by this class (always 'wxyz').")
        .def("__str__", [](const manus_pybind::Transform& self) {
            std::ostringstream ss;
            ss << "Transform(position=[" << self.position[0] << ", " << self.position[1] << ", " << self.position[2] << "], "
               << "rotation=[" << self.rotation[0] << ", " << self.rotation[1] << ", " << self.rotation[2] << ", " << self.rotation[3] << "], "
               << "scale=[" << self.scale[0] << ", " << self.scale[1] << ", " << self.scale[2] << "])";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::Transform& self) {
            std::ostringstream ss;
            ss << "Transform(position=[" << self.position[0] << ", " << self.position[1] << ", " << self.position[2] << "], "
               << "rotation=[" << self.rotation[0] << ", " << self.rotation[1] << ", " << self.rotation[2] << ", " << self.rotation[3] << "], "
               << "scale=[" << self.scale[0] << ", " << self.scale[1] << ", " << self.scale[2] << "])";
            return ss.str();
        });

    py::class_<manus_pybind::SkeletonNode>(m, "SkeletonNode")
        .def(py::init<>())
        .def_readwrite("id", &manus_pybind::SkeletonNode::id)
        .def_readwrite("transform", &manus_pybind::SkeletonNode::transform)
        .def("__str__", [](const manus_pybind::SkeletonNode& self) {
            std::ostringstream ss;
            ss << "SkeletonNode(id=" << self.id
               << ", transform=Transform(position=[" << self.transform.position[0] << ", " << self.transform.position[1] << ", " << self.transform.position[2] << "], "
               << "rotation=[" << self.transform.rotation[0] << ", " << self.transform.rotation[1] << ", " << self.transform.rotation[2] << ", " << self.transform.rotation[3] << "], "
               << "scale=[" << self.transform.scale[0] << ", " << self.transform.scale[1] << ", " << self.transform.scale[2] << "]))";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::SkeletonNode& self) {
            std::ostringstream ss;
            ss << "SkeletonNode(id=" << self.id
               << ", transform=Transform(position=[" << self.transform.position[0] << ", " << self.transform.position[1] << ", " << self.transform.position[2] << "], "
               << "rotation=[" << self.transform.rotation[0] << ", " << self.transform.rotation[1] << ", " << self.transform.rotation[2] << ", " << self.transform.rotation[3] << "], "
               << "scale=[" << self.transform.scale[0] << ", " << self.transform.scale[1] << ", " << self.transform.scale[2] << "]))";
            return ss.str();
        });

    py::class_<manus_pybind::Skeleton>(m, "Skeleton")
        .def(py::init<>())
        .def_readwrite("id", &manus_pybind::Skeleton::id)
        .def_readwrite("publish_time_seconds", &manus_pybind::Skeleton::publish_time_seconds)
        .def_readwrite("hand", &manus_pybind::Skeleton::hand)
        .def_property_readonly("nodes", &manus_pybind::Skeleton::GetNodes)
        .def_property_readonly("node_ids", [](py::object &self) {
            auto &sk = self.cast<manus_pybind::Skeleton&>();
            size_t n = sk.node_ids.size();
            return py::array_t<uint32_t>(
                {n},
                {sizeof(uint32_t)},
                sk.node_ids.data(),
                self
            );
        })
        .def_property_readonly("node_positions", [](py::object &self) {
            auto &sk = self.cast<manus_pybind::Skeleton&>();
            size_t n = sk.node_ids.size();
            return py::array_t<float>(
                {n, (size_t)3},
                {3 * sizeof(float), sizeof(float)},
                sk.node_positions.data(),
                self
            );
        }, "2D numpy array of shape (N, 3) representing node positions in [x, y, z] order.")
        .def_property_readonly("node_rotations", [](py::object &self) {
            auto &sk = self.cast<manus_pybind::Skeleton&>();
            size_t n = sk.node_ids.size();
            return py::array_t<float>(
                {n, (size_t)4},
                {4 * sizeof(float), sizeof(float)},
                sk.node_rotations.data(),
                self
            );
        }, "2D numpy array of shape (N, 4) representing node rotations in [w, x, y, z] order.")
        .def_property_readonly("node_scales", [](py::object &self) {
            auto &sk = self.cast<manus_pybind::Skeleton&>();
            size_t n = sk.node_ids.size();
            return py::array_t<float>(
                {n, (size_t)3},
                {3 * sizeof(float), sizeof(float)},
                sk.node_scales.data(),
                self
            );
        }, "2D numpy array of shape (N, 3) representing node scales in [x, y, z] order.")
        .def_property_readonly_static("position_order", [](py::object) { return "xyz"; }, "The layout order of positions returned by this class (always 'xyz').")
        .def_property_readonly_static("rotation_order", [](py::object) { return "wxyz"; }, "The layout order of quaternions returned by this class (always 'wxyz').")
        .def("__str__", [](const manus_pybind::Skeleton& self) {
            std::ostringstream ss;
            ss << "Skeleton(id=" << self.id << ", publish_time_seconds=" << self.publish_time_seconds
               << ", hand=" << self.hand << ", nodes_count=" << self.node_ids.size() << ")";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::Skeleton& self) {
            std::ostringstream ss;
            ss << "Skeleton(id=" << self.id << ", publish_time_seconds=" << self.publish_time_seconds
               << ", hand=" << self.hand << ", nodes_count=" << self.node_ids.size() << ")";
            return ss.str();
        });

    py::class_<manus_pybind::Tracker>(m, "Tracker")
        .def(py::init<>())
        .def_readwrite("id", &manus_pybind::Tracker::id)
        .def_readwrite("user_id", &manus_pybind::Tracker::user_id)
        .def_readwrite("is_hmd", &manus_pybind::Tracker::is_hmd)
        .def_readwrite("type", &manus_pybind::Tracker::type)
        .def_property_readonly("position", [](const manus_pybind::Tracker &t) {
            return t.position;
        })
        .def_property_readonly("rotation", [](const manus_pybind::Tracker &t) {
            return t.rotation;
        })
        .def_readwrite("quality", &manus_pybind::Tracker::quality)
        .def("__str__", [](const manus_pybind::Tracker& self) {
            std::ostringstream ss;
            ss << "Tracker(id=\"" << self.id << "\", user_id=" << self.user_id << ", is_hmd=" << (self.is_hmd ? "True" : "False")
               << ", type=" << self.type << ", position=[" << self.position[0] << ", " << self.position[1] << ", " << self.position[2]
               << "], rotation=[" << self.rotation[0] << ", " << self.rotation[1] << ", " << self.rotation[2] << ", " << self.rotation[3]
               << "], quality=" << self.quality << ")";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::Tracker& self) {
            std::ostringstream ss;
            ss << "Tracker(id=\"" << self.id << "\", user_id=" << self.user_id << ", is_hmd=" << (self.is_hmd ? "True" : "False")
               << ", type=" << self.type << ", position=[" << self.position[0] << ", " << self.position[1] << ", " << self.position[2]
               << "], rotation=[" << self.rotation[0] << ", " << self.rotation[1] << ", " << self.rotation[2] << ", " << self.rotation[3]
               << "], quality=" << self.quality << ")";
            return ss.str();
        });

    py::class_<manus_pybind::Ergonomics>(m, "Ergonomics")
        .def(py::init<>())
        .def_readwrite("id", &manus_pybind::Ergonomics::id)
        .def_readwrite("is_user_id", &manus_pybind::Ergonomics::is_user_id)
        .def_readwrite("hand", &manus_pybind::Ergonomics::hand)
        .def_readwrite("data", &manus_pybind::Ergonomics::data)
        .def("__str__", [](const manus_pybind::Ergonomics& self) {
            std::ostringstream ss;
            ss << "Ergonomics(id=" << self.id << ", is_user_id=" << (self.is_user_id ? "True" : "False")
               << ", hand=" << self.hand << ", data_length=" << self.data.size() << ")";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::Ergonomics& self) {
            std::ostringstream ss;
            ss << "Ergonomics(id=" << self.id << ", is_user_id=" << (self.is_user_id ? "True" : "False")
               << ", hand=" << self.hand << ", data_length=" << self.data.size() << ")";
            return ss.str();
        });

    py::class_<manus_pybind::Gesture>(m, "Gesture")
        .def(py::init<>())
        .def_readwrite("name", &manus_pybind::Gesture::name)
        .def_readwrite("probability", &manus_pybind::Gesture::probability)
        .def("__str__", [](const manus_pybind::Gesture& self) {
            std::ostringstream ss;
            ss << "Gesture(name=\"" << self.name << "\", probability=" << self.probability << ")";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::Gesture& self) {
            std::ostringstream ss;
            ss << "Gesture(name=\"" << self.name << "\", probability=" << self.probability << ")";
            return ss.str();
        });

    py::class_<manus_pybind::GestureData>(m, "GestureData")
        .def(py::init<>())
        .def_readwrite("id", &manus_pybind::GestureData::id)
        .def_readwrite("is_user_id", &manus_pybind::GestureData::is_user_id)
        .def_readwrite("gestures", &manus_pybind::GestureData::gestures)
        .def("__str__", [](const manus_pybind::GestureData& self) {
            std::ostringstream ss;
            ss << "GestureData(id=" << self.id << ", is_user_id=" << (self.is_user_id ? "True" : "False")
               << ", gestures_count=" << self.gestures.size() << ")";
            return ss.str();
        })
        .def("__repr__", [](const manus_pybind::GestureData& self) {
            std::ostringstream ss;
            ss << "GestureData(id=" << self.id << ", is_user_id=" << (self.is_user_id ? "True" : "False")
               << ", gestures_count=" << self.gestures.size() << ")";
            return ss.str();
        });

    py::class_<manus_pybind::Host>(m, "Host")
        .def(py::init<>())
        .def_readwrite("hostname", &manus_pybind::Host::hostname)
        .def_readwrite("ip_address", &manus_pybind::Host::ip_address)
        .def_readwrite("version", &manus_pybind::Host::version)
        .def("__str__", [](const manus_pybind::Host &h) {
            return "Host(hostname=\"" + h.hostname + "\", ip_address=\"" + h.ip_address + "\", version=\"" + h.version + "\")";
        })
        .def("__repr__", [](const manus_pybind::Host &h) {
            return "Host(hostname=\"" + h.hostname + "\", ip_address=\"" + h.ip_address + "\", version=\"" + h.version + "\")";
        });

    py::class_<manus_pybind::ManusClient>(m, "ManusClient")
        .def(py::init<>())
        .def("initialize", &manus_pybind::ManusClient::Initialize, py::arg("connection_type") = 2, 
             "Initializes Manus Core SDK wrapper. ConnectionType: 1=Integrated, 2=Local, 3=Remote")
        .def("look_for_hosts", &manus_pybind::ManusClient::LookForHosts, py::arg("seconds") = 2, py::arg("local_only") = true,
             "Scans the network for running Manus Core servers.")
        .def("connect_to_host", &manus_pybind::ManusClient::ConnectToHost, py::arg("ip_address") = "", py::arg("connection_type") = 2,
             "Connects to the specified Core Host IP, or falls back to first detected host.")
        .def("disconnect", &manus_pybind::ManusClient::Disconnect, 
             "Shuts down the Core SDK client.")
        .def("is_connected", &manus_pybind::ManusClient::IsConnected, 
             "Returns True if successfully connected to Manus Core.")
        .def("get_skeletons", &manus_pybind::ManusClient::GetSkeletons, 
             "Retrieves the active retargeted skeleton nodes data.")
        .def("get_raw_skeletons", &manus_pybind::ManusClient::GetRAWSkeletons, 
             "Retrieves the raw (non-retargeted) skeleton joint transforms.")
        .def("get_tracker_data", &manus_pybind::ManusClient::GetTrackerData, 
             "Retrieves trackers position and orientation updates.")
        .def("get_ergonomics_data", &manus_pybind::ManusClient::GetErgonomicsData, 
             "Retrieves finger ergonomics angle measurements.")
        .def("get_gesture_data", &manus_pybind::ManusClient::GetGestureData, 
             "Retrieves real-time gesture matching probabilities.")
        .def("set_log_level", &manus_pybind::ManusClient::SetLogLevel, py::arg("level"),
             "Sets the console log level (0=Debug, 1=Info, 2=Warn, 3=Error)");
}
