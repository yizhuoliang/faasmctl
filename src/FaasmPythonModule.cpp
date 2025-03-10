#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "FaasmClient.h"
#include "FaasmClientTypes.h"

// Define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK before including any protobuf headers
#define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK 1

#include "../faasmctl/util/gen_proto_cpp/faabric.pb.h"
#include "../faasmctl/util/gen_proto_cpp/planner.pb.h"

namespace py = pybind11;

// Utility function to calculate idle CPUs from in-flight apps
std::pair<int, int> get_num_idle_cpus_from_in_flight_apps(
    int num_vms, 
    int num_cpus_per_vm, 
    const std::vector<faabric::BatchExecuteRequestStatus>& in_flight_apps
) {
    int total_cpus = num_vms * num_cpus_per_vm;

    std::map<std::string, int> worker_occupation;
    int total_used_cpus = 0;
    
    for (const auto& app : in_flight_apps) {
        for (const auto& msg : app.messageresults()) {
            const std::string& ip = msg.executedhost();
            if (!ip.empty()) {
                if (worker_occupation.find(ip) == worker_occupation.end()) {
                    worker_occupation[ip] = 0;
                }
                worker_occupation[ip] += 1;
                total_used_cpus += 1;
            }
        }
    }

    int num_idle_vms = num_vms - static_cast<int>(worker_occupation.size());
    int num_idle_cpus = total_cpus - total_used_cpus;

    return std::make_pair(num_idle_vms, num_idle_cpus);
}

// Convert MessageMetrics to Python dictionary
py::dict message_metrics_to_dict(const faasmctl::MessageMetrics& metrics) {
    py::dict result;
    result["id"] = metrics.id;
    result["appId"] = metrics.appId;
    result["groupId"] = metrics.groupId;
    result["groupIdx"] = metrics.groupIdx;
    result["executedHost"] = metrics.executedHost;
    result["startTimestamp"] = metrics.startTimestamp;
    result["finishTimestamp"] = metrics.finishTimestamp;
    result["returnValue"] = metrics.returnValue;
    result["outputData"] = metrics.outputData;
    result["durationMs"] = metrics.getDurationMs();
    
    return result;
}

// Custom converter for protobuf messages to Python dictionaries
py::dict message_to_dict(const faabric::Message& msg) {
    py::dict result;
    result["id"] = msg.id();
    result["appId"] = msg.appid();
    result["groupId"] = msg.groupid();
    result["groupIdx"] = msg.groupidx();
    result["user"] = msg.user();
    result["function"] = msg.function();
    result["inputData"] = msg.inputdata();
    result["outputData"] = msg.outputdata();
    result["returnValue"] = msg.returnvalue();
    result["executedHost"] = msg.executedhost();
    result["startTimestamp"] = msg.starttimestamp();
    result["finishTimestamp"] = msg.finishtimestamp();
    
    // Calculate duration if both timestamps are present
    if (msg.starttimestamp() > 0 && msg.finishtimestamp() > 0) {
        result["durationMs"] = msg.finishtimestamp() - msg.starttimestamp();
    } else {
        result["durationMs"] = py::none();
    }
    
    return result;
}

py::dict batch_status_to_dict(const faabric::BatchExecuteRequestStatus& status) {
    py::dict result;
    result["appId"] = status.appid();
    result["finished"] = status.finished();
    result["expectedNumMessages"] = status.expectednummessages();
    
    py::list messageResults;
    for (const auto& msg : status.messageresults()) {
        messageResults.append(message_to_dict(msg));
    }
    result["messageResults"] = messageResults;
    
    // Extract metrics and add to result
    faasmctl::FaasmClient& client = faasmctl::FaasmClient::getInstance();
    auto metrics = client.extractMessageMetrics(status);
    
    py::list metricsResults;
    for (const auto& metric : metrics) {
        metricsResults.append(message_metrics_to_dict(metric));
    }
    result["metrics"] = metricsResults;
    
    return result;
}

