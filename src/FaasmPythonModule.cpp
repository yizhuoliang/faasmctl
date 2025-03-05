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

// Custom converter for protobuf messages to Python dictionaries
py::dict message_to_dict(const faabric::Message& msg) {
    py::dict result;
    result["id"] = msg.id();
    result["appId"] = msg.appid();
    result["user"] = msg.user();
    result["function"] = msg.function();
    result["inputData"] = msg.inputdata();
    result["outputData"] = msg.outputdata();
    result["returnValue"] = msg.returnvalue();
    result["executedHost"] = msg.executedhost();
    
    // Add other fields as needed
    
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
        .def("get_inflight_apps", [](faasmctl::FaasmClient& self) {
            auto results = self.getInflightApps();
            py::list pyResults;
            for (const auto& status : results) {
                pyResults.append(batch_status_to_dict(status));
            }
            return pyResults;
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