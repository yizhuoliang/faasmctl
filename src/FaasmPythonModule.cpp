#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "FaasmClient.h"
#include "FaasmClientTypes.h"

// Define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK before including any protobuf headers
#define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK 1

#include "../faasmctl/util/gen_proto_cpp/faabric.pb.h"

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

PYBIND11_MODULE(faasm_client_cpp, m) {
    m.doc() = "Python bindings for the FaasmClient C++ library";
    
    // Exception binding
    py::register_exception<faasmctl::FaasmClientException>(m, "FaasmClientException");
    py::register_exception<faasmctl::FaasmClientConfigException>(m, "FaasmClientConfigException");
    py::register_exception<faasmctl::FaasmClientRequestException>(m, "FaasmClientRequestException");
    
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
}