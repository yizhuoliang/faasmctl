#pragma once

#include <string>
#include <stdexcept>

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

}  // namespace faasmctl