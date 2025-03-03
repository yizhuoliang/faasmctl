#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

// Forward declarations for protobuf classes - full definitions will be included in .cpp files
namespace faabric {
    class Message;
    class BatchExecuteRequest;
    class BatchExecuteRequestStatus;
}

namespace faasmctl {

/**
 * Client for interacting with a Faasm cluster
 * Singleton class that handles initialization from config and WebAssembly invocation
 */
class FaasmClient {
public:
    // Singleton instance getter
    static FaasmClient& getInstance();
    
    // Initialize with config file path
    void init(const std::string& iniFilePath);
    
    // Check if initialized
    bool isInitialized() const { return initialized; }

    /**
     * Invoke a WebAssembly function on the Faasm cluster
     * 
     * @param msgDict Dictionary containing message properties
     * @param numMessages Number of messages to create
     * @param hostList Optional list of hosts to execute on
     * @param isAsync If true, returns immediately after sending request
     * @return Status of the batch execution (empty status object if isAsync=true)
     */
    faabric::BatchExecuteRequestStatus invokeWasm(
        const std::map<std::string, std::string>& msgDict,
        int numMessages = 1,
        const std::vector<std::string>* hostList = nullptr,
        bool isAsync = false
    );
    
    /**
     * Get information about in-flight application requests
     * Can be used to monitor cluster state and check on async invocations
     * 
     * @return List of in-flight application statuses
     */
    std::vector<faabric::BatchExecuteRequestStatus> getInflightApps();

private:
    // Private constructor for singleton
    FaasmClient();
    
    // Delete copy/move operations
    FaasmClient(const FaasmClient&) = delete;
    FaasmClient& operator=(const FaasmClient&) = delete;
    FaasmClient(FaasmClient&&) = delete;
    FaasmClient& operator=(FaasmClient&&) = delete;
    
    // Configuration
    std::string iniFilePath;
    bool initialized = false;
    
    // Track async invocations
    std::map<int32_t, std::pair<std::string, int>> asyncRequests; // appId -> {url, expectedNumMessages}
    std::mutex asyncMutex;
    
    // Helper methods
    std::string getFaasmIniValue(const std::string& section, const std::string& key);
    std::pair<std::string, std::string> getFaasmPlannerHostPort(bool inDocker = false);
    bool inDocker();
    int32_t generateGid();
    
    // Planner interaction helpers
    std::string preparePlannerMsg(const std::string& msgType, const std::string& msgBody);
    std::string httpPost(const std::string& url, const std::string& data);
    
    // Message creation helpers
    std::shared_ptr<faabric::Message> createMessage(
        const std::map<std::string, std::string>& msgDict, 
        int32_t appId = 0
    );
    
    std::shared_ptr<faabric::BatchExecuteRequest> createBatchRequest(
        const std::map<std::string, std::string>& reqDict,
        const std::map<std::string, std::string>& msgDict,
        int numMessages
    );
    
    // Execution and polling
    faabric::BatchExecuteRequestStatus invokeAndAwait(
        const std::string& url,
        const std::string& jsonMsg,
        int expectedNumMessages,
        int numRetries = 30
    );
    
    // Just send request without waiting (for async mode)
    int32_t invokeAsync(
        const std::string& url,
        const std::string& jsonMsg,
        int expectedNumMessages,
        int numRetries = 30
    );
};

}  // namespace faasmctl