#include "FaasmClient.h"
#include "FaasmClientTypes.h"
#include <google/protobuf/util/json_util.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <random>
#include <filesystem>
#include <curl/curl.h>
#include <algorithm>

// Now include the generated protobuf header
#include "../faasmctl/util/gen_proto_cpp/faabric.pb.h" 

namespace faasmctl {

//------------------------------------------------------------------------------
// HELPER CLASSES
//------------------------------------------------------------------------------

/**
 * Simple INI file parser
 */
class IniParser {
public:
    explicit IniParser(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw FaasmClientConfigException("Failed to open INI file: " + filename);
        }
        
        std::string currentSection;
        std::string line;
        
        while (std::getline(file, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            // Skip comments and empty lines
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }
            
            // Parse section
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }
            
            // Parse key-value pair
            auto delimPos = line.find('=');
            if (delimPos != std::string::npos) {
                std::string key = line.substr(0, delimPos);
                std::string value = line.substr(delimPos + 1);
                
                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                config[currentSection][key] = value;
            }
        }
    }
    
    std::string getValue(const std::string& section, const std::string& key, const std::string& defaultValue = "") const {
        auto sectionIt = config.find(section);
        if (sectionIt != config.end()) {
            auto keyIt = sectionIt->second.find(key);
            if (keyIt != sectionIt->second.end()) {
                return keyIt->second;
            }
        }
        return defaultValue;
    }
    
private:
    std::map<std::string, std::map<std::string, std::string>> config;
};

// CURL callback for HTTP responses
size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        return newLength;
    } catch(std::bad_alloc& e) {
        return 0;
    }
}

//------------------------------------------------------------------------------
// PUBLIC METHODS
//------------------------------------------------------------------------------

// Singleton instance getter
FaasmClient& FaasmClient::getInstance() {
    static FaasmClient instance;
    return instance;
}

// Constructor
FaasmClient::FaasmClient() : initialized(false) {
    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_ALL);
}

// Initialize with INI file
void FaasmClient::init(const std::string& iniFile) {
    if (!std::filesystem::exists(iniFile)) {
        throw FaasmClientConfigException("INI file does not exist: " + iniFile);
    }
    
    iniFilePath = iniFile;
    initialized = true;
}

// Main method to invoke WebAssembly functions
faabric::BatchExecuteRequestStatus FaasmClient::invokeWasm(
    const std::map<std::string, std::string>& msgDict,
    int numMessages,
    const std::vector<std::string>* hostList,
    bool isAsync
) {
    if (!initialized) {
        throw FaasmClientConfigException("FaasmClient not initialized. Call init() first.");
    }
    
    // Create request dictionary
    std::map<std::string, std::string> reqDict;
    auto userIt = msgDict.find("user");
    auto funcIt = msgDict.find("function");
    
    if (userIt != msgDict.end()) {
        reqDict["user"] = userIt->second;
    }
    
    if (funcIt != msgDict.end()) {
        reqDict["function"] = funcIt->second;
    }
    
    // Create batch request
    auto req = createBatchRequest(reqDict, msgDict, numMessages);
    
    // Convert to JSON
    std::string jsonReq;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = false;
    google::protobuf::util::MessageToJsonString(*req, &jsonReq, options);
    
    // Prepare planner message
    std::string msg = preparePlannerMsg("EXECUTE_BATCH", jsonReq);
    
    // Get planner host and port
    auto [host, port] = getFaasmPlannerHostPort(inDocker());
    std::string url = "http://" + host + ":" + port;
    
    // Calculate expected number of messages
    int expectedNumMessages = numMessages;
    auto it = msgDict.find("mpi_world_size");
    if (it != msgDict.end()) {
        expectedNumMessages = std::stoi(it->second);
    }
    
    // Handle host list if provided
    if (hostList != nullptr && !hostList->empty()) {
        if (hostList->size() != expectedNumMessages) {
            throw FaasmClientException("Host list size must match number of messages");
        }
        
        // Ensure there are enough messages
        while (req->messages_size() < expectedNumMessages) {
            faabric::Message* msg = req->add_messages();
            *msg = *createMessage(msgDict, req->appid());
        }
        
        // Preload scheduling decision
        for (int groupIdx = 0; groupIdx < req->messages_size(); groupIdx++) {
            req->mutable_messages(groupIdx)->set_groupidx(groupIdx);
            req->mutable_messages(groupIdx)->set_executedhost((*hostList)[groupIdx]);
        }
        
        std::string preloadJson;
        google::protobuf::util::MessageToJsonString(*req, &preloadJson, options);
        std::string preloadMsg = preparePlannerMsg("PRELOAD_SCHEDULING_DECISION", preloadJson);
        
        try {
            httpPost(url, preloadMsg);
        } catch (const std::exception& e) {
            throw FaasmClientException("Error preloading scheduling decision: " + std::string(e.what()));
        }
    }
    
    // Handle synchronous vs asynchronous invocation
    if (isAsync) {
        // Just invoke and return empty status
        int32_t appId = invokeAsync(url, msg, expectedNumMessages);
        
        // Track async request for later retrieval
        {
            std::lock_guard<std::mutex> lock(asyncMutex);
            asyncRequests[appId] = std::make_pair(url, expectedNumMessages);
        }
        
        // Return empty status with just the app ID
        faabric::BatchExecuteRequestStatus emptyStatus;
        emptyStatus.set_appid(appId);
        emptyStatus.set_expectednummessages(expectedNumMessages);
        emptyStatus.set_finished(false);
        return emptyStatus;
    } else {
        // Invoke and wait for results
        return invokeAndAwait(url, msg, expectedNumMessages);
    }
}

