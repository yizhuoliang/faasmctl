#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace faasmctl {

/**
 * Base exception for FaasmClient errors
 */
class FaasmClientException : public std::runtime_error {
public:
    explicit FaasmClientException(const std::string& message) 
        : std::runtime_error(message) {}
};

/**
 * Exception for HTTP request errors
 */
class FaasmClientRequestException : public FaasmClientException {
public:
    FaasmClientRequestException(const std::string& message, int statusCode)
        : FaasmClientException(message), statusCode(statusCode) {}
    
    int getStatusCode() const { return statusCode; }
    
private:
    int statusCode;
};

/**
 * Exception for configuration errors
 */
class FaasmClientConfigException : public FaasmClientException {
public:
    explicit FaasmClientConfigException(const std::string& message)
        : FaasmClientException(message) {}
};

/**
 * Structure to hold execution metrics for a message
 */
struct MessageMetrics {
    int32_t id;                 // Message ID
    int32_t appId;              // Application ID
    int32_t groupId;            // Group ID
    int32_t groupIdx;           // Group index
    std::string executedHost;   // Host that executed the message
    int64_t startTimestamp;     // Start time in milliseconds
    int64_t finishTimestamp;    // Finish time in milliseconds
    int32_t returnValue;        // Return value from the function
    std::string outputData;     // Output data returned by the function
    
    // Calculate execution duration in milliseconds
    int64_t getDurationMs() const {
        if (startTimestamp > 0 && finishTimestamp > 0) {
            return finishTimestamp - startTimestamp;
        }
        return -1; // Indicates incomplete execution
    }
};

}  // namespace faasmctl