// Helper to convert HttpMessage_Type to string
std::string httpMessageTypeToString(faabric::planner::HttpMessage_Type type) {
    switch(type) {
        case faabric::planner::HttpMessage_Type_RESET:
            return "RESET";
        case faabric::planner::HttpMessage_Type_FLUSH_AVAILABLE_HOSTS:
            return "FLUSH_AVAILABLE_HOSTS";
        case faabric::planner::HttpMessage_Type_FLUSH_EXECUTORS:
            return "FLUSH_EXECUTORS";
        case faabric::planner::HttpMessage_Type_FLUSH_SCHEDULING_STATE:
            return "FLUSH_SCHEDULING_STATE";
        case faabric::planner::HttpMessage_Type_GET_AVAILABLE_HOSTS:
            return "GET_AVAILABLE_HOSTS";
        case faabric::planner::HttpMessage_Type_GET_CONFIG:
            return "GET_CONFIG";
        case faabric::planner::HttpMessage_Type_GET_EXEC_GRAPH:
            return "GET_EXEC_GRAPH";
        case faabric::planner::HttpMessage_Type_GET_IN_FLIGHT_APPS:
            return "GET_IN_FLIGHT_APPS";
        case faabric::planner::HttpMessage_Type_EXECUTE_BATCH:
            return "EXECUTE_BATCH";
        case faabric::planner::HttpMessage_Type_EXECUTE_BATCH_STATUS:
            return "EXECUTE_BATCH_STATUS";
        case faabric::planner::HttpMessage_Type_PRELOAD_SCHEDULING_DECISION:
            return "PRELOAD_SCHEDULING_DECISION";
        case faabric::planner::HttpMessage_Type_SET_POLICY:
            return "SET_POLICY";
        case faabric::planner::HttpMessage_Type_GET_POLICY:
            return "GET_POLICY";
        case faabric::planner::HttpMessage_Type_SET_NEXT_EVICTED_VM:
            return "SET_NEXT_EVICTED_VM";
        default:
            return "NO_TYPE";
    }
}

// Helper to convert an InFlightApp to a Python dictionary
py::dict inFlightAppToDict(const faabric::planner::GetInFlightAppsResponse::InFlightApp& app) {
    py::dict result;
    result["appId"] = app.appid();
    result["subType"] = app.subtype();
    result["size"] = app.size();
    
    py::list hostIps;
    for (const auto& ip : app.hostips()) {
        hostIps.append(ip);
    }
    result["hostIps"] = hostIps;
    
    return result;
}