/**
 * Get information about in-flight application requests
 * Can be used to monitor cluster state and check on async invocations
 * 
 * @return List of in-flight application statuses
 */
std::vector<faabric::BatchExecuteRequestStatus> FaasmClient::getInflightApps() {
    if (!initialized) {
        throw FaasmClientConfigException("FaasmClient not initialized. Call init() first.");
    }
    
    // Get planner URL
    auto [host, port] = getFaasmPlannerHostPort(inDocker());
    std::string url = "http://" + host + ":" + port;
    
    // Send request to get all in-flight apps
    std::string inFlightMsg = preparePlannerMsg("GET_IN_FLIGHT_APPS", "");
    std::string response;
    
    try {
        response = httpPost(url, inFlightMsg);
    } catch (const std::exception& e) {
        throw FaasmClientException("Error getting in-flight apps: " + std::string(e.what()));
    }
    
    // Parse response (format: {"appIds": [id1, id2, ...]})
    std::vector<faabric::BatchExecuteRequestStatus> result;
    
    try {
        // Parse JSON response
        google::protobuf::util::JsonParseOptions parseOptions;
        
        // Simple hand parsing of the JSON array (since we don't have a protobuf for this response)
        if (response.find("\"appIds\"") != std::string::npos) {
            size_t start = response.find('[');
            size_t end = response.find(']');
            
            if (start != std::string::npos && end != std::string::npos) {
                std::string idsStr = response.substr(start + 1, end - start - 1);
                std::stringstream ss(idsStr);
                std::string item;
                
                // Parse each app ID and query its status
                while (std::getline(ss, item, ',')) {
                    // Trim whitespace and quotes
                    item.erase(0, item.find_first_not_of(" \t\n\r\""));
                    item.erase(item.find_last_not_of(" \t\n\r\"") + 1);
                    
                    if (!item.empty()) {
                        int32_t appId = std::stoi(item);
                        
                        // Create status object
                        faabric::BatchExecuteRequestStatus status;
                        status.set_appid(appId);
                        
                        // Check if this is one of our tracked async requests
                        {
                            std::lock_guard<std::mutex> lock(asyncMutex);
                            auto it = asyncRequests.find(appId);
                            if (it != asyncRequests.end()) {
                                // Query actual status from the planner
                                status.set_expectednummessages(it->second.second);
                                
                                // Create status query message
                                std::string statusJson;
                                google::protobuf::util::JsonPrintOptions printOptions;
                                printOptions.add_whitespace = false;
                                google::protobuf::util::MessageToJsonString(status, &statusJson, printOptions);
                                std::string statusMsg = preparePlannerMsg("EXECUTE_BATCH_STATUS", statusJson);
                                
                                try {
                                    std::string statusResponse = httpPost(it->second.first, statusMsg);
                                    google::protobuf::util::JsonStringToMessage(statusResponse, &status, parseOptions);
                                    
                                    // If finished, remove from tracked requests
                                    if (status.finished()) {
                                        asyncRequests.erase(it);
                                    }
                                } catch (const FaasmClientRequestException& e) {
                                    if (std::string(e.what()) != "App not registered in results") {
                                        // Non-expected error, clear status
                                        status.set_finished(false);
                                    }
                                }
                            }
                        }
                        
                        result.push_back(status);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        throw FaasmClientException("Error parsing in-flight apps response: " + std::string(e.what()));
    }
    
    return result;
}

//------------------------------------------------------------------------------
// PRIVATE METHODS
//------------------------------------------------------------------------------

// Generate random ID (equivalent to Python's generate_gid)
int32_t FaasmClient::generateGid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int32_t> dist(100000, 999999);
    return dist(gen);
}

// Check if running in Docker
bool FaasmClient::inDocker() {
    std::ifstream cgroup("/proc/self/cgroup");
    std::string line;
    while (std::getline(cgroup, line)) {
        if (line.find("docker") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Get value from INI file
std::string FaasmClient::getFaasmIniValue(const std::string& section, const std::string& key) {
    if (!initialized) {
        throw FaasmClientConfigException("FaasmClient not initialized. Call init() first.");
    }
    
    IniParser parser(iniFilePath);
    std::string value = parser.getValue(section, key);
    
    if (value.empty()) {
        throw FaasmClientConfigException("Config value not found: [" + section + "] " + key);
    }
    
    return value;
}

// Get planner host and port from config
std::pair<std::string, std::string> FaasmClient::getFaasmPlannerHostPort(bool inDockerEnv) {
    std::string backend = getFaasmIniValue("Faasm", "backend");
    std::string host, port;
    
    if (backend == "compose" && inDockerEnv) {
        host = getFaasmIniValue("Faasm", "planner_host_in_docker");
        port = getFaasmIniValue("Faasm", "planner_port_in_docker");
    } else {
        host = getFaasmIniValue("Faasm", "planner_host");
        port = getFaasmIniValue("Faasm", "planner_port");
    }
    
    return std::make_pair(host, port);
}

// Prepare planner message (format as JSON with http_type and payload)
std::string FaasmClient::preparePlannerMsg(const std::string& msgType, const std::string& msgBody) {
    // Let's use a very direct approach that mimics exactly what the Python code does
    
    // 1. Create a JSON structure with http_type and payload
    // 2. For payload, use the raw string but replace all double quotes with escaped quotes
    
    std::string escapedJson = msgBody;
    size_t pos = 0;
    while ((pos = escapedJson.find("\"", pos)) != std::string::npos) {
        escapedJson.replace(pos, 1, "\\\"");
        pos += 2;  // Move past the escaped quote
    }
    
    std::stringstream ss;
    ss << "{\"http_type\": \"" << msgType << "\", \"payload\": \"" << escapedJson << "\"}";
    
    return ss.str();
}

// Make HTTP POST request
std::string FaasmClient::httpPost(const std::string& url, const std::string& data)
{
    // --- Debug printout so you can compare with Python's request ---
    std::cerr << "[DEBUG] HTTP POST to " << url << " with data:\n"
              << data << "\n\n";

    CURL* curl = curl_easy_init();
    std::string responseBuffer;
    
    if (!curl) {
        throw FaasmClientException("Failed to initialize CURL");
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0); // No timeout, matches Python's `timeout=None`
    
    CURLcode res = curl_easy_perform(curl);
    
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        throw FaasmClientException("HTTP request failed: " + std::string(curl_easy_strerror(res)));
    }
    
    if (httpCode != 200) {
        // The body of the response is in responseBuffer, so throw that as an exception
        throw FaasmClientRequestException(responseBuffer, httpCode);
    }
    
    return responseBuffer;
}

// Create a Message from a dictionary
std::shared_ptr<faabric::Message> FaasmClient::createMessage(
    const std::map<std::string, std::string>& msgDict, 
    int32_t appId
) {
    auto msg = std::make_shared<faabric::Message>();
    
    // Set fields from dictionary
    for (const auto& [key, value] : msgDict) {
        if (key == "user") {
            msg->set_user(value);
        } else if (key == "function") {
            msg->set_function(value);
        } else if (key == "input_data" || key == "inputData") {
            msg->set_inputdata(value);
        } else if (key == "mpi_world_size" || key == "mpiWorldSize") {
            msg->set_mpiworldsize(std::stoi(value));
        } else if (key == "py_user" || key == "pythonUser") {
            msg->set_pythonuser(value);
        } else if (key == "py_func" || key == "pythonFunction") {
            msg->set_pythonfunction(value);
        }
        // Add other field mappings as needed
    }
    
    // Set ID fields
    if (appId == 0) {
        appId = generateGid();
    }
    msg->set_appid(appId);
    msg->set_id(generateGid());
    
    return msg;
}

// Create a BatchExecuteRequest from dictionaries
std::shared_ptr<faabric::BatchExecuteRequest> FaasmClient::createBatchRequest(
    const std::map<std::string, std::string>& reqDict,
    const std::map<std::string, std::string>& msgDict,
    int numMessages
) {
    auto req = std::make_shared<faabric::BatchExecuteRequest>();
    
    // Set fields from request dictionary
    for (const auto& [key, value] : reqDict) {
        if (key == "user") {
            req->set_user(value);
        } else if (key == "function") {
            req->set_function(value);
        }
        // Add other field mappings as needed
    }
    
    // Generate app ID
    req->set_appid(generateGid());
    
    // Add messages
    for (int i = 0; i < numMessages; i++) {
        faabric::Message* msg = req->add_messages();
        *msg = *createMessage(msgDict, req->appid());
    }
    
    return req;
}

// Just send request without waiting for results (async mode)
int32_t FaasmClient::invokeAsync(
    const std::string& url,
    const std::string& jsonMsg,
    int expectedNumMessages,
    int numRetries
) {
    const int sleepPeriodMs = 1500;
    std::string responseText;
    
    // Try to invoke with retries for "No available hosts" errors
    for (int i = 0; i < numRetries; i++) {
        try {
            responseText = httpPost(url, jsonMsg);
            break;
        } catch (const FaasmClientRequestException& e) {
            if (e.getStatusCode() == 500 && e.what() == std::string("No available hosts")) {
                std::cerr << "No available hosts, retrying... " << (i + 1) << "/" << numRetries << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepPeriodMs));
            } else {
                throw; // Re-throw for other errors
            }
        }
    }
    
    // Parse response to get app ID
    faabric::BatchExecuteRequestStatus berStatus;
    google::protobuf::util::JsonParseOptions parseOptions;
    google::protobuf::util::JsonStringToMessage(responseText, &berStatus, parseOptions);
    berStatus.set_expectednummessages(expectedNumMessages);
    
    return berStatus.appid();
}

// Helper method to invoke and await results
faabric::BatchExecuteRequestStatus FaasmClient::invokeAndAwait(
    const std::string& url,
    const std::string& jsonMsg,
    int expectedNumMessages,
    int numRetries
) {
    const int pollPeriodMs = 2000;
    const int sleepPeriodMs = 1500;
    
    // Try to invoke with retries for "No available hosts" errors
    std::string responseText;
    bool success = false;
    
    for (int i = 0; i < numRetries; i++) {
        try {
            responseText = httpPost(url, jsonMsg);
            success = true;
            break;
        } catch (const FaasmClientRequestException& e) {
            if (e.getStatusCode() == 500 && e.what() == std::string("No available hosts")) {
                std::cerr << "No available hosts, retrying... " << (i + 1) << "/" << numRetries << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepPeriodMs));
            } else {
                throw; // Re-throw for other errors
            }
        }
    }
    
    if (!success) {
        throw FaasmClientException("Failed to invoke after " + std::to_string(numRetries) + " retries");
    }
    
    // Parse response
    faabric::BatchExecuteRequestStatus berStatus;
    google::protobuf::util::JsonParseOptions parseOptions;
    google::protobuf::util::JsonStringToMessage(responseText, &berStatus, parseOptions);
    berStatus.set_expectednummessages(expectedNumMessages);
    
    // Prepare status polling message
    std::string statusJson;
    google::protobuf::util::JsonPrintOptions printOptions;
    printOptions.add_whitespace = false;
    google::protobuf::util::MessageToJsonString(berStatus, &statusJson, printOptions);
    std::string statusMsg = preparePlannerMsg("EXECUTE_BATCH_STATUS", statusJson);
    
    // Poll until finished
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollPeriodMs));
        
        try {
            std::string statusResponse = httpPost(url, statusMsg);
            google::protobuf::util::JsonStringToMessage(statusResponse, &berStatus, parseOptions);
            
            if (berStatus.finished()) {
                break;
            }
        } catch (const FaasmClientRequestException& e) {
            if (std::string(e.what()) == "App not registered in results") {
                // This is expected, keep polling
            } else {
                std::cerr << "Error polling for status: " << e.what() << std::endl;
                break;
            }
        }
    }
    
    return berStatus;
}

}  // namespace faasmctl