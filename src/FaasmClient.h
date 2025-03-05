#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

// Forward declarations for protobuf classes - full definitions will be included in .cpp files
namespace faabric {
    class Message;
    class BatchExecuteRequest;
    class BatchExecuteRequestStatus;
    
    // Add forward declarations for planner namespace
    namespace planner {
        class HttpMessage;
        class GetInFlightAppsResponse;
        
        // Forward declare the enum type - actual values defined in planner.pb.h
        enum HttpMessage_Type : int;
    }
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
     * @return Status of the batch execution (with appId for isAsync=true)
     */
    faabric::BatchExecuteRequestStatus invokeWasm(
        const std::map<std::string, std::string>& msgDict,
        int numMessages = 1,
        const std::vector<std::string>* hostList = nullptr,
        bool isAsync = false
    );
    
    /**
     * Check the status of an asynchronous WebAssembly function invocation
     * 
     * @param appId The application ID returned from the async invocation
     * @param expectedNumMessages The number of messages expected in the response
     * @return Status of the batch execution
     */
    faabric::BatchExecuteRequestStatus checkAsyncStatus(
        int32_t appId,
        int expectedNumMessages
    );
    
    /**
     * Get information about in-flight application requests
     * Can be used to monitor cluster state
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