PYBIND11_MODULE(faasm_client_cpp, m) {
    m.doc() = "Python bindings for the FaasmClient C++ library";
    
    // Exception binding
    py::register_exception<faasmctl::FaasmClientException>(m, "FaasmClientException");
    py::register_exception<faasmctl::FaasmClientConfigException>(m, "FaasmClientConfigException");
    py::register_exception<faasmctl::FaasmClientRequestException>(m, "FaasmClientRequestException");
    
    // Add enum bindings for HttpMessage_Type
    py::enum_<faabric::planner::HttpMessage_Type>(m, "HttpMessageType")
        .value("NO_TYPE", faabric::planner::HttpMessage_Type_NO_TYPE)
        .value("RESET", faabric::planner::HttpMessage_Type_RESET)
        .value("FLUSH_AVAILABLE_HOSTS", faabric::planner::HttpMessage_Type_FLUSH_AVAILABLE_HOSTS)
        .value("FLUSH_EXECUTORS", faabric::planner::HttpMessage_Type_FLUSH_EXECUTORS)
        .value("FLUSH_SCHEDULING_STATE", faabric::planner::HttpMessage_Type_FLUSH_SCHEDULING_STATE)
        .value("GET_AVAILABLE_HOSTS", faabric::planner::HttpMessage_Type_GET_AVAILABLE_HOSTS)
        .value("GET_CONFIG", faabric::planner::HttpMessage_Type_GET_CONFIG)
        .value("GET_EXEC_GRAPH", faabric::planner::HttpMessage_Type_GET_EXEC_GRAPH)
        .value("GET_IN_FLIGHT_APPS", faabric::planner::HttpMessage_Type_GET_IN_FLIGHT_APPS)
        .value("EXECUTE_BATCH", faabric::planner::HttpMessage_Type_EXECUTE_BATCH)
        .value("EXECUTE_BATCH_STATUS", faabric::planner::HttpMessage_Type_EXECUTE_BATCH_STATUS)
        .value("PRELOAD_SCHEDULING_DECISION", faabric::planner::HttpMessage_Type_PRELOAD_SCHEDULING_DECISION)
        .value("SET_POLICY", faabric::planner::HttpMessage_Type_SET_POLICY)
        .value("GET_POLICY", faabric::planner::HttpMessage_Type_GET_POLICY)
        .value("SET_NEXT_EVICTED_VM", faabric::planner::HttpMessage_Type_SET_NEXT_EVICTED_VM)
        .export_values();
    
    // MessageMetrics class binding
    py::class_<faasmctl::MessageMetrics>(m, "MessageMetrics")
        .def(py::init<>())
        .def_readwrite("id", &faasmctl::MessageMetrics::id)
        .def_readwrite("appId", &faasmctl::MessageMetrics::appId)
        .def_readwrite("groupId", &faasmctl::MessageMetrics::groupId)
        .def_readwrite("groupIdx", &faasmctl::MessageMetrics::groupIdx)
        .def_readwrite("executedHost", &faasmctl::MessageMetrics::executedHost)
        .def_readwrite("startTimestamp", &faasmctl::MessageMetrics::startTimestamp)
        .def_readwrite("finishTimestamp", &faasmctl::MessageMetrics::finishTimestamp)
        .def_readwrite("returnValue", &faasmctl::MessageMetrics::returnValue)
        .def_readwrite("outputData", &faasmctl::MessageMetrics::outputData)
        .def("get_duration_ms", &faasmctl::MessageMetrics::getDurationMs)
        .def("__repr__", [](const faasmctl::MessageMetrics& m) {
            return "<MessageMetrics id=" + std::to_string(m.id) + 
                   " host=" + m.executedHost + 
                   " duration=" + std::to_string(m.getDurationMs()) + "ms>";
        });
    
    // FaasmClient class binding
    py::class_<faasmctl::FaasmClient>(m, "FaasmClient")
        .def_static("get_instance", &faasmctl::FaasmClient::getInstance, py::return_value_policy::reference)
        .def("init", &faasmctl::FaasmClient::init)
        .def("is_initialized", &faasmctl::FaasmClient::isInitialized)
        .def("invoke_wasm", [](faasmctl::FaasmClient& self, 
                             const std::map<std::string, std::string>& msgDict,
                             int numMessages,
                             const std::vector<std::string>* hostList,
                             bool isAsync) {
            auto result = self.invokeWasm(msgDict, numMessages, hostList, isAsync);
            return batch_status_to_dict(result);
        }, 
        py::arg("msg_dict"),
        py::arg("num_messages") = 1,
        py::arg("host_list") = nullptr,
        py::arg("is_async") = false)
        .def("check_async_status", [](faasmctl::FaasmClient& self, int32_t appId, int expectedNumMessages) {
            auto result = self.checkAsyncStatus(appId, expectedNumMessages);
            return batch_status_to_dict(result);
        },
        py::arg("app_id"),
        py::arg("expected_num_messages"))
        .def("get_inflight_apps", [](faasmctl::FaasmClient& self) {
            auto results = self.getInflightApps();
            py::list pyResults;
            for (const auto& status : results) {
                pyResults.append(batch_status_to_dict(status));
            }
            return pyResults;
        })
        .def("extract_message_metrics", [](faasmctl::FaasmClient& self, const faabric::BatchExecuteRequestStatus& status) {
            auto metrics = self.extractMessageMetrics(status);
            py::list pyMetrics;
            for (const auto& metric : metrics) {
                pyMetrics.append(message_metrics_to_dict(metric));
            }
            return pyMetrics;
        });
    
    // Utility functions
    m.def("get_num_idle_cpus_from_in_flight_apps", [](int num_vms, int num_cpus_per_vm, py::list in_flight_apps) {
        // Convert Python list to C++ vector
        std::vector<faabric::BatchExecuteRequestStatus> cppApps;
        for (auto app : in_flight_apps) {
            faabric::BatchExecuteRequestStatus status;
            // Extract data from Python dict
            py::dict appDict = app.cast<py::dict>();
            
            status.set_appid(appDict["appId"].cast<int>());
            status.set_finished(appDict["finished"].cast<bool>());
            
            py::list messages = appDict["messageResults"].cast<py::list>();
            for (auto msgObj : messages) {
                py::dict msgDict = msgObj.cast<py::dict>();
                faabric::Message* msg = status.add_messageresults();
                msg->set_executedhost(msgDict["executedHost"].cast<std::string>());
            }
            
            cppApps.push_back(status);
        }
        
        auto result = get_num_idle_cpus_from_in_flight_apps(num_vms, num_cpus_per_vm, cppApps);
        return py::make_tuple(result.first, result.second);
    }, py::arg("num_vms"), py::arg("num_cpus_per_vm"), py::arg("in_flight_apps"));
    
    // Add helper function for debugging
    m.def("http_message_type_to_string", &httpMessageTypeToString);
}